#pragma once

class Position;

namespace evaluate
{
    // Evaluate the position in centipawns from the side to move's perspective.
    int evaluate(const Position &pos);
}
