#pragma once

#include <ElaScrollArea.h>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <array>
#include <functional>

class ElaFlowLayout;
class ElaText;
class QGridLayout;
class QPushButton;

namespace eve::ui {
struct ExtensionItem;

namespace detail {
/** The extension sidebar presents catalog values and delegates category navigation to the dialog. */
class ExtensionFacts final : public ElaScrollArea {
public:
    using CategorySelected = std::function<void(const QString&)>;
    ExtensionFacts(QWidget* parent, CategorySelected categorySelected);
    void setItem(const ExtensionItem* item, bool preview, bool categoryNavigationEnabled);

private:
    struct FactRow {
        FactRow() = default;
        FactRow(QGridLayout* grid, const QString& caption, const char* id);
        void setValue(const QString& text) const;
        ElaText* name = nullptr;
        ElaText* value = nullptr;
    };
    struct Resource {
        QPushButton* button = nullptr;
        QUrl target;
    };
    void setCategories(const QStringList& categories, bool navigationEnabled);
    void setResources(const ExtensionItem& item);

    CategorySelected categorySelected_;
    QWidget *workspace_, *categories_, *resources_;
    ElaFlowLayout* tags_;
    ElaText* license_;
    FactRow identity_, plugin_, version_, source_, state_, packageBytes_, installedAt_, instances_;
    QVector<QPushButton*> categoryButtons_;
    std::array<Resource, 4> links_;
};
}
}
