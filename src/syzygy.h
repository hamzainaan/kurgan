#pragma once

#include "movegen.h"
#include "position.h"

#include <string>
#include <vector>

namespace syzygy
{
    enum WDLScore : int
    {
        WDL_LOSS = -2,
        WDL_BLESSED_LOSS = -1,
        WDL_DRAW = 0,
        WDL_CURSED_WIN = 1,
        WDL_WIN = 2
    };

    enum ProbeState : int
    {
        PROBE_CHANGE_STM = -1,
        PROBE_FAIL = 0,
        PROBE_OK = 1,
        PROBE_ZEROING = 2
    };

    int init(const std::string &paths);
    int maxCardinality();

    WDLScore probeWDL(Position &pos, ProbeState &result);
    int probeDTZ(Position &pos, ProbeState &result);

    bool rankRoot(Position &pos, std::vector<Move> &moves, bool &dtzUsed, bool &winning);
}
