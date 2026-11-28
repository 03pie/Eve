#pragma once
#include <ElaScrollArea.h>
#include <QStringList>
#include <vector>

class ElaText;
class QPlainTextEdit;

namespace eve::ui { struct ExtensionItem; }
namespace eve::ui::detail {
struct ExtensionContribution {
    QString id, title, instance;
    bool window = false;
};

/** Only values from the current workspace, keyed by the host's exact instance generation. */
struct ExtensionWorkspace {
    QStringList paths;
    std::vector<ExtensionContribution> pages, commands;
};

class ExtensionFeaturesView final : public ElaScrollArea {
public:
    explicit ExtensionFeaturesView(QWidget* parent);
    bool setContent(const ExtensionItem* item, const ExtensionWorkspace& workspace);

private:
    QWidget *desktop_, *language_, *configuration_, *pages_, *commands_;
    QPlainTextEdit* defaults_;
    ElaText *desktopText_, *runtimeVersion_, *pageList_, *commandList_;
    QString configSource_;
    bool hasDefaults_ = false;
};
}
