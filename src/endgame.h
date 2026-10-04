#pragma once

#include "types.h"

class Position;

namespace endgame
{
    constexpr int KNOWN_WIN = 10000;
    constexpr int SCALE_NORMAL = 64;

    void init();

    bool evaluate(const Position &pos, int &value);

    int scale(const Position &pos, Color strong);

    bool probeKPK(Square strongKing, Square strongPawn, Square weakKing, Color stm);
}
