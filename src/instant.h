#pragma once

#include "movegen.h"
#include "position.h"

namespace instant
{
    enum class Kind
    {
        NONE,
        FORCED,
        RECAPTURE
    };

    void setEnabled(bool enabled);
    bool enabled();

    void classify(const Position &root, Move previousMove, PieceType previousVictim, bool restricted);
    Kind kind();
    bool isCandidate(Move m);
}
