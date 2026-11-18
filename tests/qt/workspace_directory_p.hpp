/** @file workspace_directory_p.hpp
 * @brief 两个真实工作台测试共享的目录纯值副本；不保留代次、工厂、作用域或回调。
 */
#pragma once
#include "../../src/module/instance_origin_p.hpp"
#include <QJsonArray>
#include <cordis/module/manager.hpp>
#include <cstddef>
#include <optional>
#include <vector>

namespace workspace_directory_test {
struct GenerationValues {
    cordis::module::ModuleGenerationSnapshot module;
    std::size_t admissions = 0;
    bool loading = false, image = false, observationImage = false, artifactFile = false, backend = false;
    std::optional<cordis::OperationId> reservation;
    std::vector<cordis::module::ModuleRuntimeOrigin> runtimes;
    std::vector<cordis::module::detail::InstanceOrigin> instances;
};
struct DirectoryValues {
    std::vector<GenerationValues> generations;
    QJsonArray reservations, readers;
    std::size_t readerOperations = 0;
};
/** @brief 原 owner 线程复制真实目录；所有锁和临时代次引用在返回前释放，不推进事件或调用插件。 */
DirectoryValues directory();
} // namespace workspace_directory_test
