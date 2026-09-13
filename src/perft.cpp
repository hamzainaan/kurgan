#include "perft.h"

#include "movegen.h"

#include <chrono>
#include <iostream>

namespace
{
    // Standard perft positions and their reference node counts, indexed by depth.
    struct TestCase
    {
        const char *fen;
        int maxDepth;
        uint64_t expected[7];
    };

    constexpr TestCase SUITE[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
         5,
         {1, 20, 400, 8902, 197281, 4865609, 119060324}},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
         4,
         {1, 48, 2039, 97862, 4085603, 193690690, 8031647685ULL}},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
         5,
         {1, 14, 191, 2812, 43238, 674624, 11030083}},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
         4,
         {1, 6, 264, 9467, 422333, 15833292, 706045033}},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
         4,
         {1, 44, 1486, 62379, 2103487, 89941194, 3894594319ULL}},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
         4,
         {1, 46, 2079, 89890, 3894594, 164075551, 6923051137ULL}},
    };
}

uint64_t perft::nodes(Position &pos, int depth)
{
    if (depth <= 0)
        return 1;

    MoveList list;
    movegen::generate_pseudo_legal_moves(pos, list);

    uint64_t n = 0;
    for (int i = 0; i < list.size; ++i)
    {
        const Move m = list.moves[i];
        if (!pos.do_move(m))
            continue;
        n += nodes(pos, depth - 1);
        pos.undo_move(m);
    }
    return n;
}

void perft::divide(Position &pos, int depth)
{
    if (depth <= 0)
        return;

    MoveList list;
    movegen::generate_pseudo_legal_moves(pos, list);

    uint64_t total = 0;
    for (int i = 0; i < list.size; ++i)
    {
        const Move m = list.moves[i];
        if (!pos.do_move(m))
            continue;

        const uint64_t n = nodes(pos, depth - 1);
        pos.undo_move(m);

        total += n;
        std::cout << moveToUci(m) << ": " << n << std::endl;
    }
    std::cout << "Total: " << total << std::endl;
}

bool perft::suite()
{
    const auto start = std::chrono::steady_clock::now();
    bool ok = true;
    uint64_t total = 0;

    for (size_t i = 0; i < sizeof(SUITE) / sizeof(SUITE[0]); ++i)
    {
        Position pos;
        pos.set_fen(SUITE[i].fen);

        for (int depth = 1; depth <= SUITE[i].maxDepth; ++depth)
        {
            const uint64_t got = nodes(pos, depth);
            const uint64_t want = SUITE[i].expected[depth];
            const bool pass = got == want;

            total += got;
            ok = ok && pass;

            std::cout << "perft " << (i + 1) << " depth " << depth
                      << " nodes " << got << " expected " << want
                      << (pass ? " ok" : " FAIL") << std::endl;
        }
    }

    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::cout << "perft suite " << (ok ? "passed" : "failed")
              << " nodes " << total << " time " << ms << " ms";
    if (ms > 0)
        std::cout << " nps " << (total * 1000 / static_cast<uint64_t>(ms));
    std::cout << std::endl;

    return ok;
}
