#include "datagen.h"

#include "movegen.h"
#include "nnue.h"
#include "position.h"
#include "search.h"
#include "tt.h"

#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
    constexpr const char *START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    constexpr int OPENING_MAX_SCORE = 500;
    constexpr int WIN_SCORE = 2500;
    constexpr int WIN_PLIES = 4;
    constexpr int DRAW_SCORE = 10;
    constexpr int DRAW_PLIES = 10;
    constexpr int DRAW_MIN_PLY = 80;
    constexpr int MAX_GAME_PLIES = 400;
    constexpr int REPORT_SECONDS = 10;

    struct Record
    {
        uint64_t occ = 0;
        uint8_t pcs[16] = {};
        int16_t score = 0;
        uint8_t result = 1;
        uint8_t ksq = 0;
        uint8_t oppKsq = 0;
        uint8_t extra[3] = {};
    };

    static_assert(sizeof(Record) == 32, "a bulletformat record is 32 bytes");

    using Clock = std::chrono::steady_clock;

    Record encode(const Position &pos, int score)
    {
        Record r;
        const Color us = pos.sideToMove;
        const Color them = static_cast<Color>(us ^ 1);
        const int flip = us == BLACK ? 56 : 0;

        int index = 0;
        for (int square = 0; square < SQUARE_NB; ++square)
        {
            const Piece piece = pos.board[square ^ flip];
            if (piece == NO_PIECE)
                continue;

            const int code = (colorOf(piece) == us ? 0 : 8) | static_cast<int>(typeOf(piece));
            r.occ |= 1ULL << square;
            r.pcs[index / 2] |= static_cast<uint8_t>(code << (4 * (index & 1)));
            ++index;
        }

        r.score = static_cast<int16_t>(score);
        r.ksq = static_cast<uint8_t>(pos.kingSquare(us) ^ flip);
        r.oppKsq = static_cast<uint8_t>(pos.kingSquare(them) ^ flip ^ 56);
        return r;
    }

    std::vector<Move> legalMoves(const Position &pos)
    {
        MoveList list;
        movegen::generate_pseudo_legal_moves(pos, list);

        std::vector<Move> moves;
        for (int i = 0; i < list.size; ++i)
            if (movegen::is_legal(pos, list.moves[i]))
                moves.push_back(list.moves[i]);
        return moves;
    }

    bool inCheck(const Position &pos)
    {
        const Square ksq = pos.kingSquare(pos.sideToMove);
        return ksq != SQ_NONE && movegen::squareAttacked(pos, ksq, static_cast<Color>(pos.sideToMove ^ 1));
    }

    bool isNoisy(const Position &pos, Move m)
    {
        return m.isPromotion() || m.isEnPassant() || (!m.isCastling() && pos.board[m.to()] != NO_PIECE);
    }

    bool bareMinors(const Position &pos)
    {
        if (pos.byType[PAWN] | pos.byType[ROOK] | pos.byType[QUEEN])
            return false;
        return std::bitset<64>(pos.byType[KNIGHT] | pos.byType[BISHOP]).count() <= 1;
    }

    Move think(const Position &pos, const search::SearchLimits &limits, int &score)
    {
        search::prepare(pos, limits);
        search::go(pos, limits);
        score = search::bestScore();
        return search::bestMove();
    }

    bool playGame(std::mt19937_64 &rng, const datagen::Options &options, std::vector<Record> &records)
    {
        records.clear();
        search::clear();

        Position pos;
        pos.set_fen(START_FEN);

        const int randomPlies = options.randomPlies + static_cast<int>(rng() & 1);
        for (int i = 0; i < randomPlies; ++i)
        {
            const std::vector<Move> moves = legalMoves(pos);
            if (moves.empty())
                return false;
            pos.do_move(moves[rng() % moves.size()]);
        }
        if (legalMoves(pos).empty())
            return false;

        search::SearchLimits limits;
        limits.softNodes = options.softNodes;
        limits.nodes = options.hardNodes;

        int score = 0;
        think(pos, limits, score);
        if (std::abs(score) > OPENING_MAX_SCORE)
            return false;

        std::vector<Color> sides;
        int result = 1;
        int winRun = 0;
        int drawRun = 0;

        for (int ply = randomPlies;; ++ply)
        {
            const Color us = pos.sideToMove;
            const bool checked = inCheck(pos);

            if (legalMoves(pos).empty())
            {
                result = !checked ? 1 : (us == WHITE ? 0 : 2);
                break;
            }
            if (pos.halfmoveClock >= 100 || pos.isRepetition(0) || bareMinors(pos) || ply >= MAX_GAME_PLIES)
            {
                result = 1;
                break;
            }

            const Move m = think(pos, limits, score);
            if (score >= tt::MATE_THRESHOLD || score <= -tt::MATE_THRESHOLD)
            {
                result = (score > 0) == (us == WHITE) ? 2 : 0;
                break;
            }

            if (!checked && !isNoisy(pos, m))
            {
                records.push_back(encode(pos, score));
                sides.push_back(us);
            }

            const int white = us == WHITE ? score : -score;
            if (std::abs(white) >= WIN_SCORE)
                winRun = white > 0 ? std::max(winRun, 0) + 1 : std::min(winRun, 0) - 1;
            else
                winRun = 0;
            if (std::abs(winRun) >= WIN_PLIES)
            {
                result = winRun > 0 ? 2 : 0;
                break;
            }

            drawRun = ply >= DRAW_MIN_PLY && std::abs(score) <= DRAW_SCORE ? drawRun + 1 : 0;
            if (drawRun >= DRAW_PLIES)
            {
                result = 1;
                break;
            }

            pos.do_move(m);
        }

        for (size_t i = 0; i < records.size(); ++i)
            records[i].result = static_cast<uint8_t>(sides[i] == WHITE ? result : 2 - result);
        return true;
    }

    uint64_t recordsIn(const std::vector<std::string> &paths)
    {
        uint64_t total = 0;
        for (const std::string &path : paths)
        {
            std::error_code ec;
            const uintmax_t size = std::filesystem::file_size(path, ec);
            if (!ec)
                total += static_cast<uint64_t>(size / sizeof(Record));
        }
        return total;
    }

    void alignFile(const std::string &path)
    {
        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size % sizeof(Record) != 0)
            std::filesystem::resize_file(path, size - size % sizeof(Record), ec);
    }

    std::string duration(int64_t seconds)
    {
        std::ostringstream out;
        out << seconds / 3600 << "h" << (seconds / 60) % 60 << "m" << seconds % 60 << "s";
        return out.str();
    }

    void report(uint64_t done, uint64_t target, Clock::time_point start)
    {
        const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
        const double rate = elapsed > 0.0 ? static_cast<double>(done) / elapsed : 0.0;
        const uint64_t left = done < target ? target - done : 0;
        std::cout << "info string datagen positions " << done << '/' << target
                  << " (" << (target > 0 ? 100 * done / target : 100) << "%)"
                  << " rate " << static_cast<uint64_t>(rate) << " pos/s"
                  << " elapsed " << duration(static_cast<int64_t>(elapsed))
                  << " eta " << (rate > 0.0 ? duration(static_cast<int64_t>(static_cast<double>(left) / rate)) : "-")
                  << std::endl;
    }

    void work(const datagen::Options &options, uint64_t target, uint64_t seed, const std::string &path, bool verbose)
    {
        search::setSilent(true);
        search::setThreads(1);
        search::setMultiPV(1);
        search::setContempt(0);
        search::setHashSize(options.hash);

        alignFile(path);
        std::FILE *file = std::fopen(path.c_str(), "ab");
        if (file == nullptr)
        {
            std::cerr << "info string datagen cannot open " << path << std::endl;
            return;
        }

        std::mt19937_64 rng(seed);
        std::vector<Record> records;
        uint64_t written = 0;
        const Clock::time_point start = Clock::now();
        Clock::time_point last = start;

        while (written < target)
        {
            if (!playGame(rng, options, records) || records.empty())
                continue;

            std::fwrite(records.data(), sizeof(Record), records.size(), file);
            std::fflush(file);
            written += records.size();

            if (verbose && Clock::now() - last >= std::chrono::seconds(REPORT_SECONDS))
            {
                last = Clock::now();
                report(written, target, start);
            }
        }

        std::fclose(file);
    }
}

void datagen::run(const Options &requested)
{
    Options options = requested;
    options.workers = std::clamp(options.workers, 1, search::maxThreadCount());
    options.softNodes = std::max(options.softNodes, 1);
    options.hardNodes = std::max(options.hardNodes, options.softNodes);
    options.randomPlies = std::max(options.randomPlies, 0);
    if (options.seed == 0)
        options.seed = (static_cast<uint64_t>(std::random_device{}()) << 32)
                       ^ static_cast<uint64_t>(Clock::now().time_since_epoch().count());

#if defined(_WIN32)
    options.workers = 1;
#endif

    std::error_code ec;
    std::filesystem::create_directories(options.out, ec);
    if (ec)
    {
        std::cout << "info string datagen cannot create " << options.out << ": " << ec.message() << std::endl;
        return;
    }

    char tag[16];
    if (nnue::active())
        std::snprintf(tag, sizeof(tag), "%08x", static_cast<unsigned>(nnue::hash()));
    else
        std::snprintf(tag, sizeof(tag), "hce");

    std::vector<std::string> paths;
    std::vector<uint64_t> seeds;
    for (int i = 0; i < options.workers; ++i)
    {
        const uint64_t seed = options.seed + 0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(i + 1);
        paths.push_back((std::filesystem::path(options.out)
                         / (std::string(tag) + "-" + std::to_string(options.seed) + "-" + std::to_string(i) + ".data"))
                            .string());
        seeds.push_back(seed);
    }

    const uint64_t perWorker = (options.positions + static_cast<uint64_t>(options.workers) - 1)
                               / static_cast<uint64_t>(options.workers);
    const uint64_t target = perWorker * static_cast<uint64_t>(options.workers);
    const uint64_t base = recordsIn(paths);

    std::cout << "info string datagen net " << tag << " workers " << options.workers
              << " positions " << target << " softnodes " << options.softNodes
              << " hardnodes " << options.hardNodes << " randomplies " << options.randomPlies
              << " hash " << options.hash << " seed " << options.seed
              << " out " << options.out << std::endl;

    const Clock::time_point start = Clock::now();

#if defined(_WIN32)
    const int savedThreads = search::threadSetting();
    const int savedHash = search::hashSize();
    work(options, perWorker, seeds[0], paths[0], true);
    search::setSilent(false);
    search::setThreads(savedThreads);
    search::setHashSize(savedHash);
#else
    std::cout.flush();
    std::vector<pid_t> children;
    for (int i = 0; i < options.workers; ++i)
    {
        const pid_t pid = fork();
        if (pid == 0)
        {
            work(options, perWorker, seeds[static_cast<size_t>(i)], paths[static_cast<size_t>(i)], false);
            _exit(0);
        }
        if (pid < 0)
        {
            std::cout << "info string datagen fork failed after " << i << " workers" << std::endl;
            break;
        }
        children.push_back(pid);
    }

    Clock::time_point last = start;
    size_t running = children.size();
    while (running > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (pid_t &pid : children)
        {
            int status = 0;
            if (pid > 0 && waitpid(pid, &status, WNOHANG) == pid)
            {
                pid = 0;
                --running;
            }
        }

        if (Clock::now() - last >= std::chrono::seconds(REPORT_SECONDS))
        {
            last = Clock::now();
            report(recordsIn(paths) - base, target, start);
        }
    }
#endif

    report(recordsIn(paths) - base, target, start);
    std::cout << "info string datagen done";
    for (const std::string &path : paths)
        std::cout << ' ' << path;
    std::cout << std::endl;
}
