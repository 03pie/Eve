/** @file upgrade_panel.hpp
 * @brief 具体宿主升级子页；只保留纯预览和原 Task，不拥有事务或业务工厂。
 */
#pragma once
#include "workspace.hpp"
#include <QPointer>
#include <QString>
#include <QWidget>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

class ElaLineEdit;
class ElaPlainTextEdit;
class ElaPushButton;
class ElaText;
class QFileDialog;

namespace notebook {
class UpgradePanel final : public QWidget {
public:
    UpgradePanel(std::weak_ptr<Workspace> workspace, QWidget* parent, std::function<void()> refreshOwner);
    /** @brief 父窗口已持 withModuleView；这里只投影最后取得的原值。 */
    void refresh(const WorkspaceModulesSnapshot& modules, const WorkspaceUpgradeSnapshot& upgrade,
                 const std::string& alias);

private:
    std::weak_ptr<Workspace> workspace_;
    std::function<void()> refreshOwner_;
    ElaLineEdit* source_ = nullptr;
    ElaPushButton *browse_ = nullptr, *previewButton_ = nullptr, *start_ = nullptr, *cancel_ = nullptr,
                  *save_ = nullptr;
    ElaPlainTextEdit *previewText_ = nullptr, *reportText_ = nullptr;
    ElaText *status_ = nullptr, *saveStatus_ = nullptr;
    QPointer<QFileDialog> chooser_;
    std::optional<UpgradePreview> preview_;
    std::optional<cordis::Task<WorkspaceUpgradeReport>> execution_;
    std::optional<cordis::Task<cordis::loader::SaveReport>> saving_;
    std::string alias_;
    QString sourceText_, message_, saveMessage_;
    cordis::GenerationId generation_;
    cordis::RuntimeId runtime_;
    cordis::OperationId operation_;
    std::uint64_t revision_ = 0, inputVersion_ = 0;
    bool acting_ = false, rendering_ = false;

    void act(const std::function<void(Workspace&)>& action);
    void sourceChanged(const QString& text);
    void browse();
    void inspect();
    void start();
    void cancel();
    void save();
};
} // namespace notebook
