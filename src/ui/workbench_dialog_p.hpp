#pragma once

class QWidget;
class ElaDialog;
namespace cordis::ui::detail {
/** Platform tool windows share the workbench wallpaper; plugin-owned windows manage their own appearance. */
ElaDialog* createWorkbenchDialog(QWidget* parent);
}
