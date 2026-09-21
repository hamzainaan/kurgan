#pragma once

class Position;

namespace evaluate
{
    // Evaluate the position in centipawns from the side to move's perspective.
    // Uses the loaded NNUE when there is one, the HCE otherwise.
    int evaluate(const Position &pos);

    // Classic hand-crafted evaluation
    int hce(const Position &pos);

    // Which of the two evaluate() uses right now?
    const char *name();
}
