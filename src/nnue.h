#pragma once

#include <cstdint>
#include <string>

class Position;

// KNet v1
namespace nnue
{
    constexpr int KING_BUCKETS = 32;
    constexpr int PIECE_CODES = 10;
    constexpr int KING_PAWN_DIM = 16;
    constexpr int GROUP_A_SIZE = KING_BUCKETS * PIECE_CODES * 64;
    constexpr int GROUP_B_SIZE = KING_BUCKETS * KING_PAWN_DIM;
    constexpr int PERSPECTIVE_SIZE = GROUP_A_SIZE + GROUP_B_SIZE;
    constexpr int INPUT_SIZE = 2 * PERSPECTIVE_SIZE;
    constexpr int HALF_DIM = 256;
    constexpr int L2_DIM = 16;
    constexpr int OUTPUT_BUCKETS = 8;
    constexpr int MAX_MATERIAL = 24;

    constexpr uint8_t MAX_ACTIVATION = 127;

    constexpr bool supported()
    {
#ifdef KURGAN_NNUE
        return true;
#else
        return false;
#endif
    }

    // Load a network file; returns false (and unloads) on any format error.
    bool load(const std::string &path);
    void unload();
    bool loaded();

    // Reason of the last failed load (empty after a successful one).
    const std::string &error();

    // UCI "Use NNUE" switch; a loaded net is used only while this is true.
    void setEnabled(bool value);
    bool enabled();

    // Supported, loaded and enabled.
    bool active();

    const std::string &file();

    // Short identifier of the loaded network (0 when nothing is loaded).
    uint32_t hash();

    // Network evaluation in engine centipawns from the side to move.
    int evaluate(const Position &pos);
}
