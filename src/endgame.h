#pragma once

#include "types.h"

class Position;

namespace endgame
{
    // Bonus that lifts a known win above any ordinary evaluation
    constexpr int KNOWN_WIN = 10000;
    // Scale factor that leaves the evaluation unchanged
    constexpr int SCALE_NORMAL = 64;

    // Builds the KPK bitbase once
    void init();

    // Checks for a known endgame. When one applies, writes its score to value
    // and returns true.
    bool evaluate(const Position &pos, int &value);

    // Returns the evaluation scale for the stronger side.
    int scale(const Position &pos, Color strong);

    // Probes the KPK bitbase with squares normalized so the strong side is white
    bool probeKPK(Square strongKing, Square strongPawn, Square weakKing, Color stm);
}
