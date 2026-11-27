/**
 * @file main.cpp
 * @brief 输出基础库构建信息的示例程序。
 */
#include <eve/build_info.hpp>
#include <iostream>

/**
 * @brief 输出当前 Eve 构建的版本和编译器信息。
 * @return 成功输出信息时返回 0。
 */
int main()
{
    const auto info = eve::buildInfo();
    std::cout << "Eve " << info.version << " / " << info.compiler
              << " / " << info.pointerBits << "-bit\n"
              << "Foundation build only; plugin runtime is not implemented yet.\n";
}
