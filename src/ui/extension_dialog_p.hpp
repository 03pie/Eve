/** @file extension_dialog_p.hpp
 * @brief 首页卡片共用的 Ela 详情窗口。目录和实例读取均由宿主提供。
 */
#pragma once
#include <eve/qt/scheduler.hpp>
#include <eve/ui/extensions.hpp>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <optional>

class ElaComboBox;
class ElaDialog;
class ElaMenu;
class ElaPushButton;
class ElaText;
class ElaToolButton;
class QTabWidget;
class QWidget;

namespace eve::ui::detail {
class ExtensionFacts;
class ExtensionReadmeView;
class ExtensionFeaturesView;
struct ExtensionWorkspace;

class ExtensionDialog final : public QObject {
public:
    using Workspace = std::function<ExtensionWorkspace(const QString& plugin)>;
    using Present = std::function<Result<void>(ExtensionAction, const QString& plugin, const QString& instance)>;
    ExtensionDialog(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<ExtensionControls> controls,
                    Workspace workspace, Present present, std::function<void()> changed, QWidget* owner);
    void openItem(const QString& moduleId, const QString& instancePath = {});
    void choosePackage(const QString& expectedId = {});
    void syncInventory(const std::vector<ExtensionItem>& inventory);
    std::function<void(const QString&)> categorySelected;
    QString visibleFeaturePlugin() const;
    void refreshFeatures(const QString& plugin, const ExtensionWorkspace& workspace);
private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void updateIcon();
    void resetPresentation();
    void placeWindow();
    void showWindow();
    void reloadItem(QString id);
    void applyInventory(const QString& id, const std::vector<ExtensionItem>& inventory);
    void previewFile(const QString& path, const QString& expectedId, const QString& legacyPlugin);
    void dispatch(ExtensionAction action);
    void presentWorkspaceAction(ExtensionAction action, const QString& plugin, const QString& path);
    void submit(ExtensionAction action, const QString& id, const QString& path, const QString& digest);
    void showFailure(const ErrorInfo& error, std::function<void()> retry);
    void render();
    void renderActions();
    QString featurePlugin() const;
    void renderFeatures(const ExtensionItem* item, const ExtensionWorkspace& workspace);
    bool offers(ExtensionAction action) const;
    std::shared_ptr<qt::QtScheduler> scheduler_;
    std::shared_ptr<ExtensionControls> controls_;
    Workspace workspace_;
    Present present_;
    std::function<void()> changed_;
    std::optional<ExtensionItem> item_;
    std::optional<ExtensionPreview> preview_;
    std::optional<QString> removalHash_;
    std::function<void()> retry_, primaryAction_;
    QString message_, instancePath_;
    QSize ownerSize_;
    // A preview belongs to one opening of the dialog. A submitted mutation belongs to the host.
    quint64 presentation_ = 0;
    bool reading_ = false, applying_ = false;
    ElaDialog* window_;
    QTabWidget* tabs_;
    int featuresTab_, managementTab_;
    ElaText *icon_, *title_, *metadata_, *summary_, *review_, *status_, *scope_;
    ExtensionReadmeView* body_;
    ExtensionReadmeView* changelog_;
    ExtensionFeaturesView* features_;
    ExtensionFacts* facts_;
    ElaComboBox* instance_;
    QWidget *primaryGroup_, *packageGroup_;
    ElaPushButton *primary_, *workspaceOptions_, *uninstall_, *packageOptions_, *cancel_;
    ElaPushButton *retryButton_, *logButton_, *configure_, *add_, *logs_;
    ElaToolButton* more_;
    ElaMenu *workspaceMenu_, *packageMenu_, *menu_;
};
}
