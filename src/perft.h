#pragma once

#include "position.h"

#include <cstdint>

namespace perft
{
    // Count leaf nodes of the legal move tree to the given depth.
    uint64_t nodes(Position &pos, int depth);

    // Print per-root-move node counts followed by the total.
    void divide(Position &pos, int depth);

    // Run the built-in perft suite against known values; returns true on success.
    bool suite();
}
