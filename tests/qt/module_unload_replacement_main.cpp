/** @file module_unload_replacement_main.cpp
 * @brief 实际替换终结后的物理回收组合；不另造事务或模拟模块角色。
 */
#include "module_unload_replacement_p.hpp"
#include <iostream>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        const auto args = app.arguments();
        CHECK(args.size() == 4);
        unload_test::Scene scene(args[2], args[3].split(';'));
        const auto name = args[1];
        CHECK(unload_test::replacementCases(scene, name) || unload_test::configuredReplacementCases(scene, name) ||
              unload_test::concurrentOwners(scene, name));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
