#pragma once

#include "types.h"

#include <cstddef>
#include <cstdint>
#include <string>

class Position;

// KNet v3: the architecture Bullet emits. Thirty-two king buckets (rank x
// mirrored file group, i.e. Stockfish's HalfKA_hm bucketing), each perspective
// mirroring the king file, 512 hidden units, an output bucket per four pieces on
// the board and squared clipped ReLU.
namespace nnue
{
    constexpr int KING_BUCKETS = 32;
    constexpr int FEATURES_PER_BUCKET = 768;
    constexpr int INPUT_SIZE = KING_BUCKETS * FEATURES_PER_BUCKET; // 24576
    constexpr int HALF_DIM = 512;
    constexpr int OUTPUT_BUCKETS = 8;

    constexpr int PIECE_STRIDE = 64;
    constexpr int COLOUR_STRIDE = 6 * PIECE_STRIDE; // 384

    // Quantisation of the trainer's export: QA scales the features, QB the head.
    constexpr int QA = 255;
    constexpr int QB = 64;
    constexpr int EVAL_SCALE = 400;
    constexpr int64_t HEAD_DIVISOR = static_cast<int64_t>(QA) * QA * QB;

    // Headerless little-endian int16 payload of one quantised Bullet network.
    constexpr size_t NET_SIZE = static_cast<size_t>(INPUT_SIZE) * HALF_DIM * 2 + HALF_DIM * 2
        + static_cast<size_t>(OUTPUT_BUCKETS) * 2 * HALF_DIM * 2 + OUTPUT_BUCKETS * 2;

    constexpr bool supported()
    {
#ifdef KURGAN_NNUE
        return true;
#else
        return false;
#endif
    }

    bool loadFromMemory(const uint8_t *data, size_t size, const std::string &name);

    bool loadEmbedded();

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

    // Incremental accumulator. A position binds itself with track() once its
    // board is set up and reports every piece change with update(); the values
    // are rebuilt lazily, the first time the position is actually evaluated.
    void track(const Position &pos);
    void update(const Position &pos, Piece piece, Square square, bool add);
}
