#pragma once

class Position;

namespace evaluate
{
    // Evaluates the position in centipawns from the side to move, using NNUE
    // when it is active and the hand-crafted evaluation otherwise.
    int evaluate(const Position &pos);

    // Runs the hand-crafted evaluation.
    int hce(const Position &pos);

    // Returns the name of the active evaluator.
    const char *name();
}
