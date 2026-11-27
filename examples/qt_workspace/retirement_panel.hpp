/** @file retirement_panel.hpp
 * @brief 高级回收视图只保留选中代次和最后一次纯回执，不持模块执行资源。
 */
#pragma once
#include "workspace.hpp"
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class ElaComboBox;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaText;

namespace notebook {
class RetirementPanel final : public QWidget {
public:
    RetirementPanel(std::weak_ptr<Workspace> workspace, QWidget* parent, std::function<void()> refreshOwner);
    /** @brief 父组件已持原 withModuleView；目录及回执都只是纯值。 */
    void refresh(const WorkspaceModulesSnapshot& modules, const WorkspaceUpgradeSnapshot& upgrade);

private:
    std::weak_ptr<Workspace> workspace_;
    std::function<void()> refreshOwner_;
    ElaComboBox* generations_ = nullptr;
    ElaPushButton* retry_ = nullptr;
    ElaText* status_ = nullptr;
    ElaPlainTextEdit* details_ = nullptr;
    eve::GenerationId selected_, attempted_;
    std::optional<eve::Result<WorkspaceRetirementReport>> lastAttempt_;
    bool rendering_ = false, acting_ = false;

    void selectionChanged();
    void retry();
};
} // namespace notebook
