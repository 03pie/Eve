#include "appearance_p.hpp"
#include "settings_page_p.hpp"
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaScrollArea.h>
#include <ElaScrollPageArea.h>
#include <QAction>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTimer>
#include <utility>

namespace cordis::ui::detail
{
namespace
{
class SettingsRow final : public ElaScrollPageArea
{
  public:
    SettingsRow(const QString &title, QWidget *control, QWidget *parent) : ElaScrollPageArea(parent)
    {
        setMinimumHeight(75);
        setMaximumHeight(QWIDGETSIZE_MAX);
        layout_ = new QBoxLayout(QBoxLayout::LeftToRight, this);
        layout_->setContentsMargins(12, 8, 12, 8);
        auto name = new ElaText(title, 15, this);
        name->setWordWrap(true);
        layout_->addWidget(name, 1);
        layout_->addWidget(control);
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        ElaScrollPageArea::resizeEvent(event);
        layout_->setDirection(event->size().width() < 510 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    }

  private:
    QBoxLayout *layout_;
};
} // namespace

SettingsPage::SettingsPage(QWidget *parent, Guard guard) : QWidget(parent), guard_(std::move(guard))
{
    setWindowTitle(tr("设置"));
    setObjectName("CordisSettingsPage");
    auto outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 12, 16, 12);
    auto title = new ElaText(tr("设置"), 24, this);
    title->setIsBold(true);
    outer->addWidget(title);
    auto header = new QHBoxLayout;
    search_ = new ElaLineEdit(this);
    search_->setObjectName("CordisSettingsSearch");
    search_->setAccessibleName(tr("搜索工作台设置"));
    search_->setPlaceholderText(tr("查找设置　Ctrl+F"));
    search_->setClearButtonEnabled(true);
    header->addWidget(search_, 1);
    status_ = new ElaText(this);
    status_->setObjectName("CordisPreferencesStatus");
    status_->setTextPixelSize(12);
    status_->setWordWrap(true);
    secondaryText(status_);
    header->addWidget(status_, 1);
    outer->addLayout(header);
    body_ = new QBoxLayout(QBoxLayout::LeftToRight);
    nav_ = new ElaListView(this);
    nav_->setObjectName("CordisSettingsNavigation");
    nav_->setAccessibleName(tr("设置导航"));
    nav_->setIsTransparent(true);
    nav_->setFrameShape(QFrame::NoFrame);
    nav_->setItemHeight(36);
    nav_->setWrapping(false);
    nav_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    nav_->setSelectionMode(QAbstractItemView::SingleSelection);
    model_ = new QStandardItemModel(nav_);
    nav_->setModel(model_);
    nav_->installEventFilter(this);
    body_->addWidget(nav_);
    scroll_ = new ElaScrollArea(this);
    scroll_->setAccessibleName(tr("设置内容"));
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    document_ = new QWidget;
    sections_ = new QVBoxLayout(document_);
    sections_->setContentsMargins(2, 2, 8, 2);
    sections_->setSpacing(14);
    sections_->setAlignment(Qt::AlignTop);
    scroll_->setWidget(document_);
    document_->setAutoFillBackground(false);
    scroll_->viewport()->installEventFilter(this);
    body_->addWidget(scroll_, 1);
    outer->addLayout(body_, 1);
    empty_ = new ElaText(tr("没有匹配的设置。清空搜索可显示全部设置。"), 14, document_);
    empty_->setObjectName("CordisSettingsSearchEmpty");
    empty_->hide();
    sections_->addWidget(empty_);
    connect(nav_, &QListView::clicked, this, [this](QModelIndex item) { jump(item.row(), true); });
    connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { syncLocation(); });
    connect(search_, &QLineEdit::textChanged, this, [this] { guard_([this] { filter(); }); });
    connect(search_, &QLineEdit::returnPressed, this, [this] {
        for (auto &group : groups_)
            for (auto &row : group.rows)
                if (!row.card->isHidden()) {
                    auto target = row.target;
                    if (row.card->findChild<QWidget *>("CordisBackgroundPicker") &&
                        (search_->text().contains(tr("背景")) || search_->text().contains(tr("壁纸"))))
                        target = row.card->findChild<QWidget *>("CordisBackgroundPicker");
                    scroll_->ensureWidgetVisible(target);
                    target->setFocus(Qt::ShortcutFocusReason);
                    return;
                }
    });
    auto find = new QAction(this);
    find->setObjectName("CordisFindSettings");
    find->setShortcut(QKeySequence::Find);
    find->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(find);
    connect(find, &QAction::triggered, this, [this] {
        search_->setFocus();
        search_->selectAll();
    });
    auto clear = new QAction(search_);
    clear->setObjectName("CordisClearSettingsSearch");
    clear->setShortcut(Qt::Key_Escape);
    clear->setShortcutContext(Qt::WidgetShortcut);
    search_->addAction(clear);
    connect(clear, &QAction::triggered, search_, &QLineEdit::clear);
}
void SettingsPage::addGroup(const char *name, const QString &title, const QString &keywords)
{
    auto box = new QWidget(document_);
    box->setObjectName(name);
    auto layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);
    auto heading = new ElaText(title, 18, box);
    heading->setIsBold(true);
    heading->setFocusPolicy(Qt::ClickFocus);
    layout->addWidget(heading);
    sections_->addWidget(box);
    groups_.push_back({box, heading, layout, title + " " + keywords, {}});
    model_->appendRow(new QStandardItem(title));
}
void SettingsPage::addRow(const QString &title, QWidget *control, const QString &keywords, QWidget *target)
{
    auto &group = groups_.back();
    auto row = new SettingsRow(title, control, group.box);
    group.layout->addWidget(row);
    group.rows.push_back({row, target ? target : control, title + " " + keywords});
}
void SettingsPage::finish() { filter(); }
void SettingsPage::filter()
{
    if (filtering_)
        return;
    QScopedValueRollback<bool> updating(filtering_, true);
    const auto query = search_->text().trimmed();
    int first = -1;
    for (int i = 0; i < static_cast<int>(groups_.size()); ++i) {
        auto &group = groups_[i];
        const bool whole = query.isEmpty() || group.keywords.contains(query, Qt::CaseInsensitive);
        bool any = false;
        for (auto &row : group.rows) {
            const bool show = whole || row.keywords.contains(query, Qt::CaseInsensitive);
            row.card->setVisible(show);
            any |= show;
        }
        group.box->setVisible(any);
        nav_->setRowHidden(i, !any);
        if (any && first < 0)
            first = i;
    }
    empty_->setVisible(first < 0);
    nav_->setVisible(first >= 0);
    sizeLastGroup();
    if (first >= 0)
        jump(groups_[active_].box->isHidden() ? first : active_, false);
    if (search_->text().trimmed() != query)
        QTimer::singleShot(0, this, [this] { guard_([this] { filter(); }); });
}
void SettingsPage::sizeLastGroup()
{
    bool last = true;
    for (auto it = groups_.rbegin(); it != groups_.rend(); ++it) {
        it->box->setMinimumHeight(last && !it->box->isHidden() ? qMax(0, scroll_->viewport()->height() - 4) : 0);
        if (!it->box->isHidden())
            last = false;
    }
    sections_->activate();
}
void SettingsPage::jump(int index, bool focus)
{
    if (index < 0 || index >= static_cast<int>(groups_.size()) || groups_[index].box->isHidden())
        return;
    active_ = index;
    const auto selected = model_->index(index, 0);
    nav_->setCurrentIndex(selected);
    nav_->scrollTo(selected);
    scroll_->verticalScrollBar()->setValue(groups_[index].box->y() - 2);
    if (focus)
        groups_[index].heading->setFocus(Qt::OtherFocusReason);
}
void SettingsPage::syncLocation()
{
    if (filtering_ || !isVisible())
        return;
    const int threshold = scroll_->verticalScrollBar()->value() + 3;
    for (int i = static_cast<int>(groups_.size()) - 1; i >= 0; --i) {
        if (groups_[i].box->isHidden() || groups_[i].box->y() > threshold)
            continue;
        active_ = i;
        nav_->setCurrentIndex(model_->index(i, 0));
        nav_->scrollTo(nav_->currentIndex());
        break;
    }
}
bool SettingsPage::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::Resize && scroll_ && object == scroll_->viewport())
        sizeLastGroup();
    if (object == nav_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter) {
            jump(nav_->currentIndex().row(), true);
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}
void SettingsPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const bool narrow = width() < 760;
    body_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    nav_->setFlow(narrow ? QListView::LeftToRight : QListView::TopToBottom);
    nav_->setGridSize(narrow ? QSize(100, 36) : QSize{});
    nav_->setMinimumSize(narrow ? QSize(0, 50) : QSize(150, 0));
    nav_->setMaximumSize(narrow ? QSize(QWIDGETSIZE_MAX, 50) : QSize(150, QWIDGETSIZE_MAX));
}
} // namespace cordis::ui::detail
