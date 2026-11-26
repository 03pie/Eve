#pragma once
#include <QWidget>
#include <functional>
#include <vector>

class ElaLineEdit;
class ElaListView;
class ElaScrollArea;
class ElaText;
class QBoxLayout;
class QStandardItemModel;
class QVBoxLayout;
class ElaWindow;
class QTabWidget;

namespace cordis::ui::detail
{
/** Page-owned section geometry and search index; changing location never touches workbench history. */
class SettingsPage final : public QWidget
{
  public:
    using Guard = std::function<void(const std::function<void()> &)>;
    SettingsPage(QWidget *parent, Guard guard);
    void addGroup(const char *name, const QString &title, const QString &keywords);
    void addRow(const QString &title, QWidget *controls, const QString &keywords, QWidget *target = nullptr);
    void finish();
    ElaText *status() const { return status_; }

  protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct Row {
        QWidget *card;
        QWidget *target;
        QString keywords;
    };
    struct Group {
        QWidget *box;
        ElaText *heading;
        QVBoxLayout *layout;
        QString keywords;
        std::vector<Row> rows;
    };
    void filter();
    void jump(int index, bool focus);
    void syncLocation();
    void sizeLastGroup();
    Guard guard_;
    ElaLineEdit *search_;
    ElaText *status_;
    ElaText *empty_;
    ElaListView *nav_;
    QStandardItemModel *model_;
    ElaScrollArea *scroll_ = nullptr;
    QWidget *document_;
    QVBoxLayout *sections_;
    QBoxLayout *body_;
    std::vector<Group> groups_;
    int active_ = 0;
    bool filtering_ = false;
};
class WorkbenchPreferences;
SettingsPage *createSettingsPage(ElaWindow *window, QTabWidget *panel, WorkbenchPreferences *prefs,
                                 SettingsPage::Guard guard);
} // namespace cordis::ui::detail
