#pragma once

#include <cstdint>
#include <string>

namespace datagen
{
    struct Options
    {
        int workers = 1;
        uint64_t positions = 10000000;
        int softNodes = 5000;
        int hardNodes = 20000;
        int randomPlies = 8;
        int hash = 16;
        uint64_t seed = 0;
        std::string out = "data/selfplay";
    };

    void run(const Options &options);
}
