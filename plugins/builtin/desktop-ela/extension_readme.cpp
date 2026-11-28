#include "extension_readme_p.hpp"
#include "extension_links_p.hpp"
#include "theme_palette_p.hpp"
#include <ElaScrollBar.h>
#include <QDesktopServices>
#include <QImage>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>

namespace eve::ui::detail {
namespace {
class ReadmeDocument final : public QTextDocument {
public:
    using QTextDocument::QTextDocument;

protected:
    QVariant loadResource(int type, const QUrl&) override
    {
        // A real (transparent) image also prevents Qt's image handler from trying the filename.
        if (type != ImageResource) return {};
        QImage placeholder(1, 1, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(Qt::transparent);
        return placeholder;
    }
};
}

ExtensionReadmeView::ExtensionReadmeView(QWidget* parent) : QTextBrowser(parent)
{
    setDocument(new ReadmeDocument(this));
    setAccessibleName(tr("扩展说明文档"));
    setFrameShape(QFrame::NoFrame);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBar(new ElaScrollBar(this));
    auto typeface = font();
    typeface.setPixelSize(16);
    setFont(typeface);
    document()->setDefaultFont(typeface);
    document()->setDocumentMargin(8);
    const auto applyTheme = [this](ElaThemeType::ThemeMode mode) {
        applyWorkbenchContentPalette(this, mode);
        formatDocument();
    };
    applyTheme(eTheme->getThemeMode());
    connect(eTheme, &ElaTheme::themeModeChanged, this, applyTheme);
    connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl& link) {
        if (link.isRelative() && link.path().isEmpty() && link.hasFragment()) {
            scrollToAnchor(link.fragment());
        } else if (isExtensionWebLink(link)) {
            QDesktopServices::openUrl(link);
        }
    });
}

void ExtensionReadmeView::setContent(const QString& markdown, const QString& fallback)
{
    const bool hasMarkdown = !markdown.isEmpty();
    const auto text = hasMarkdown ? markdown : fallback;
    if (markdown_ == hasMarkdown && content_ == text) return;
    markdown_ = hasMarkdown;
    content_ = text;
    if (hasMarkdown) {
        document()->setMarkdown(text, {QTextDocument::MarkdownDialectGitHub, QTextDocument::MarkdownNoHTML});
    } else {
        setPlainText(text);
    }
    formatDocument();
    verticalScrollBar()->setValue(0);
}

void ExtensionReadmeView::formatDocument()
{
    // Qt's Markdown importer supplies semantic blocks but very little space between them.
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        cursor.setPosition(block.position());
        auto format = block.blockFormat();
        const bool heading = format.headingLevel() > 0;
        format.setTopMargin(heading && block.previous().isValid() ? 18 : 0);
        format.setBottomMargin(block.textList() ? 3 : 9);
        format.setLineHeight(126, QTextBlockFormat::ProportionalHeight);
        cursor.setBlockFormat(format);
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto part = it.fragment();
            if (part.charFormat().anchorHref().isEmpty()) continue;
            cursor.setPosition(part.position());
            cursor.setPosition(part.position() + part.length(), QTextCursor::KeepAnchor);
            QTextCharFormat link;
            link.setForeground(palette().color(QPalette::Link));
            link.setFontUnderline(true);
            cursor.mergeCharFormat(link);
        }
    }
    cursor.endEditBlock();
}
}
