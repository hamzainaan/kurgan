#pragma once

namespace bench
{
    // Run a deterministic fixed-depth search over a built-in position set.
    // 'depth' <= 0 selects the default depth.
    void run(int depth);
}
