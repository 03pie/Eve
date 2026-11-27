/** @file extension_dialog_p.hpp
 * @brief 首页卡片共用的 Ela 详情窗口。目录和实例读取均由宿主提供。
 */
#pragma once
#include <eve/qt/scheduler.hpp>
#include <eve/ui/extensions.hpp>
#include <QObject>
#include <QStringList>
#include <optional>

class ElaComboBox;
class ElaDialog;
class ElaMenu;
class ElaPushButton;
class ElaText;
class QTabWidget;
class QWidget;

namespace eve::ui::detail {
class ExtensionDialog final : public QObject {
public:
    using Instances = std::function<QStringList(const QString& plugin)>;
    using Present = std::function<Result<void>(ExtensionAction, const QString& plugin, const QString& instance)>;
    ExtensionDialog(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<ExtensionControls> controls,
                    Instances instances, Present present, std::function<void()> changed, QWidget* owner);
    void openItem(const QString& moduleId, const QString& instancePath = {});
    void choosePackage(const QString& expectedId = {});
private:
    void resetPresentation();
    void showWindow();
    void reloadItem(const QString& id);
    void previewFile(const QString& path, const QString& expectedId, const QString& legacyPlugin);
    void dispatch(ExtensionAction action);
    void presentWorkspaceAction(ExtensionAction action, const QString& plugin, const QString& path);
    void submit(ExtensionAction action, const QString& id, const QString& path, const QString& digest);
    void showFailure(const ErrorInfo& error, std::function<void()> retry);
    void render();
    void renderActions();
    bool offers(ExtensionAction action) const;
    std::shared_ptr<qt::QtScheduler> scheduler_;
    std::shared_ptr<ExtensionControls> controls_;
    Instances instances_;
    Present present_;
    std::function<void()> changed_;
    std::optional<ExtensionItem> item_;
    std::optional<ExtensionPreview> preview_;
    std::optional<QString> removalHash_;
    std::function<void()> retry_, primaryAction_;
    QString message_, instancePath_;
    // A preview belongs to one opening of the dialog. A submitted mutation belongs to the host.
    quint64 presentation_ = 0;
    bool reading_ = false, applying_ = false;
    ElaDialog* window_;
    QTabWidget* tabs_;
    ElaText *icon_, *title_, *metadata_, *body_, *identity_, *state_, *review_, *status_, *scope_;
    ElaComboBox* instance_;
    ElaPushButton *primary_, *more_, *cancel_, *retryButton_, *logButton_, *configure_, *add_, *logs_;
    ElaMenu* menu_;
};
}
