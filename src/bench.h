#pragma once

namespace bench
{
    // Runs a deterministic fixed-depth search over built-in positions. A depth
    // of zero or less selects the default.
    void run(int depth);
}
