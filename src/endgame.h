#pragma once

#include "types.h"

class Position;

namespace endgame
{
    constexpr int KNOWN_WIN = 10000;
    constexpr int SCALE_NORMAL = 64;

    void init();

    // Checks for a known endgame. When one applies, writes its score to value
    // and returns true.
    bool evaluate(const Position &pos, int &value);

    // Returns the evaluation scale for the stronger side.
    int scale(const Position &pos, Color strong);

    bool probeKPK(Square strongKing, Square strongPawn, Square weakKing, Color stm);
}
