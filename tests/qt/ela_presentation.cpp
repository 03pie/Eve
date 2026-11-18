/** @file ela_presentation.cpp
 * @brief 真实 Ela 节点准备、可见索引、菜单和延迟切页的组件检查。
 */
#include "ui_publication_p.hpp"
#include <ElaNavigationBar.h>
#include <QAction>
#include <QMenu>

namespace {
using namespace presentation_test;
QString add(ElaWindow& window, const char* title, const QString& parent = {}, std::function<bool()> gate = {})
{
    auto* page = new QWidget;
    const auto result =
        parent.isEmpty()
            ? window.addPageNode(QString::fromLatin1(title), page, ElaIconType::None, std::move(gate))
            : window.addPageNode(QString::fromLatin1(title), page, parent, ElaIconType::None, std::move(gate));
    CHECK(result == ElaNavigationType::Success);
    const auto key = page->property("ElaPageKey").toString();
    CHECK(!key.isEmpty());
    return key;
}
bool suggested(ElaWindow& window, const QString& key)
{
    for (const auto& row : window.getNavigationSuggestDataList())
        if (row.getSuggestData().value("ElaPageKey").toString() == key)
            return true;
    return false;
}
void held()
{
    ElaWindow window;
    window.setStackSwitchMode(ElaWindowType::None);
    const auto safe = add(window, "safe");
    bool published = false;
    QString group;
    CHECK(window.addExpanderNode("candidate", group, ElaIconType::None, [&] { return published; }) ==
          ElaNavigationType::Success);
    const auto page = add(window, "hidden", group, [&] { return published; });
    CHECK(!window.isNodePresentable(page) && !window.getNavigationNodeIndex(page).isValid());
    CHECK(!window.getNavigationNodeIndex(group).isValid() && !suggested(window, page));
    window.navigation(page);
    CHECK(window.getCurrentNavigationPageKey() == safe);
    published = true;
    CHECK(window.isNodePresentable(page));
    window.refreshNavigationPresentation();
    const auto index = window.getNavigationNodeIndex(page);
    CHECK(index.isValid() && index.parent() == window.getNavigationNodeIndex(group) && suggested(window, page));
    window.navigation(page);
    CHECK(window.getCurrentNavigationPageKey() == page);
    published = false;
    window.refreshNavigationPresentation();
    CHECK(window.getCurrentNavigationPageKey().isEmpty() && !window.getNavigationNodeIndex(page).isValid());
    window.removeNavigationNode(group);
    CHECK(!suggested(window, page) && !window.isNodePresentable(page));
}
void indices()
{
    ElaWindow window;
    window.setStackSwitchMode(ElaWindowType::None);
    const auto first = add(window, "first");
    bool visible = false;
    QString group;
    CHECK(window.addExpanderNode("group", group, ElaIconType::None, [&] { return visible; }) ==
          ElaNavigationType::Success);
    const auto child = add(window, "child", group);
    const auto last = add(window, "last");
    CHECK(window.getNavigationNodeIndex(last).row() == 1);
    visible = true;
    window.refreshNavigationPresentation();
    CHECK(window.getNavigationNodeIndex(group).row() == 1 && window.getNavigationNodeIndex(last).row() == 2);
    CHECK(window.getNavigationNodeIndex(child).row() == 0 &&
          window.getNavigationNodeIndex(child).parent() == window.getNavigationNodeIndex(group));
    window.removeNavigationNode(first);
    CHECK(window.getNavigationNodeIndex(group).row() == 0 && window.getNavigationNodeIndex(last).row() == 1);
    visible = false;
    window.refreshNavigationPresentation();
    CHECK(window.getNavigationNodeIndex(last).row() == 0 && !window.getNavigationNodeIndex(child).isValid());
    window.setNavigationBarDisplayMode(ElaNavigationType::Compact);
    window.setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    visible = true;
    window.refreshNavigationPresentation();
    CHECK(window.getNavigationNodeIndex(group).row() == 0 && window.getNavigationNodeIndex(last).row() == 1);
    CHECK(window.getNavigationNodeIndex(child).parent() == window.getNavigationNodeIndex(group));
}
void stack()
{
    ElaWindow window;
    window.setStackSwitchMode(ElaWindowType::None);
    bool visible = false;
    const auto hidden = add(window, "first held", {}, [&] { return visible; });
    CHECK(window.getCurrentNavigationPageKey().isEmpty());
    const auto shown = add(window, "shown");
    window.navigation(shown);
    CHECK(window.getCurrentNavigationPageKey() == shown);
    window.removeNavigationNode(shown);
    CHECK(window.getCurrentNavigationPageKey().isEmpty());
    window.navigation(hidden);
    CHECK(window.getCurrentNavigationPageKey().isEmpty());
    visible = true;
    window.refreshNavigationPresentation();
    window.navigation(hidden);
    CHECK(window.getCurrentNavigationPageKey() == hidden);
}
void compact()
{
    ElaWindow window;
    window.setStackSwitchMode(ElaWindowType::None);
    const auto safe = add(window, "safe");
    bool visible = false;
    QString group;
    CHECK(window.addExpanderNode("group", group) == ElaNavigationType::Success);
    const auto page = add(window, "candidate", group, [&] { return visible; });
    QAction* action = nullptr;
    for (auto* menu : window.findChildren<QMenu*>())
        for (auto* candidate : menu->actions())
            if (candidate->data().toString() == page)
                action = candidate;
    CHECK(action && !action->isVisible() && !suggested(window, page));
    action->trigger();
    CHECK(window.getCurrentNavigationPageKey() == safe);
    visible = true;
    window.refreshNavigationPresentation();
    CHECK(action->isVisible() && suggested(window, page));
    visible = false; // 已显示的旧菜单动作也必须在触发时重查。
    action->trigger();
    CHECK(window.getCurrentNavigationPageKey() == safe);
}
void animation()
{
    ElaWindow window;
    const auto safe = add(window, "safe");
    const auto oldTarget = add(window, "ordinary target");
    const auto controlled = add(window, "controlled", {}, [] { return true; });
    window.navigation(oldTarget); // 默认 Popup 在 180ms 后才实际切页。
    window.navigation(controlled);
    CHECK(window.getCurrentNavigationPageKey() == controlled);
    bool elapsed = false;
    QTimer::singleShot(550, &window, [&] { elapsed = true; });
    until([&] { return elapsed; });
    CHECK(window.getCurrentNavigationPageKey() == controlled);
    window.setStackSwitchMode(ElaWindowType::None);
    window.navigation(safe);
    window.setStackSwitchMode(ElaWindowType::Popup);
    window.navigation(oldTarget);
    window.removeNavigationNode(oldTarget);
    const auto replacement = add(window, "held replacement", {}, [] { return false; });
    elapsed = false;
    QTimer::singleShot(550, &window, [&] { elapsed = true; });
    until([&] { return elapsed; });
    CHECK(window.getCurrentNavigationPageKey() != replacement);
}
void animationCancel()
{
    for (const auto mode : {ElaWindowType::Scale, ElaWindowType::Flip, ElaWindowType::Popup}) {
        ElaWindow window;
        add(window, "safe");
        const auto target = add(window, "target");
        const auto unrelated = add(window, "unrelated");
        QPointer<QWidget> targetWidget;
        for (auto* widget : window.findChildren<QWidget*>())
            if (widget->property("ElaPageKey").toString() == target)
                targetWidget = widget;
        CHECK(targetWidget);
        window.setStackSwitchMode(mode);
        window.navigation(target);
        // 观察真实动画已把 B 选中并隐藏，不按经过时间推测动画阶段。
        until(
            [&] { return window.getCurrentNavigationPageKey() == target && targetWidget && targetWidget->isHidden(); });
        window.removeNavigationNode(unrelated);
        CHECK(window.getCurrentNavigationPageKey() == target && targetWidget && !targetWidget->isHidden());
        window.navigation(target);
        CHECK(window.getCurrentNavigationPageKey() == target && !targetWidget->isHidden());
        window.removeNavigationNode(target);
        CHECK(window.getCurrentNavigationPageKey().isEmpty() && targetWidget && targetWidget->isHidden());
    }
    // 视图尚未 refresh 时，live gate 也必须禁止“恢复”已变为 Held 的当前目标。
    ElaWindow window;
    add(window, "safe");
    bool visible = true;
    const auto controlled = add(window, "controlled", {}, [&] { return visible; });
    const auto unrelated = add(window, "unrelated");
    window.navigation(controlled);
    CHECK(window.getCurrentNavigationPageKey() == controlled);
    visible = false;
    window.removeNavigationNode(unrelated);
    CHECK(window.getCurrentNavigationPageKey().isEmpty() && !window.isNodePresentable(controlled));
    window.navigation(controlled);
    CHECK(window.getCurrentNavigationPageKey().isEmpty());
}
} // namespace

bool elaPresentationCase(const QString& name)
{
    if (name == "ela_held")
        held();
    else if (name == "ela_indices")
        indices();
    else if (name == "ela_stack")
        stack();
    else if (name == "ela_compact")
        compact();
    else if (name == "ela_animation")
        animation();
    else if (name == "ela_animation_cancel")
        animationCancel();
    else
        return false;
    return true;
}
