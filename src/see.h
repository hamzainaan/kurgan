#pragma once

#include "movegen.h"

namespace see
{
    // Returns the static exchange evaluation in centipawns from the mover's side.
    int evaluate(const Position &pos, Move m);

    // Checks whether the static exchange evaluation meets a threshold.
    bool ge(const Position &pos, Move m, int threshold);
}
