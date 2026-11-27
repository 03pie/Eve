#pragma once
#include <QLineEdit>
#include <QPointer>
#include <utility>
#include <vector>

namespace eve::ui::detail {
/** Focus and visible owned windows temporarily left by an explicit workbench navigation. */
class WorkbenchReturnPoint {
public:
    QWidget* target() const { return widget_; }
    bool hasSuspendedWindows() const { return !windows_.empty(); }

    void capture(QWidget* widget, QWidget* mainWindow)
    {
        widget_ = widget;
        anchor_ = -1;
        text_.clear();
        if (!widget)
            return;
        if (auto edit = qobject_cast<QLineEdit*>(widget); edit && edit->hasSelectedText()) {
            text_ = edit->text();
            cursor_ = edit->cursorPosition();
            anchor_ = edit->selectionStart();
            if (cursor_ == anchor_)
                anchor_ += edit->selectedText().size();
        }
        std::vector<QPointer<QWidget>> chain;
        auto window = widget->window();
        while (window && window != mainWindow) {
            if (window->isVisible())
                chain.push_back(window);
            auto owner = window->parentWidget();
            window = owner ? owner->window() : nullptr;
        }
        if (window == mainWindow)
            for (const auto& dialog : chain) {
                windows_.push_back(dialog);
                if (dialog)
                    dialog->hide();
            }
    }

    // Keep an already suspended tool reachable when a main-window overlay
    // passes us its target. Explicit navigation from a different tool wins.
    void take(WorkbenchReturnPoint next)
    {
        if (hasSuspendedWindows() && !next.hasSuspendedWindows())
            return;
        windows_.insert(windows_.end(), next.windows_.begin(), next.windows_.end());
        widget_ = next.widget_;
        text_ = std::move(next.text_);
        anchor_ = next.anchor_;
        cursor_ = next.cursor_;
    }

    void restore(QWidget* fallback = nullptr)
    {
        auto saved = std::exchange(*this, {});
        QPointer<QWidget> fallbackFocus(fallback);
        for (auto it = saved.windows_.rbegin(); it != saved.windows_.rend(); ++it)
            if (*it)
                (*it)->show();
        auto focus = saved.widget_;
        if (!focus || !focus->isVisible() || !focus->isEnabled())
            focus = fallbackFocus;
        if (!focus || !focus->isVisible() || !focus->isEnabled())
            return;
        focus->window()->raise();
        focus->window()->activateWindow();
        focus->setFocus(Qt::OtherFocusReason);
        if (auto edit = qobject_cast<QLineEdit*>(focus.data());
            edit && focus == saved.widget_ && saved.anchor_ >= 0 && edit->text() == saved.text_)
            edit->setSelection(saved.anchor_, saved.cursor_ - saved.anchor_);
    }

private:
    QPointer<QWidget> widget_;
    std::vector<QPointer<QWidget>> windows_;
    QString text_;
    int anchor_ = -1, cursor_ = 0;
};
}
