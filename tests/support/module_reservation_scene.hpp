/** @file module_reservation_scene.hpp
 * @brief 预约及状态树测试共用同一个真实模块目录、管理器和双版本产物。
 */
#pragma once
#include "module_artifacts.hpp"
#include <QStringList>
#include <algorithm>
#include <eve/module/manager.hpp>
#ifdef EVE_TEST_QT_RESERVATION
#include <eve/module/qt_module.hpp>
#endif
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#ifndef CHECK
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)
#endif

struct Scene {
    std::shared_ptr<eve::module::ModuleManager> managerOwner = std::make_shared<eve::module::ModuleManager>();
    eve::module::ModuleManager& manager = *managerOwner;
    bool qt;
    QString root;
    QStringList dlls;
    std::string oldPath, nextPath;
    eve::GenerationId current, candidate;
    Scene(bool backend, QString directory, QStringList paths)
        : qt(backend), root(std::move(directory)), dlls(std::move(paths))
    {
        CHECK(dlls.size() == 5);
        oldPath = install(dlls[0], "old").toStdString();
        nextPath = install(dlls[1], "next", "2.0.0").toStdString();
        current = load(oldPath).value().generation;
        candidate = stage(nextPath).value().generation;
    }
    QString install(const QString& dll, const QString& dir, const QString& version = "1.0.0") const
    {
        return eve::testing::modules::install(
            dll, root + '/' + dir, qt ? "org.eve.qt.example" : "org.eve.business",
            qt ? QJsonArray{"org.eve.qt.page", "org.eve.qt.settings"} : QJsonArray{"org.eve.plugin"},
            qt ? "qt-ui" : "c-abi", version);
    }
    eve::Result<eve::module::ModuleLoadReport> load(const std::string& path)
    {
#ifdef EVE_TEST_QT_RESERVATION
        if (qt)
            return eve::module::loadQtModule(root.toStdString(), path);
#endif
        return manager.load(root.toStdString(), path);
    }
    eve::Result<eve::module::ModuleLoadReport> stage(const std::string& path)
    {
#ifdef EVE_TEST_QT_RESERVATION
        if (qt)
            return eve::module::stageQtModule(root.toStdString(), path, current);
#endif
        return manager.stage(root.toStdString(), path, current);
    }
    eve::module::ModuleGenerationSnapshot state(eve::GenerationId id) const
    {
        const auto values = manager.snapshot().value();
        const auto found =
            std::find_if(values.begin(), values.end(), [&](const auto& value) { return value.generation == id; });
        CHECK(found != values.end());
        return *found;
    }
    eve::module::ReplacementReservation reserve() { return manager.reserveReplacement(current, candidate).value(); }
    void checkReserved(bool reserved) const
    {
        for (const auto id : {current, candidate}) {
            const auto value = state(id);
            CHECK(value.replacement.has_value() == reserved);
            if (reserved)
                CHECK(value.replacement->current == current && value.replacement->candidate == candidate);
        }
        CHECK(state(current).role == eve::module::GenerationRole::Current &&
              state(candidate).role == eve::module::GenerationRole::Candidate);
    }
};
