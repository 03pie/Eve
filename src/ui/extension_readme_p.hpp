#pragma once
#include <QTextBrowser>

namespace eve::ui::detail {
/** Offline extension documentation. Only user-activated web links leave the reader. */
class ExtensionReadmeView final : public QTextBrowser {
public:
    explicit ExtensionReadmeView(QWidget* parent);
    void setContent(const QString& markdown, const QString& fallback);

private:
    void formatDocument();
    QString content_;
    bool markdown_ = false;
};
}
