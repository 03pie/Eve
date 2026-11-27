#pragma once
#include <eve/qt/scheduler.hpp>
#include <eve/ui/extensions.hpp>
#include <QWidget>
#include <optional>

class ElaListView;
class ElaLineEdit;
class ElaComboBox;
class ElaPushButton;
class ElaText;
class ElaMenu;
class QStandardItemModel;

namespace eve::ui::detail {
/** Displays value snapshots only. All registry changes go through the host's extension controls. */
class ExtensionsPage final : public QWidget {
public:
    using Instances = std::function<QStringList(const QString& plugin)>;
    using Present = std::function<Result<void>(ExtensionAction, const QString& plugin, const QString& instance)>;
    ExtensionsPage(std::shared_ptr<qt::QtScheduler> scheduler, std::shared_ptr<ExtensionControls> controls,
                   Instances instances, Present present, QWidget* parent = nullptr);
    void choosePackage(const QString& expectedId = {});
    void previewFile(const QString& path, const QString& expectedId = {});
    void showDesktops();
    void refresh();
protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
private:
    void rebuild();
    void render();
    void updatePanes();
    void selectRow(bool openDetail);
    void runAction(ExtensionAction action);
    void submit(ExtensionAction action, const QString& id, const QString& path = {}, const QString& digest = {});
    void showFailure(const ErrorInfo& error, std::function<void()> retry);
    const ExtensionItem* selected() const;
    std::shared_ptr<qt::QtScheduler> scheduler_;
    std::shared_ptr<ExtensionControls> controls_;
    Instances instances_;
    Present present_;
    std::vector<ExtensionItem> inventory_;
    std::optional<ExtensionPreview> preview_;
    std::function<void()> retry_;
    QString selectedId_, message_, removalHash_;
    bool busy_ = false, narrowDetail_ = false, confirmRemoval_ = false;
    QWidget *browser_, *details_;
    ElaLineEdit* search_;
    ElaComboBox *filter_, *instance_;
    ElaListView* list_;
    QStandardItemModel* model_;
    ElaText *icon_, *title_, *meta_, *body_, *status_, *empty_;
    ElaPushButton *back_, *install_, *primary_, *more_, *cancel_, *retryButton_, *logButton_;
    ElaMenu* menu_;
    std::function<void()> primaryAction_;
};
}
