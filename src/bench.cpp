#include "bench.h"

#include "movegen.h"
#include "position.h"
#include "search.h"

#include <chrono>
#include <cstdint>
#include <iostream>

namespace
{
    constexpr int DEFAULT_DEPTH = 12;
    constexpr int BENCH_HASH_MB = 64;

    constexpr const char *POSITIONS[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "r1b2rk1/1ppqn1bp/p2p1np1/3Pp3/2P1P3/2N2N1P/PP1QBPP1/R3K2R w KQ - 2 12",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "r1bqkb1r/pp2pppp/2p2n2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R b KQkq - 0 4",
        "2r3k1/1q1nbppp/r3p3/3pP3/pPpP4/P1Q2N2/2RN1PPP/2R3K1 b - - 0 23",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "8/8/8/2k5/2pP4/8/B7/4K3 b - d3 0 3",
        "4k3/8/8/8/8/8/4P3/4K3 w - - 5 39",
        "8/8/4k3/8/8/4K3/8/R7 w - - 0 1",
    };
}

void bench::run(int depth)
{
    if (depth <= 0)
        depth = DEFAULT_DEPTH;

    const int savedThreads = search::threadSetting();
    const int savedHash = search::hashSize();

    search::setThreads(1);
    search::setHashSize(BENCH_HASH_MB);
    search::setSilent(true);

    const auto start = std::chrono::steady_clock::now();
    uint64_t totalNodes = 0;

    const int count = static_cast<int>(sizeof(POSITIONS) / sizeof(POSITIONS[0]));
    for (int i = 0; i < count; ++i)
    {
        Position pos;
        pos.set_fen(POSITIONS[i]);

        search::clear();

        search::SearchLimits limits;
        limits.depth = depth;
        search::go(pos, limits);

        const uint64_t n = search::totalNodes();
        totalNodes += n;

        std::cout << "info string bench " << (i + 1) << '/' << count
                  << " fen " << POSITIONS[i]
                  << " bestmove " << moveToUci(search::bestMove())
                  << " nodes " << n << std::endl;
    }

    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    search::setSilent(false);
    search::setThreads(savedThreads);
    search::setHashSize(savedHash);

    std::cout << "Nodes searched: " << totalNodes << std::endl;
    std::cout << "Time: " << ms << " ms" << std::endl;
    std::cout << "NPS: " << (ms > 0 ? totalNodes * 1000 / static_cast<uint64_t>(ms) : 0) << std::endl;
}