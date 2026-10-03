#include "syzygy.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    using syzygy::ProbeState;
    using syzygy::WDLScore;

    constexpr int TB_PIECES = 7;
    constexpr int MAX_DTZ = 1 << 18;

    enum TableType
    {
        TABLE_WDL,
        TABLE_DTZ
    };

    constexpr uint8_t FLAG_STM = 1;
    constexpr uint8_t FLAG_MAPPED = 2;
    constexpr uint8_t FLAG_WIN_PLIES = 4;
    constexpr uint8_t FLAG_LOSS_PLIES = 8;
    constexpr uint8_t FLAG_WIDE = 16;
    constexpr uint8_t FLAG_SINGLE_VALUE = 128;

    constexpr uint8_t WDL_MAGIC[4] = {0x71, 0xE8, 0x23, 0x5D};
    constexpr uint8_t DTZ_MAGIC[4] = {0xD7, 0x66, 0x0C, 0xA5};
    constexpr const char *PIECE_CHARS = "PNBRQK";

    int mapPawns[SQUARE_NB];
    int mapB1H1H7[SQUARE_NB];
    int mapA1D1D4[SQUARE_NB];
    int mapKK[10][SQUARE_NB];
    int binomial[6][SQUARE_NB];
    int leadPawnIdx[6][SQUARE_NB];
    int leadPawnsSize[6][4];
    bool encodingReady = false;

    template <typename T>
    T readLE(const uint8_t *p)
    {
        T v;
        std::memcpy(&v, p, sizeof(T));
        return v;
    }

    uint32_t readBE32(const uint8_t *p)
    {
        return __builtin_bswap32(readLE<uint32_t>(p));
    }

    uint64_t readBE64(const uint8_t *p)
    {
        return __builtin_bswap64(readLE<uint64_t>(p));
    }

    int sqFile(int sq) { return sq & 7; }
    int sqRank(int sq) { return sq >> 3; }
    int offA1H8(int sq) { return sqRank(sq) - sqFile(sq); }
    bool pawnsBefore(int a, int b) { return mapPawns[a] < mapPawns[b]; }
    int signOf(int v) { return (0 < v) - (v < 0); }

    int dtzBeforeZeroing(int wdl)
    {
        return wdl == syzygy::WDL_WIN           ? 1
               : wdl == syzygy::WDL_CURSED_WIN   ? 101
               : wdl == syzygy::WDL_BLESSED_LOSS ? -101
               : wdl == syzygy::WDL_LOSS         ? -1
                                                 : 0;
    }

    struct PairsData
    {
        uint8_t flags = 0;
        uint8_t maxSymLen = 0;
        uint8_t minSymLen = 0;
        uint32_t numBlocks = 0;
        size_t sizeofBlock = 0;
        size_t span = 0;
        const uint8_t *lowestSym = nullptr;
        const uint8_t *btree = nullptr;
        const uint8_t *blockLength = nullptr;
        uint32_t blockLengthSize = 0;
        const uint8_t *sparseIndex = nullptr;
        size_t sparseIndexSize = 0;
        const uint8_t *data = nullptr;
        std::vector<uint64_t> base64;
        std::vector<uint8_t> symlen;
        int pieces[TB_PIECES] = {};
        uint64_t groupIdx[TB_PIECES + 1] = {};
        int groupLen[TB_PIECES + 1] = {};
        uint16_t mapIdx[4] = {};

        int left(int sym) const { return ((btree[3 * sym + 1] & 0xF) << 8) | btree[3 * sym]; }
        int right(int sym) const { return (btree[3 * sym + 2] << 4) | (btree[3 * sym + 1] >> 4); }
    };

    struct Table
    {
        TableType type = TABLE_WDL;
        std::string name;
        std::atomic<bool> ready{false};
        void *base = nullptr;
        void *handle = nullptr;
        size_t size = 0;
        const uint8_t *map = nullptr;
        uint64_t key = 0;
        uint64_t key2 = 0;
        int pieceCount = 0;
        bool hasPawns = false;
        bool hasUniquePieces = false;
        int pawnCount[2] = {};
        PairsData items[2][4];

        PairsData *get(int stm, int file)
        {
            return &items[type == TABLE_WDL ? stm % 2 : 0][hasPawns ? file : 0];
        }
    };

    std::vector<std::string> paths;
    std::deque<Table> tables;
    std::unordered_map<uint64_t, std::pair<Table *, Table *>> tableIndex;
    std::mutex mapMutex;
    int cardinality = 0;

    uint64_t materialKey(const int counts[COLOR_NB][PIECE_TYPE_NB])
    {
        uint64_t key = 0;
        for (int c = WHITE; c <= BLACK; ++c)
            for (int pt = PAWN; pt < KING; ++pt)
                key |= static_cast<uint64_t>(counts[c][pt]) << (4 * (c * 5 + pt));
        return key;
    }

    uint64_t positionKey(const Position &pos)
    {
        int counts[COLOR_NB][PIECE_TYPE_NB] = {};
        for (int c = WHITE; c <= BLACK; ++c)
            for (int pt = PAWN; pt < KING; ++pt)
                counts[c][pt] = std::popcount(pos.byColor[c] & pos.byType[pt]);
        return materialKey(counts);
    }

    void unmap(Table &e)
    {
        if (e.base == nullptr)
            return;
#if defined(_WIN32)
        UnmapViewOfFile(e.base);
        CloseHandle(static_cast<HANDLE>(e.handle));
#else
        munmap(e.base, e.size);
#endif
        e.base = nullptr;
        e.handle = nullptr;
        e.size = 0;
    }

    bool fileExists(const std::string &name)
    {
        for (const std::string &dir : paths)
        {
            const std::string file = dir + "/" + name;
#if defined(_WIN32)
            const DWORD attributes = GetFileAttributesA(file.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return true;
#else
            struct stat st;
            if (stat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                return true;
#endif
        }
        return false;
    }

    const uint8_t *mapFile(Table &e, const std::string &name)
    {
        for (const std::string &dir : paths)
        {
            const std::string file = dir + "/" + name;
#if defined(_WIN32)
            HANDLE fd = CreateFileA(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_RANDOM_ACCESS, nullptr);
            if (fd == INVALID_HANDLE_VALUE)
                continue;
            LARGE_INTEGER length;
            if (!GetFileSizeEx(fd, &length) || length.QuadPart == 0)
            {
                CloseHandle(fd);
                continue;
            }
            HANDLE mapping = CreateFileMappingA(fd, nullptr, PAGE_READONLY, 0, 0, nullptr);
            CloseHandle(fd);
            if (mapping == nullptr)
                continue;
            void *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            if (view == nullptr)
            {
                CloseHandle(mapping);
                continue;
            }
            e.base = view;
            e.handle = mapping;
            e.size = static_cast<size_t>(length.QuadPart);
#else
            const int fd = open(file.c_str(), O_RDONLY);
            if (fd == -1)
                continue;
            struct stat st;
            if (fstat(fd, &st) != 0 || st.st_size == 0)
            {
                close(fd);
                continue;
            }
            void *view = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
            close(fd);
            if (view == MAP_FAILED)
                continue;
#if defined(MADV_RANDOM)
            madvise(view, static_cast<size_t>(st.st_size), MADV_RANDOM);
#endif
            e.base = view;
            e.size = static_cast<size_t>(st.st_size);
#endif
            const uint8_t *data = static_cast<const uint8_t *>(e.base);
            const uint8_t *magic = e.type == TABLE_WDL ? WDL_MAGIC : DTZ_MAGIC;
            if (e.size % 64 != 16 || std::memcmp(data, magic, 4) != 0)
            {
                unmap(e);
                return nullptr;
            }
            return data + 4;
        }
        return nullptr;
    }

    int decompressPairs(const PairsData *d, uint64_t idx)
    {
        if (d->flags & FLAG_SINGLE_VALUE)
            return d->minSymLen;

        const size_t k = static_cast<size_t>(idx / d->span);
        uint32_t block = readLE<uint32_t>(d->sparseIndex + 6 * k);
        int64_t offset = readLE<uint16_t>(d->sparseIndex + 6 * k + 4);
        offset += static_cast<int64_t>(idx % d->span) - static_cast<int64_t>(d->span / 2);

        while (offset < 0)
            offset += readLE<uint16_t>(d->blockLength + 2 * static_cast<size_t>(--block)) + 1;
        while (offset > readLE<uint16_t>(d->blockLength + 2 * static_cast<size_t>(block)))
            offset -= readLE<uint16_t>(d->blockLength + 2 * static_cast<size_t>(block++)) + 1;

        const uint8_t *ptr = d->data + static_cast<uint64_t>(block) * d->sizeofBlock;
        uint64_t buf64 = readBE64(ptr);
        ptr += 8;
        int buf64Size = 64;
        int sym;

        while (true)
        {
            size_t len = 0;
            while (buf64 < d->base64[len])
                ++len;
            sym = static_cast<int>((buf64 - d->base64[len]) >> (64 - len - d->minSymLen));
            sym += readLE<uint16_t>(d->lowestSym + 2 * len);
            if (offset < d->symlen[static_cast<size_t>(sym)] + 1)
                break;
            offset -= d->symlen[static_cast<size_t>(sym)] + 1;
            len += d->minSymLen;
            buf64 <<= len;
            buf64Size -= static_cast<int>(len);
            if (buf64Size <= 32)
            {
                buf64Size += 32;
                buf64 |= static_cast<uint64_t>(readBE32(ptr)) << (64 - buf64Size);
                ptr += 4;
            }
        }

        while (d->symlen[static_cast<size_t>(sym)])
        {
            const int l = d->left(sym);
            if (offset < d->symlen[static_cast<size_t>(l)] + 1)
                sym = l;
            else
            {
                offset -= d->symlen[static_cast<size_t>(l)] + 1;
                sym = d->right(sym);
            }
        }

        return d->left(sym);
    }

    void setGroups(Table &e, PairsData *d, const int order[2], int file)
    {
        int n = 0;
        int firstLen = e.hasPawns ? 0 : e.hasUniquePieces ? 3 : 2;
        d->groupLen[n] = 1;

        for (int i = 1; i < e.pieceCount; ++i)
            if (--firstLen > 0 || d->pieces[i] == d->pieces[i - 1])
                d->groupLen[n]++;
            else
                d->groupLen[++n] = 1;

        d->groupLen[++n] = 0;

        const bool pp = e.hasPawns && e.pawnCount[1];
        int next = pp ? 2 : 1;
        int freeSquares = 64 - d->groupLen[0] - (pp ? d->groupLen[1] : 0);
        uint64_t idx = 1;

        for (int k = 0; next < n || k == order[0] || k == order[1]; ++k)
            if (k == order[0])
            {
                d->groupIdx[0] = idx;
                idx *= static_cast<uint64_t>(e.hasPawns ? leadPawnsSize[d->groupLen[0]][file]
                                             : e.hasUniquePieces ? 31332
                                                                 : 462);
            }
            else if (k == order[1])
            {
                d->groupIdx[1] = idx;
                idx *= static_cast<uint64_t>(binomial[d->groupLen[1]][48 - d->groupLen[0]]);
            }
            else
            {
                d->groupIdx[next] = idx;
                idx *= static_cast<uint64_t>(binomial[d->groupLen[next]][freeSquares]);
                freeSquares -= d->groupLen[next++];
            }

        d->groupIdx[n] = idx;
    }

    uint8_t setSymlen(PairsData *d, int s, std::vector<bool> &visited)
    {
        visited[static_cast<size_t>(s)] = true;
        const int sr = d->right(s);
        if (sr == 0xFFF)
            return 0;
        const int sl = d->left(s);
        if (!visited[static_cast<size_t>(sl)])
            d->symlen[static_cast<size_t>(sl)] = setSymlen(d, sl, visited);
        if (!visited[static_cast<size_t>(sr)])
            d->symlen[static_cast<size_t>(sr)] = setSymlen(d, sr, visited);
        return static_cast<uint8_t>(d->symlen[static_cast<size_t>(sl)] + d->symlen[static_cast<size_t>(sr)] + 1);
    }

    const uint8_t *setSizes(PairsData *d, const uint8_t *data)
    {
        d->flags = *data++;
        if (d->flags & FLAG_SINGLE_VALUE)
        {
            d->numBlocks = 0;
            d->span = 0;
            d->blockLengthSize = 0;
            d->sparseIndexSize = 0;
            d->minSymLen = *data++;
            return data;
        }

        const uint64_t tbSize = d->groupIdx[std::find(d->groupLen, d->groupLen + 7, 0) - d->groupLen];

        d->sizeofBlock = static_cast<size_t>(1) << *data++;
        d->span = static_cast<size_t>(1) << *data++;
        d->sparseIndexSize = static_cast<size_t>((tbSize + d->span - 1) / d->span);
        const uint8_t padding = *data++;
        d->numBlocks = readLE<uint32_t>(data);
        data += sizeof(uint32_t);
        d->blockLengthSize = d->numBlocks + padding;
        d->maxSymLen = *data++;
        d->minSymLen = *data++;
        d->lowestSym = data;
        d->base64.assign(static_cast<size_t>(d->maxSymLen - d->minSymLen + 1), 0);

        for (int i = static_cast<int>(d->base64.size()) - 2; i >= 0; --i)
        {
            const size_t u = static_cast<size_t>(i);
            d->base64[u] = (d->base64[u + 1] + readLE<uint16_t>(d->lowestSym + 2 * u)
                            - readLE<uint16_t>(d->lowestSym + 2 * (u + 1)))
                           / 2;
        }

        for (size_t i = 0; i < d->base64.size(); ++i)
            d->base64[i] <<= 64 - i - d->minSymLen;

        data += d->base64.size() * sizeof(uint16_t);
        d->symlen.assign(readLE<uint16_t>(data), 0);
        data += sizeof(uint16_t);
        d->btree = data;

        std::vector<bool> visited(d->symlen.size());
        for (size_t sym = 0; sym < d->symlen.size(); ++sym)
            if (!visited[sym])
                d->symlen[sym] = setSymlen(d, static_cast<int>(sym), visited);

        return data + d->symlen.size() * 3 + (d->symlen.size() & 1);
    }

    const uint8_t *setDtzMap(Table &e, const uint8_t *data, int maxFile)
    {
        if (e.type == TABLE_WDL)
            return data;

        e.map = data;
        for (int f = 0; f <= maxFile; ++f)
        {
            PairsData *d = e.get(0, f);
            if (!(d->flags & FLAG_MAPPED))
                continue;
            if (d->flags & FLAG_WIDE)
            {
                data += reinterpret_cast<uintptr_t>(data) & 1;
                for (int i = 0; i < 4; ++i)
                {
                    d->mapIdx[i] = static_cast<uint16_t>((data - e.map) / 2 + 1);
                    data += 2 * readLE<uint16_t>(data) + 2;
                }
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                {
                    d->mapIdx[i] = static_cast<uint16_t>(data - e.map + 1);
                    data += *data + 1;
                }
            }
        }
        return data + (reinterpret_cast<uintptr_t>(data) & 1);
    }

    void setup(Table &e, const uint8_t *data)
    {
        ++data;

        const int sides = e.type == TABLE_WDL && e.key != e.key2 ? 2 : 1;
        const int maxFile = e.hasPawns ? 3 : 0;
        const bool pp = e.hasPawns && e.pawnCount[1];

        for (int f = 0; f <= maxFile; ++f)
        {
            for (int i = 0; i < sides; ++i)
                *e.get(i, f) = PairsData();

            const int order[2][2] = {{*data & 0xF, pp ? *(data + 1) & 0xF : 0xF},
                                     {*data >> 4, pp ? *(data + 1) >> 4 : 0xF}};
            data += 1 + pp;

            for (int k = 0; k < e.pieceCount; ++k, ++data)
                for (int i = 0; i < sides; ++i)
                    e.get(i, f)->pieces[k] = i ? *data >> 4 : *data & 0xF;

            for (int i = 0; i < sides; ++i)
                setGroups(e, e.get(i, f), order[i], f);
        }

        data += reinterpret_cast<uintptr_t>(data) & 1;

        for (int f = 0; f <= maxFile; ++f)
            for (int i = 0; i < sides; ++i)
                data = setSizes(e.get(i, f), data);

        data = setDtzMap(e, data, maxFile);

        for (int f = 0; f <= maxFile; ++f)
            for (int i = 0; i < sides; ++i)
            {
                PairsData *d = e.get(i, f);
                d->sparseIndex = data;
                data += d->sparseIndexSize * 6;
            }

        for (int f = 0; f <= maxFile; ++f)
            for (int i = 0; i < sides; ++i)
            {
                PairsData *d = e.get(i, f);
                d->blockLength = data;
                data += d->blockLengthSize * sizeof(uint16_t);
            }

        for (int f = 0; f <= maxFile; ++f)
            for (int i = 0; i < sides; ++i)
            {
                data = reinterpret_cast<const uint8_t *>((reinterpret_cast<uintptr_t>(data) + 0x3F) & ~static_cast<uintptr_t>(0x3F));
                PairsData *d = e.get(i, f);
                d->data = data;
                data += d->numBlocks * d->sizeofBlock;
            }
    }

    bool mapped(Table &e)
    {
        if (e.ready.load(std::memory_order_acquire))
            return e.base != nullptr;

        std::lock_guard<std::mutex> lock(mapMutex);
        if (e.ready.load(std::memory_order_relaxed))
            return e.base != nullptr;

        const uint8_t *data = mapFile(e, e.name + (e.type == TABLE_WDL ? ".rtbw" : ".rtbz"));
        if (data != nullptr)
            setup(e, data);

        e.ready.store(true, std::memory_order_release);
        return e.base != nullptr;
    }

    int mapScore(Table &e, int file, int value, int wdl)
    {
        if (e.type == TABLE_WDL)
            return value - 2;

        constexpr int WDL_MAP[] = {1, 3, 0, 2, 0};
        const PairsData *d = e.get(0, file);
        if (d->flags & FLAG_MAPPED)
        {
            const size_t at = static_cast<size_t>(d->mapIdx[WDL_MAP[wdl + 2]] + value);
            value = (d->flags & FLAG_WIDE) ? readLE<uint16_t>(e.map + 2 * at) : e.map[at];
        }

        if ((wdl == syzygy::WDL_WIN && !(d->flags & FLAG_WIN_PLIES))
            || (wdl == syzygy::WDL_LOSS && !(d->flags & FLAG_LOSS_PLIES))
            || wdl == syzygy::WDL_CURSED_WIN || wdl == syzygy::WDL_BLESSED_LOSS)
            value *= 2;

        return value + 1;
    }

    int probeTable(const Position &pos, Table *entry, int wdl, ProbeState &result)
    {
        int squares[TB_PIECES];
        int pieces[TB_PIECES];
        uint64_t idx;
        int next = 0;
        int size = 0;
        int leadPawnsCnt = 0;
        Bitboard b;
        Bitboard leadPawns = 0;
        int tbFile = 0;

        const uint64_t key = positionKey(pos);
        const bool symmetricBlackToMove = entry->key == entry->key2 && pos.sideToMove == BLACK;
        const bool blackStronger = key != entry->key;
        const bool flip = symmetricBlackToMove || blackStronger;
        const int flipColor = flip ? 8 : 0;
        const int flipSquares = flip ? 56 : 0;
        const int stm = (flip ? 1 : 0) ^ static_cast<int>(pos.sideToMove);

        if (entry->hasPawns)
        {
            const int pc = entry->get(0, 0)->pieces[0] ^ flipColor;
            leadPawns = b = pos.byColor[pc >> 3] & pos.byType[PAWN];
            do
            {
                squares[size++] = std::countr_zero(b) ^ flipSquares;
                b &= b - 1;
            } while (b);
            leadPawnsCnt = size;
            std::swap(squares[0], *std::max_element(squares, squares + leadPawnsCnt, pawnsBefore));
            tbFile = std::min(sqFile(squares[0]), 7 - sqFile(squares[0]));
        }

        if (entry->type == TABLE_DTZ)
        {
            const uint8_t flags = entry->get(stm, tbFile)->flags;
            if ((flags & FLAG_STM) != stm && !(entry->key == entry->key2 && !entry->hasPawns))
            {
                result = syzygy::PROBE_CHANGE_STM;
                return 0;
            }
        }

        b = (pos.byColor[WHITE] | pos.byColor[BLACK]) ^ leadPawns;
        do
        {
            const int s = std::countr_zero(b);
            b &= b - 1;
            squares[size] = s ^ flipSquares;
            pieces[size++] = static_cast<int>(pos.board[s]) ^ flipColor;
        } while (b);

        PairsData *d = entry->get(stm, tbFile);

        for (int i = leadPawnsCnt; i < size - 1; ++i)
            for (int j = i + 1; j < size; ++j)
                if (d->pieces[i] == pieces[j])
                {
                    std::swap(pieces[i], pieces[j]);
                    std::swap(squares[i], squares[j]);
                    break;
                }

        if (sqFile(squares[0]) > 3)
            for (int i = 0; i < size; ++i)
                squares[i] ^= 7;

        if (entry->hasPawns)
        {
            idx = static_cast<uint64_t>(leadPawnIdx[leadPawnsCnt][squares[0]]);
            std::stable_sort(squares + 1, squares + leadPawnsCnt, pawnsBefore);
            for (int i = 1; i < leadPawnsCnt; ++i)
                idx += static_cast<uint64_t>(binomial[i][mapPawns[squares[i]]]);
        }
        else
        {
            if (sqRank(squares[0]) > 3)
                for (int i = 0; i < size; ++i)
                    squares[i] ^= 56;

            for (int i = 0; i < d->groupLen[0]; ++i)
            {
                if (!offA1H8(squares[i]))
                    continue;
                if (offA1H8(squares[i]) > 0)
                    for (int j = i; j < size; ++j)
                        squares[j] = ((squares[j] >> 3) | (squares[j] << 3)) & 63;
                break;
            }

            if (entry->hasUniquePieces)
            {
                const int adjust1 = squares[1] > squares[0];
                const int adjust2 = (squares[2] > squares[0]) + (squares[2] > squares[1]);

                if (offA1H8(squares[0]))
                    idx = static_cast<uint64_t>((mapA1D1D4[squares[0]] * 63 + (squares[1] - adjust1)) * 62 + squares[2] - adjust2);
                else if (offA1H8(squares[1]))
                    idx = static_cast<uint64_t>((6 * 63 + sqRank(squares[0]) * 28 + mapB1H1H7[squares[1]]) * 62 + squares[2] - adjust2);
                else if (offA1H8(squares[2]))
                    idx = static_cast<uint64_t>(6 * 63 * 62 + 4 * 28 * 62 + sqRank(squares[0]) * 7 * 28
                                                + (sqRank(squares[1]) - adjust1) * 28 + mapB1H1H7[squares[2]]);
                else
                    idx = static_cast<uint64_t>(6 * 63 * 62 + 4 * 28 * 62 + 4 * 7 * 28 + sqRank(squares[0]) * 7 * 6
                                                + (sqRank(squares[1]) - adjust1) * 6 + (sqRank(squares[2]) - adjust2));
            }
            else
                idx = static_cast<uint64_t>(mapKK[mapA1D1D4[squares[0]]][squares[1]]);
        }

        idx *= d->groupIdx[0];
        int *groupSq = squares + d->groupLen[0];
        bool remainingPawns = entry->hasPawns && entry->pawnCount[1];

        while (d->groupLen[++next])
        {
            std::stable_sort(groupSq, groupSq + d->groupLen[next]);
            uint64_t n = 0;

            for (int i = 0; i < d->groupLen[next]; ++i)
            {
                const int sq = groupSq[i];
                const int adjust = static_cast<int>(std::count_if(squares, groupSq, [sq](int s) { return sq > s; }));
                n += static_cast<uint64_t>(binomial[i + 1][sq - adjust - 8 * remainingPawns]);
            }

            remainingPawns = false;
            idx += n * d->groupIdx[next];
            groupSq += d->groupLen[next];
        }

        return mapScore(*entry, tbFile, decompressPairs(d, idx), wdl);
    }

    int probe(const Position &pos, TableType type, ProbeState &result, int wdl)
    {
        if (std::popcount(pos.byColor[WHITE] | pos.byColor[BLACK]) == 2)
            return syzygy::WDL_DRAW;

        const auto it = tableIndex.find(positionKey(pos));
        if (it == tableIndex.end())
        {
            result = syzygy::PROBE_FAIL;
            return 0;
        }

        Table *entry = type == TABLE_WDL ? it->second.first : it->second.second;
        if (!mapped(*entry))
        {
            result = syzygy::PROBE_FAIL;
            return 0;
        }

        return probeTable(pos, entry, wdl, result);
    }

    bool isCapture(const Position &pos, Move m)
    {
        return m.isEnPassant() || (!m.isCastling() && pos.board[m.to()] != NO_PIECE);
    }

    bool isPawnMove(const Position &pos, Move m)
    {
        return typeOf(pos.board[m.from()]) == PAWN;
    }

    void legalMoves(const Position &pos, MoveList &legal)
    {
        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);
        legal.clear();
        for (int i = 0; i < list.count(); ++i)
            if (movegen::is_legal(pos, list[i]))
                legal.add(list[i]);
    }

    bool inCheck(const Position &pos)
    {
        const Square ksq = pos.kingSquare(pos.sideToMove);
        return ksq != SQ_NONE && movegen::squareAttacked(pos, ksq, static_cast<Color>(pos.sideToMove ^ 1));
    }

    bool isMate(const Position &pos)
    {
        if (!inCheck(pos))
            return false;
        MoveList legal;
        legalMoves(pos, legal);
        return legal.count() == 0;
    }

    int searchWDL(Position &pos, ProbeState &result, bool checkZeroing)
    {
        int bestValue = syzygy::WDL_LOSS;
        MoveList legal;
        legalMoves(pos, legal);
        int moveCount = 0;

        for (int i = 0; i < legal.count(); ++i)
        {
            const Move m = legal[i];
            if (!isCapture(pos, m) && (!checkZeroing || !isPawnMove(pos, m)))
                continue;

            ++moveCount;
            if (!pos.do_move(m))
                continue;
            const int value = -searchWDL(pos, result, false);
            pos.undo_move(m);

            if (result == syzygy::PROBE_FAIL)
                return syzygy::WDL_DRAW;

            if (value > bestValue)
            {
                bestValue = value;
                if (value >= syzygy::WDL_WIN)
                {
                    result = syzygy::PROBE_ZEROING;
                    return value;
                }
            }
        }

        const bool noMoreMoves = moveCount && moveCount == legal.count();
        int value;
        if (noMoreMoves)
            value = bestValue;
        else
        {
            value = probe(pos, TABLE_WDL, result, syzygy::WDL_DRAW);
            if (result == syzygy::PROBE_FAIL)
                return syzygy::WDL_DRAW;
        }

        if (bestValue >= value)
        {
            result = bestValue > syzygy::WDL_DRAW || noMoreMoves ? syzygy::PROBE_ZEROING : syzygy::PROBE_OK;
            return bestValue;
        }

        result = syzygy::PROBE_OK;
        return value;
    }

    void addTable(const std::vector<int> &pieces)
    {
        std::string code;
        for (int pt : pieces)
            code += PIECE_CHARS[pt];
        code.insert(code.find('K', 1), "v");

        if (!fileExists(code + ".rtbw"))
            return;

        int counts[COLOR_NB][PIECE_TYPE_NB] = {};
        int side = WHITE;
        for (char ch : code)
        {
            if (ch == 'v')
            {
                side = BLACK;
                continue;
            }
            const int pt = static_cast<int>(std::strchr(PIECE_CHARS, ch) - PIECE_CHARS);
            if (pt != KING)
                ++counts[side][pt];
        }

        int swapped[COLOR_NB][PIECE_TYPE_NB] = {};
        for (int pt = PAWN; pt < KING; ++pt)
        {
            swapped[WHITE][pt] = counts[BLACK][pt];
            swapped[BLACK][pt] = counts[WHITE][pt];
        }

        Table &wdl = tables.emplace_back();
        wdl.type = TABLE_WDL;
        wdl.name = code;
        wdl.key = materialKey(counts);
        wdl.key2 = materialKey(swapped);
        wdl.pieceCount = static_cast<int>(pieces.size());
        wdl.hasPawns = counts[WHITE][PAWN] + counts[BLACK][PAWN] > 0;
        for (int c = WHITE; c <= BLACK; ++c)
            for (int pt = PAWN; pt < KING; ++pt)
                if (counts[c][pt] == 1)
                    wdl.hasUniquePieces = true;

        const bool whiteLeads = !counts[BLACK][PAWN] || (counts[WHITE][PAWN] && counts[BLACK][PAWN] >= counts[WHITE][PAWN]);
        wdl.pawnCount[0] = whiteLeads ? counts[WHITE][PAWN] : counts[BLACK][PAWN];
        wdl.pawnCount[1] = whiteLeads ? counts[BLACK][PAWN] : counts[WHITE][PAWN];

        Table &dtz = tables.emplace_back();
        dtz.type = TABLE_DTZ;
        dtz.name = wdl.name;
        dtz.key = wdl.key;
        dtz.key2 = wdl.key2;
        dtz.pieceCount = wdl.pieceCount;
        dtz.hasPawns = wdl.hasPawns;
        dtz.hasUniquePieces = wdl.hasUniquePieces;
        dtz.pawnCount[0] = wdl.pawnCount[0];
        dtz.pawnCount[1] = wdl.pawnCount[1];

        tableIndex[wdl.key] = {&wdl, &dtz};
        tableIndex[wdl.key2] = {&wdl, &dtz};
        cardinality = std::max(cardinality, wdl.pieceCount);
    }

    void initEncoding()
    {
        if (encodingReady)
            return;

        int code = 0;
        for (int s = 0; s < 64; ++s)
            if (offA1H8(s) < 0)
                mapB1H1H7[s] = code++;

        std::vector<int> diagonal;
        code = 0;
        for (int s = 0; s <= SQ_D4; ++s)
            if (offA1H8(s) < 0 && sqFile(s) <= 3)
                mapA1D1D4[s] = code++;
            else if (!offA1H8(s) && sqFile(s) <= 3)
                diagonal.push_back(s);
        for (int s : diagonal)
            mapA1D1D4[s] = code++;

        std::vector<std::pair<int, int>> bothOnDiagonal;
        code = 0;
        for (int idx = 0; idx < 10; ++idx)
            for (int s1 = 0; s1 <= SQ_D4; ++s1)
                if (mapA1D1D4[s1] == idx && (idx || s1 == SQ_B1))
                {
                    const Bitboard kingZone = movegen::attacks(KING, static_cast<Square>(s1), 0) | (1ULL << s1);
                    for (int s2 = 0; s2 < 64; ++s2)
                        if (kingZone & (1ULL << s2))
                            continue;
                        else if (!offA1H8(s1) && offA1H8(s2) > 0)
                            continue;
                        else if (!offA1H8(s1) && !offA1H8(s2))
                            bothOnDiagonal.emplace_back(idx, s2);
                        else
                            mapKK[idx][s2] = code++;
                }
        for (const auto &p : bothOnDiagonal)
            mapKK[p.first][p.second] = code++;

        binomial[0][0] = 1;
        for (int n = 1; n < 64; ++n)
            for (int k = 0; k < 6 && k <= n; ++k)
                binomial[k][n] = (k > 0 ? binomial[k - 1][n - 1] : 0) + (k < n ? binomial[k][n - 1] : 0);

        int availableSquares = 47;
        for (int leadPawnsCnt = 1; leadPawnsCnt <= 5; ++leadPawnsCnt)
            for (int f = 0; f <= 3; ++f)
            {
                int idx = 0;
                for (int r = 1; r <= 6; ++r)
                {
                    const int sq = r * 8 + f;
                    if (leadPawnsCnt == 1)
                    {
                        mapPawns[sq] = availableSquares--;
                        mapPawns[sq ^ 7] = availableSquares--;
                    }
                    leadPawnIdx[leadPawnsCnt][sq] = idx;
                    idx += binomial[leadPawnsCnt - 1][mapPawns[sq]];
                }
                leadPawnsSize[leadPawnsCnt][f] = idx;
            }

        encodingReady = true;
    }
}

int syzygy::init(const std::string &pathList)
{
    for (Table &e : tables)
        unmap(e);
    tableIndex.clear();
    tables.clear();
    paths.clear();
    cardinality = 0;

    if (pathList.empty() || pathList == "<empty>")
        return 0;

#if defined(_WIN32)
    constexpr char SEPARATOR = ';';
#else
    constexpr char SEPARATOR = ':';
#endif
    size_t start = 0;
    while (start <= pathList.size())
    {
        const size_t end = std::min(pathList.find(SEPARATOR, start), pathList.size());
        if (end > start)
            paths.push_back(pathList.substr(start, end - start));
        start = end + 1;
    }

    initEncoding();

    for (int p1 = PAWN; p1 < KING; ++p1)
    {
        addTable({KING, p1, KING});
        for (int p2 = PAWN; p2 <= p1; ++p2)
        {
            addTable({KING, p1, p2, KING});
            addTable({KING, p1, KING, p2});
            for (int p3 = PAWN; p3 < KING; ++p3)
                addTable({KING, p1, p2, KING, p3});
            for (int p3 = PAWN; p3 <= p2; ++p3)
            {
                addTable({KING, p1, p2, p3, KING});
                for (int p4 = PAWN; p4 <= p3; ++p4)
                {
                    addTable({KING, p1, p2, p3, p4, KING});
                    for (int p5 = PAWN; p5 <= p4; ++p5)
                        addTable({KING, p1, p2, p3, p4, p5, KING});
                    for (int p5 = PAWN; p5 < KING; ++p5)
                        addTable({KING, p1, p2, p3, p4, KING, p5});
                }
                for (int p4 = PAWN; p4 < KING; ++p4)
                {
                    addTable({KING, p1, p2, p3, KING, p4});
                    for (int p5 = PAWN; p5 <= p4; ++p5)
                        addTable({KING, p1, p2, p3, KING, p4, p5});
                }
            }
            for (int p3 = PAWN; p3 <= p1; ++p3)
                for (int p4 = PAWN; p4 <= (p1 == p3 ? p2 : p3); ++p4)
                    addTable({KING, p1, p2, KING, p3, p4});
        }
    }

    return static_cast<int>(tables.size() / 2);
}

int syzygy::maxCardinality()
{
    return cardinality;
}

syzygy::WDLScore syzygy::probeWDL(Position &pos, ProbeState &result)
{
    result = PROBE_OK;
    return static_cast<WDLScore>(searchWDL(pos, result, false));
}

int syzygy::probeDTZ(Position &pos, ProbeState &result)
{
    result = PROBE_OK;
    const int wdl = searchWDL(pos, result, true);

    if (result == PROBE_FAIL || wdl == WDL_DRAW)
        return 0;

    if (result == PROBE_ZEROING)
        return dtzBeforeZeroing(wdl);

    int dtz = probe(pos, TABLE_DTZ, result, wdl);
    if (result == PROBE_FAIL)
        return 0;

    if (result != PROBE_CHANGE_STM)
        return (dtz + 100 * (wdl == WDL_BLESSED_LOSS || wdl == WDL_CURSED_WIN)) * signOf(wdl);

    int minDTZ = 0xFFFF;
    MoveList legal;
    legalMoves(pos, legal);
    for (int i = 0; i < legal.count(); ++i)
    {
        const Move m = legal[i];
        const bool zeroing = isCapture(pos, m) || isPawnMove(pos, m);
        if (!pos.do_move(m))
            continue;

        if (zeroing)
        {
            ProbeState childResult = PROBE_OK;
            dtz = -dtzBeforeZeroing(searchWDL(pos, childResult, false));
            result = childResult;
        }
        else
            dtz = -probeDTZ(pos, result);

        if (dtz == 1 && isMate(pos))
            minDTZ = 1;

        if (!zeroing)
            dtz += signOf(dtz);

        if (dtz < minDTZ && signOf(dtz) == signOf(wdl))
            minDTZ = dtz;

        pos.undo_move(m);

        if (result == PROBE_FAIL)
            return 0;
    }

    return minDTZ == 0xFFFF ? -1 : minDTZ;
}

bool syzygy::rankRoot(Position &pos, std::vector<Move> &moves, bool &dtzUsed, bool &winning)
{
    std::vector<Move> candidates = moves;
    if (candidates.empty())
    {
        MoveList legal;
        legalMoves(pos, legal);
        for (int i = 0; i < legal.count(); ++i)
            candidates.push_back(legal[i]);
    }
    if (candidates.empty())
        return false;

    std::vector<int> ranks(candidates.size(), 0);
    const int cnt50 = pos.halfmoveClock;
    ProbeState result = PROBE_OK;

    dtzUsed = true;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        const Move m = candidates[i];
        if (!pos.do_move(m))
            return false;

        int dtz;
        if (pos.halfmoveClock == 0)
            dtz = dtzBeforeZeroing(-probeWDL(pos, result));
        else if (pos.halfmoveClock >= 100 || pos.isRepetition(1))
            dtz = 0;
        else
        {
            dtz = -probeDTZ(pos, result);
            dtz = dtz > 0 ? dtz + 1 : dtz < 0 ? dtz - 1 : dtz;
        }

        if (dtz == 2 && isMate(pos))
            dtz = 1;

        pos.undo_move(m);

        if (result == PROBE_FAIL)
        {
            dtzUsed = false;
            break;
        }

        ranks[i] = dtz > 0   ? (dtz + cnt50 <= 99 ? MAX_DTZ : MAX_DTZ - (dtz + cnt50))
                   : dtz < 0 ? (-dtz * 2 + cnt50 < 100 ? -MAX_DTZ : -MAX_DTZ + (-dtz + cnt50))
                             : 0;
    }

    if (!dtzUsed)
    {
        constexpr int WDL_TO_RANK[] = {-MAX_DTZ, -MAX_DTZ + 101, 0, MAX_DTZ - 101, MAX_DTZ};
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const Move m = candidates[i];
            if (!pos.do_move(m))
                return false;

            result = PROBE_OK;
            const int wdl = pos.halfmoveClock >= 100 || pos.isRepetition(1) ? WDL_DRAW : -probeWDL(pos, result);
            pos.undo_move(m);

            if (result == PROBE_FAIL)
                return false;

            ranks[i] = WDL_TO_RANK[wdl + 2];
        }
    }

    const int best = *std::max_element(ranks.begin(), ranks.end());
    winning = best > 0;

    moves.clear();
    for (size_t i = 0; i < candidates.size(); ++i)
        if (ranks[i] == best)
            moves.push_back(candidates[i]);

    return true;
}
