#include "appearance_p.hpp"
#include <QAccessibleWidget>

namespace eve::ui::detail
{
namespace
{
class AccessibleToggle final : public QAccessibleWidget
{
public:
    explicit AccessibleToggle(KeyboardToggleSwitch* toggle) : QAccessibleWidget(toggle, QAccessible::CheckBox) {}

    QAccessible::State state() const override
    {
        auto result = QAccessibleWidget::state();
        result.checkable = true;
        result.checked = control()->getIsToggled();
        return result;
    }

    QStringList actionNames() const override
    {
        auto names = QAccessibleWidget::actionNames();
        if (control()->isEnabled())
            names.append(toggleAction());
        return names;
    }

    void doAction(const QString& name) override
    {
        if (name == toggleAction()) {
            if (control()->isEnabled())
                control()->setIsToggled(!control()->getIsToggled());
        } else {
            QAccessibleWidget::doAction(name);
        }
    }

    QStringList keyBindingsForAction(const QString& name) const override
    {
        return name == toggleAction() ? QStringList{QStringLiteral("Space"), QStringLiteral("Enter")}
                                      : QAccessibleWidget::keyBindingsForAction(name);
    }

private:
    KeyboardToggleSwitch* control() const { return static_cast<KeyboardToggleSwitch*>(widget()); }
};
} // namespace

KeyboardToggleSwitch::KeyboardToggleSwitch(QWidget* parent) : ElaToggleSwitch(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    QAccessible::registerAccessibleInterface(new AccessibleToggle(this));
    connect(this, &ElaToggleSwitch::toggled, this, [this] {
        QAccessible::State changed;
        changed.checked = true;
        QAccessibleStateChangeEvent event(this, changed);
        QAccessible::updateAccessibility(&event);
    });
}
} // namespace eve::ui::detail
