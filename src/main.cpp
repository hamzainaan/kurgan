#include <iostream>

#include "uci.h"
#include "version.h"

int main()
{
    std::cout << "Kurgan " << KURGAN_VERSION << " by Hamza Inan (build " << KURGAN_BUILD << ")"
              << std::endl;

    uci::loop();
    return 0;
}