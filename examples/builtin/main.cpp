#include <cordis/build_info.hpp>
#include <iostream>

int main()
{
    const auto info = cordis::buildInfo();
    std::cout << "CordisCpp " << info.version << " / " << info.compiler
              << " / " << info.pointerBits << "-bit\n"
              << "Foundation build only; plugin runtime is not implemented yet.\n";
}
