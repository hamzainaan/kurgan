#pragma once

#include "movegen.h"

namespace see
{
    // Static Exchange Evaluation: the net material gain in centipawns of 'm'
    // from the point of view of the side to move. Positive values are good for
    // the mover. A quiet move into an attacked square yields a negative value.
    int evaluate(const Position &pos, Move m);

    // True when evaluate(pos, m) >= threshold. Used for SEE-based pruning.
    bool ge(const Position &pos, Move m, int threshold);
}
