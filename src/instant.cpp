#include "instant.h"

#include "see.h"

namespace
{
    bool enabledSetting = true;
    instant::Kind rootKind = instant::Kind::NONE;
    MoveList candidates;

    void collectLegalMoves(const Position &root, MoveList &legal)
    {
        MoveList list;
        movegen::generatePseudoLegalMoves(root, list);
        for (int i = 0; i < list.count(); ++i)
            if (movegen::isLegal(root, list[i]))
                legal.add(list[i]);
    }

    bool isRecapture(const Position &root, Move m, Square target)
    {
        return m.to() == target && !m.isCastling() && !m.isEnPassant() && see::ge(root, m, 0);
    }
}

namespace instant
{
    void setEnabled(bool enabled)
    {
        enabledSetting = enabled;
    }

    bool enabled()
    {
        return enabledSetting;
    }

    void classify(const Position &root, Move previousMove, PieceType previousVictim, bool restricted)
    {
        rootKind = Kind::NONE;
        candidates.clear();

        if (!enabledSetting || restricted)
            return;

        MoveList legal;
        collectLegalMoves(root, legal);

        if (legal.count() == 1)
        {
            rootKind = Kind::FORCED;
            candidates.add(legal[0]);
            return;
        }

        if (previousMove == Move() || previousVictim == NO_PIECE_TYPE)
            return;

        const Square target = previousMove.to();
        const Piece occupant = root.board[target];
        if (occupant == NO_PIECE || colorOf(occupant) == root.sideToMove)
            return;

        for (int i = 0; i < legal.count(); ++i)
            if (isRecapture(root, legal[i], target))
                candidates.add(legal[i]);

        if (candidates.count() > 0)
            rootKind = Kind::RECAPTURE;
    }

    Kind kind()
    {
        return rootKind;
    }

    bool isCandidate(Move m)
    {
        for (int i = 0; i < candidates.count(); ++i)
            if (candidates[i] == m)
                return true;
        return false;
    }
}
