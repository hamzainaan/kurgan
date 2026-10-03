#pragma once

class Position;

namespace endgame
{
    void init();
    int adjust(const Position &pos, int eval);
}
