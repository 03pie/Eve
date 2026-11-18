/** @file notebook_state_scene_p.hpp
 * @brief 使用实际 Kernel、UI providers 与 Notebook Runtime 的有限状态检查装配。
 */
#pragma once
#include "notebook_state_p.hpp"
#include <QPointer>
#include <cordis/kernel.hpp>
#include <cordis/module/qt_module.hpp>
#include <cordis/ui/ela_shell.hpp>
#include <optional>
#include <vector>

class ElaWindow;
class QPlainTextEdit;
class QPushButton;
class QWidget;
namespace notebook_test {
struct Imported {
    Instance parent;
    std::shared_ptr<std::optional<Instance>> child;
};
struct Scene {
    std::shared_ptr<qt::QtScheduler> scheduler = std::make_shared<qt::QtScheduler>();
    std::shared_ptr<Kernel> kernel = std::make_shared<Kernel>(scheduler);
    ui::ElaShell shell{scheduler};
    module::ModuleManager manager;
    GenerationId generation;
    std::optional<Runtime> runtime;
    std::vector<Runtime> parents;
    std::vector<ProviderHandle> providers;
    QPointer<ElaWindow> window;

    Scene(const QString& root, const QString& variant);
    Imported inject(std::optional<StateSnapshot> state = {},
                    std::string config = "{\"title\":\"Notebook state\",\"text\":\"configuration seed\"}");
    ReplacementScope freeze() const;
    StateSnapshot capture(const Instance& instance) const;
    QWidget* page(const Instance& instance) const;
    QPlainTextEdit* editor(const Instance& instance) const;
    QPushButton* publish(const Instance& instance) const;
    std::size_t pageCount() const;
    std::size_t commandCount() const;
    std::size_t outputCount(const std::string& text) const;
    module::ModuleGenerationSnapshot moduleState() const;
    void stop(const Imported& instance);
    void close();
};
} // namespace notebook_test
