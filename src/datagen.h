#pragma once

#include <cstdint>
#include <string>

namespace datagen
{
    struct Options
    {
        int workers = 1;
        uint64_t positions = 10000000;
        int softNodes = 5000; // Ends the move after the iteration that reaches this node count
        int hardNodes = 20000; // Absolute node limit per move
        int randomPlies = 8; // Random opening plies played before recording
        int hash = 16; // Hash size in MB for each worker
        uint64_t seed = 0; // Zero picks a random seed
        std::string out = "data/selfplay"; // Output directory
    };

    // Generates self-play records with the given options.
    void run(const Options &options);
}
