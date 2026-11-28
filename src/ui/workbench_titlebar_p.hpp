#pragma once

class ElaMenuBar;
class ElaToolButton;
class ElaWindow;

namespace eve::ui::detail {
/** Mounts the host's existing actions and controls in the three Ela app bar slots. */
void installWorkbenchTitlebar(ElaWindow* window, ElaMenuBar* menus, ElaToolButton* back,
                              ElaToolButton* forward, ElaToolButton* quickOpen, ElaToolButton* navigation,
                              ElaToolButton* panel);
}
