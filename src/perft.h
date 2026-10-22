#pragma once

#include "position.h"

#include <cstdint>

namespace perft
{
    // Counts legal leaf nodes to the given depth.
    uint64_t nodes(Position &pos, int depth);

    // Prints per-root-move node counts and the total.
    void divide(Position &pos, int depth);

    // Runs the built-in perft suite and returns true on success.
    bool suite();
}
