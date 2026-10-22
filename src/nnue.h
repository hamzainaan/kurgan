#pragma once

#include "types.h"

#include <cstddef>
#include <cstdint>
#include <string>

class Position;

// KNet v3 uses 32 king buckets with mirrored files, a 512-wide SCReLU hidden
// layer and eight output buckets selected by piece count.
namespace nnue
{
    constexpr int KING_BUCKETS = 32;
    constexpr int FEATURES_PER_BUCKET = 768;
    constexpr int INPUT_SIZE = KING_BUCKETS * FEATURES_PER_BUCKET;
    constexpr int HALF_DIM = 512;
    constexpr int OUTPUT_BUCKETS = 8;

    constexpr int PIECE_STRIDE = 64;
    constexpr int COLOUR_STRIDE = 6 * PIECE_STRIDE;

    // QA scales the features and QB scales the output head.
    constexpr int QA = 255;
    constexpr int QB = 64;
    constexpr int EVAL_SCALE = 400;
    constexpr int64_t HEAD_DIVISOR = static_cast<int64_t>(QA) * QA * QB;

    // Byte size of a headerless little-endian int16 Bullet network.
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

    // Reason for the last failed load, empty after a successful one.
    const std::string &error();

    // Enables use of the loaded network.
    void setEnabled(bool value);
    bool enabled();

    // Checks whether NNUE is supported, loaded and enabled.
    bool active();

    const std::string &file();

    // Returns a short identifier of the loaded network, or 0 when none is loaded.
    uint32_t hash();

    // Evaluates the position in engine centipawns from the side to move.
    int evaluate(const Position &pos);
}
