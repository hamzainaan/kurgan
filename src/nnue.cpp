#include "nnue.h"

#include "position.h"

#include <cstring>
#include <fstream>
#include <vector>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
    std::string g_file;
    std::string g_error;
    bool g_enabled = true;
}

#ifndef KURGAN_NNUE

bool nnue::load(const std::string &) { return false; }
void nnue::unload() {}
bool nnue::loaded() { return false; }
const std::string &nnue::error() { return g_error; }
void nnue::setEnabled(bool value) { g_enabled = value; }
bool nnue::enabled() { return g_enabled; }
bool nnue::active() { return false; }
const std::string &nnue::file() { return g_file; }
uint32_t nnue::hash() { return 0; }
int nnue::evaluate(const Position &) { return 0; }

#else

namespace
{
    constexpr uint32_t MAGIC = 0x314E4E4B; // "KNN1"
    constexpr uint32_t VERSION = 1;
    constexpr int HEADER_WORDS = 11;
    constexpr int HEADER_SIZE = HEADER_WORDS * 4 + 4; // padded to 48 bytes
    constexpr int PIECE_CODE_STRIDE = nnue::PIECE_CODES * 64;
    constexpr int BUCKET_STRIDE = nnue::L2_DIM;

    constexpr Bitboard FILE_BB[8] = {
        0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
        0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL};

    struct Network
    {
        uint32_t hash = 0;
        int ftScaleBits = 8;
        int l2ScaleBits = 10;
        int l3ScaleBits = 6;
        std::vector<int16_t> ft;
        std::vector<int16_t> ftBias;
        std::vector<int8_t> l2;
        std::vector<int32_t> l2Bias;
        std::vector<int8_t> l3;
        std::vector<int32_t> l3Bias;
    };

    Network *g_net = nullptr;
    uint32_t g_hash = 0;

    int bitCount(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcountll(b);
#elif defined(_MSC_VER)
        return static_cast<int>(__popcnt64(b));
#else
        int n = 0;
        for (; b; b &= b - 1)
            ++n;
        return n;
#endif
    }

    int bitScan(Bitboard b)
    {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_ctzll(b);
#elif defined(_MSC_VER)
        unsigned long index = 0;
        _BitScanForward64(&index, b);
        return static_cast<int>(index);
#else
        int n = 0;
        for (; (b & 1) == 0; b >>= 1)
            ++n;
        return n;
#endif
    }

    uint32_t fnv1a(const void *data, size_t size)
    {
        const uint8_t *bytes = static_cast<const uint8_t *>(data);
        uint32_t h = 2166136261u;
        for (size_t i = 0; i < size; ++i)
            h = (h ^ bytes[i]) * 16777619u;
        return h;
    }

    int32_t clampI16(int32_t v)
    {
        return v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
    }

    // Arithmetic shift: the trainer's integer forward floors negative sums too.
    int32_t activate(int32_t value, int bits)
    {
        const int32_t v = value >> bits;
        return v < 0 ? 0 : (v > nnue::MAX_ACTIVATION ? nnue::MAX_ACTIVATION : v);
    }

    int kingBucket(Square s)
    {
        const int file = s & 7;
        const int rank = s >> 3;
        return rank * 4 + (file > 3 ? 7 - file : file);
    }

    int pawnCountOnKingFiles(const Position &pos, Color pawns, int kingFile)
    {
        const int file = kingFile > 3 ? 7 - kingFile : kingFile;
        Bitboard files = FILE_BB[file];
        if (file > 0)
            files |= FILE_BB[file - 1];
        if (file < 7)
            files |= FILE_BB[file + 1];
        const int count = bitCount(pos.byColor[pawns] & pos.byType[PAWN] & files);
        return count > 3 ? 3 : count;
    }

    void accumulate(const Network &net, const Position &pos, int32_t acc[COLOR_NB][nnue::HALF_DIM])
    {
        // Perspective 0 is the side to move, perspective 1 the opponent.
        const Color us = pos.sideToMove;
        const Square kingSq[COLOR_NB] = {pos.kingSquare(us), pos.kingSquare(static_cast<Color>(us ^ 1))};
        const int bucket[COLOR_NB] = {
            kingSq[0] == SQ_NONE ? 0 : kingBucket(kingSq[0]),
            kingSq[1] == SQ_NONE ? 0 : kingBucket(kingSq[1]),
        };

        for (int p = 0; p < COLOR_NB; ++p)
            for (int i = 0; i < nnue::HALF_DIM; ++i)
                acc[p][i] = net.ftBias[i];

        for (int pt = PAWN; pt <= QUEEN; ++pt)
        {
            for (int pieceColor = WHITE; pieceColor <= BLACK; ++pieceColor)
            {
                Bitboard b = pos.byColor[pieceColor] & pos.byType[pt];
                while (b)
                {
                    const int sq = bitScan(b);
                    b &= b - 1;
                    for (int p = 0; p < COLOR_NB; ++p)
                    {
                        const int code = pt + (pieceColor != (us ^ p) ? 5 : 0);
                        const int index = p * nnue::PERSPECTIVE_SIZE + bucket[p] * PIECE_CODE_STRIDE + code * 64 + sq;
                        const int16_t *row = net.ft.data() + static_cast<size_t>(index) * nnue::HALF_DIM;
                        int32_t *a = acc[p];
                        for (int i = 0; i < nnue::HALF_DIM; ++i)
                            a[i] += row[i];
                    }
                }
            }
        }

        for (int p = 0; p < COLOR_NB; ++p)
        {
            const Color ownColor = static_cast<Color>(us ^ p);
            const int own = pawnCountOnKingFiles(pos, ownColor, kingSq[p] & 7);
            const int them = pawnCountOnKingFiles(pos, static_cast<Color>(ownColor ^ 1), kingSq[p] & 7);
            const int index = p * nnue::PERSPECTIVE_SIZE + nnue::GROUP_A_SIZE + bucket[p] * nnue::KING_PAWN_DIM
                + own * 4 + them;
            const int16_t *row = net.ft.data() + static_cast<size_t>(index) * nnue::HALF_DIM;
            for (int i = 0; i < nnue::HALF_DIM; ++i)
                acc[p][i] += row[i];
        }

        for (int p = 0; p < COLOR_NB; ++p)
            for (int i = 0; i < nnue::HALF_DIM; ++i)
                acc[p][i] = clampI16(acc[p][i]);
    }

    int outputBucket(const Position &pos)
    {
        const int material = bitCount(pos.byType[KNIGHT] | pos.byType[BISHOP]) + 2 * bitCount(pos.byType[ROOK])
            + 4 * bitCount(pos.byType[QUEEN]);
        const int bucket = material * nnue::OUTPUT_BUCKETS / (nnue::MAX_MATERIAL + 1);
        return bucket < nnue::OUTPUT_BUCKETS ? bucket : nnue::OUTPUT_BUCKETS - 1;
    }

    bool readExact(std::ifstream &in, void *dst, size_t size)
    {
        in.read(static_cast<char *>(dst), static_cast<std::streamsize>(size));
        return static_cast<size_t>(in.gcount()) == size;
    }
}

bool nnue::load(const std::string &path)
{
    unload();

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        g_error = "cannot open " + path;
        return false;
    }

    uint32_t header[HEADER_WORDS + 1] = {};
    if (!readExact(in, header, HEADER_SIZE))
    {
        g_error = "truncated header";
        return false;
    }

    if (header[0] != MAGIC)
    {
        g_error = "bad magic (not a Kurgan NNUE file)";
        return false;
    }
    if (header[1] != VERSION)
    {
        g_error = "unsupported version " + std::to_string(header[1]);
        return false;
    }
    if (header[2] != INPUT_SIZE || header[3] != HALF_DIM || header[4] != L2_DIM || header[5] != OUTPUT_BUCKETS)
    {
        g_error = "architecture mismatch";
        return false;
    }

    Network net;
    net.ftScaleBits = static_cast<int>(header[6]);
    net.l2ScaleBits = static_cast<int>(header[7]);
    net.l3ScaleBits = static_cast<int>(header[8]);
    if (net.ftScaleBits < 1 || net.ftScaleBits > 14 || net.l2ScaleBits < 1 || net.l2ScaleBits > 14
        || net.l3ScaleBits < 1 || net.l3ScaleBits > 14)
    {
        g_error = "invalid quantization scales";
        return false;
    }

    net.ftBias.resize(HALF_DIM);
    net.ft.resize(static_cast<size_t>(INPUT_SIZE) * HALF_DIM);
    net.l2Bias.resize(OUTPUT_BUCKETS * L2_DIM);
    net.l2.resize(static_cast<size_t>(OUTPUT_BUCKETS) * 2 * HALF_DIM * L2_DIM);
    net.l3Bias.resize(OUTPUT_BUCKETS);
    net.l3.resize(OUTPUT_BUCKETS * L2_DIM);

    const size_t payload = net.ftBias.size() * sizeof(int16_t) + net.ft.size() * sizeof(int16_t)
        + net.l2Bias.size() * sizeof(int32_t) + net.l2.size() * sizeof(int8_t) + net.l3Bias.size() * sizeof(int32_t)
        + net.l3.size() * sizeof(int8_t);

    std::vector<uint8_t> buffer(payload);
    if (!readExact(in, buffer.data(), payload))
    {
        g_error = "truncated payload";
        return false;
    }
    if (fnv1a(buffer.data(), payload) != header[9])
    {
        g_error = "checksum mismatch";
        return false;
    }

    size_t offset = 0;
    const auto take = [&](void *dst, size_t bytes)
    {
        std::memcpy(dst, buffer.data() + offset, bytes);
        offset += bytes;
    };
    take(net.ftBias.data(), net.ftBias.size() * sizeof(int16_t));
    take(net.ft.data(), net.ft.size() * sizeof(int16_t));
    take(net.l2Bias.data(), net.l2Bias.size() * sizeof(int32_t));
    take(net.l2.data(), net.l2.size() * sizeof(int8_t));
    take(net.l3Bias.data(), net.l3Bias.size() * sizeof(int32_t));
    take(net.l3.data(), net.l3.size() * sizeof(int8_t));

    net.hash = header[9];
    g_net = new Network(net);
    g_file = path;
    g_hash = net.hash;
    g_error.clear();
    return true;
}

void nnue::unload()
{
    delete g_net;
    g_net = nullptr;
    g_file.clear();
    g_hash = 0;
}

bool nnue::loaded() { return g_net != nullptr; }
const std::string &nnue::error() { return g_error; }

void nnue::setEnabled(bool value) { g_enabled = value; }

bool nnue::enabled() { return g_enabled; }

bool nnue::active() { return g_net != nullptr && g_enabled; }

const std::string &nnue::file() { return g_file; }

uint32_t nnue::hash() { return g_hash; }

int nnue::evaluate(const Position &pos)
{
    const Network &net = *g_net;

    int32_t acc[COLOR_NB][HALF_DIM];
    accumulate(net, pos, acc);

    int8_t act[2 * HALF_DIM];
    for (int i = 0; i < HALF_DIM; ++i)
    {
        act[i] = static_cast<int8_t>(activate(acc[0][i], net.ftScaleBits));
        act[HALF_DIM + i] = static_cast<int8_t>(activate(acc[1][i], net.ftScaleBits));
    }

    const int bucket = outputBucket(pos);
    const int8_t *l2Weights = net.l2.data() + static_cast<size_t>(bucket) * 2 * HALF_DIM * L2_DIM;
    const int32_t *l2Biases = net.l2Bias.data() + static_cast<size_t>(bucket) * BUCKET_STRIDE;

    int32_t l2Acc[L2_DIM];
    for (int j = 0; j < L2_DIM; ++j)
        l2Acc[j] = l2Biases[j];

    for (int i = 0; i < 2 * HALF_DIM; ++i)
    {
        const int a = act[i];
        if (a == 0)
            continue;
        const int8_t *row = l2Weights + static_cast<size_t>(i) * L2_DIM;
        for (int j = 0; j < L2_DIM; ++j)
            l2Acc[j] += a * row[j];
    }

    int32_t out = net.l3Bias[bucket];
    const int8_t *l3Weights = net.l3.data() + static_cast<size_t>(bucket) * L2_DIM;
    for (int j = 0; j < L2_DIM; ++j)
        out += activate(l2Acc[j], net.l2ScaleBits) * l3Weights[j];

    return static_cast<int>(out >> net.l3ScaleBits);
}

#endif
