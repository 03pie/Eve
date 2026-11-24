#include "reader_page.hpp"
#include "pdf_renderer.hpp"
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>

namespace literature {
namespace {
template<class T> T* named(T* widget, const char* name)
{
    widget->setObjectName(QString::fromLatin1(name));
    return widget;
}

cordis::ErrorInfo operationError(const QString& message)
{
    return cordis::makeError(cordis::ErrorCode::CallbackFailed, message.toStdString());
}
} // namespace

ReaderPage::ReaderPage(std::shared_ptr<ReaderLibrary> library, cordis::Context context,
                       std::shared_ptr<cordis::qt::QtScheduler> scheduler,
                       std::shared_ptr<std::atomic_bool> cancelled, const QString& title)
    : library_(std::move(library)), context_(std::move(context)), scheduler_(std::move(scheduler)),
      cancelled_(std::move(cancelled))
{
    setObjectName(QStringLiteral("LiteratureReaderPage"));
    const auto identity = context_.identity().value();
    setProperty("cordis.instance", QVariant::fromValue<qulonglong>(identity.instance.value()));
    setProperty("cordis.epoch", QVariant::fromValue<qulonglong>(identity.epoch));
    setProperty("readerBusy", false);
    setProperty("readerPageCount", 0);
    setProperty("readerCurrentPage", 0);
    setProperty("readerRenderSerial", 0);
    auto layout = new QVBoxLayout(this);
    auto heading = new QLabel(title, this);
    heading->setStyleSheet(QStringLiteral("font-size: 23px; font-weight: 600;"));
    layout->addWidget(heading);
    auto importRow = new QHBoxLayout;
    importPath_ = named(new QLineEdit(this), "ReaderImportPath");
    importPath_->setPlaceholderText(QStringLiteral("选择或输入本地 PDF 路径，原文件保持不变"));
    browse_ = named(new QPushButton(QStringLiteral("选择 PDF…"), this), "ReaderBrowse");
    import_ = named(new QPushButton(QStringLiteral("导入书库"), this), "ReaderImport");
    importRow->addWidget(importPath_, 1);
    importRow->addWidget(browse_);
    importRow->addWidget(import_);
    layout->addLayout(importRow);
    auto splitter = new QSplitter(this);
    documents_ = named(new QListWidget(splitter), "ReaderDocuments");
    documents_->setMinimumWidth(180);
    documents_->setMaximumWidth(320);
    auto reading = new QWidget(splitter);
    auto readingLayout = new QVBoxLayout(reading);
    readingLayout->setContentsMargins(0, 0, 0, 0);
    auto navigation = new QHBoxLayout;
    previous_ = named(new QPushButton(QStringLiteral("上一页"), reading), "ReaderPreviousPage");
    next_ = named(new QPushButton(QStringLiteral("下一页"), reading), "ReaderNextPage");
    pageNumber_ = named(new QSpinBox(reading), "ReaderPageNumber");
    pageNumber_->setRange(1, 1);
    pageCountLabel_ = named(new QLabel(QStringLiteral("/ 0 页"), reading), "ReaderPageCount");
    zoomControl_ = named(new QComboBox(reading), "ReaderZoom");
    for (double factor : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0})
        zoomControl_->addItem(QString::number(static_cast<int>(factor * 100)) + QLatin1Char('%'), factor);
    zoomControl_->setCurrentIndex(2);
    navigation->addWidget(previous_);
    navigation->addWidget(next_);
    navigation->addWidget(new QLabel(QStringLiteral("页码"), reading));
    navigation->addWidget(pageNumber_);
    navigation->addWidget(pageCountLabel_);
    navigation->addStretch();
    navigation->addWidget(zoomControl_);
    readingLayout->addLayout(navigation);
    scroll_ = named(new QScrollArea(reading), "ReaderScroll");
    scroll_->setWidgetResizable(false);
    scroll_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    canvas_ = named(new QLabel(QStringLiteral("导入 PDF 后在此阅读真实页面")), "ReaderCanvas");
    canvas_->setAlignment(Qt::AlignCenter);
    canvas_->setMinimumSize(380, 400);
    canvas_->setStyleSheet(QStringLiteral("background: white; color: #334155;"));
    scroll_->setWidget(canvas_);
    readingLayout->addWidget(scroll_, 1);
    auto notesPanel = new QWidget(splitter);
    auto notesLayout = new QVBoxLayout(notesPanel);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->addWidget(new QLabel(QStringLiteral("文献笔记"), notesPanel));
    notes_ = named(new QPlainTextEdit(notesPanel), "ReaderNotes");
    notes_->setPlaceholderText(QStringLiteral("记录阅读笔记；点击“保存书库”写入磁盘。"));
    notesLayout->addWidget(notes_, 1);
    auto exportRow = new QHBoxLayout;
    exportPath_ = named(new QLineEdit(notesPanel), "ReaderExportNotesPath");
    exportPath_->setPlaceholderText(QStringLiteral("笔记导出路径（.md / .txt）"));
    exportNotes_ = named(new QPushButton(QStringLiteral("导出笔记"), notesPanel), "ReaderExportNotes");
    exportRow->addWidget(exportPath_, 1);
    exportRow->addWidget(exportNotes_);
    notesLayout->addLayout(exportRow);
    auto markTabs = named(new QTabWidget(notesPanel), "ReaderMarkTabs");
    auto bookmarkPanel = new QWidget(markTabs);
    auto bookmarkLayout = new QVBoxLayout(bookmarkPanel);
    bookmarkLabel_ = named(new QLineEdit(bookmarkPanel), "ReaderBookmarkLabel");
    bookmarkLabel_->setPlaceholderText(QStringLiteral("书签名称（可空）"));
    addBookmark_ = named(new QPushButton(QStringLiteral("为当前页添加书签"), bookmarkPanel), "ReaderAddBookmark");
    bookmarks_ = named(new QListWidget(bookmarkPanel), "ReaderBookmarks");
    bookmarks_->setToolTip(QStringLiteral("点击书签，跳转到对应 PDF 页"));
    removeBookmark_ = named(new QPushButton(QStringLiteral("删除选中书签"), bookmarkPanel), "ReaderRemoveBookmark");
    bookmarkLayout->addWidget(bookmarkLabel_);
    bookmarkLayout->addWidget(addBookmark_);
    bookmarkLayout->addWidget(bookmarks_, 1);
    bookmarkLayout->addWidget(removeBookmark_);
    markTabs->addTab(bookmarkPanel, QStringLiteral("书签"));
    auto annotationPanel = new QWidget(markTabs);
    auto annotationLayout = new QVBoxLayout(annotationPanel);
    quote_ = named(new QLineEdit(annotationPanel), "ReaderQuote");
    quote_->setPlaceholderText(QStringLiteral("引文（手动录入，可空）"));
    annotationNote_ = named(new QPlainTextEdit(annotationPanel), "ReaderAnnotationNote");
    annotationNote_->setPlaceholderText(QStringLiteral("当前页附注；不修改 PDF 正文"));
    annotationNote_->setMaximumHeight(90);
    addAnnotation_ = named(new QPushButton(QStringLiteral("添加当前页附注"), annotationPanel), "ReaderAddAnnotation");
    annotations_ = named(new QListWidget(annotationPanel), "ReaderAnnotations");
    annotations_->setToolTip(QStringLiteral("双击附注跳转到对应 PDF 页"));
    removeAnnotation_ = named(new QPushButton(QStringLiteral("删除选中附注"), annotationPanel), "ReaderRemoveAnnotation");
    annotationLayout->addWidget(quote_);
    annotationLayout->addWidget(annotationNote_);
    annotationLayout->addWidget(addAnnotation_);
    annotationLayout->addWidget(annotations_, 1);
    annotationLayout->addWidget(removeAnnotation_);
    markTabs->addTab(annotationPanel, QStringLiteral("按页附注"));
    notesLayout->addWidget(markTabs, 1);
    notesPanel->setMinimumWidth(220);
    notesPanel->setMaximumWidth(380);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto footer = new QHBoxLayout;
    status_ = named(new QLabel(this), "ReaderStatus");
    status_->setWordWrap(true);
    save_ = named(new QPushButton(QStringLiteral("保存书库"), this), "ReaderSave");
    reload_ = named(new QPushButton(QStringLiteral("重新载入"), this), "ReaderReload");
    footer->addWidget(status_, 1);
    footer->addWidget(reload_);
    footer->addWidget(save_);
    layout->addLayout(footer);

    QObject::connect(browse_, &QPushButton::clicked, this, [this] {
        if (!acceptsInput())
            return;
        const auto file = QFileDialog::getOpenFileName(this, QStringLiteral("导入文献"), {}, QStringLiteral("PDF (*.pdf)"));
        if (!file.isEmpty())
            importPath_->setText(file);
    });
    QObject::connect(import_, &QPushButton::clicked, this, [this] { importPdf(); });
    QObject::connect(documents_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (acceptsInput() && row >= 0)
            selectDocument(documents_->item(row)->data(Qt::UserRole).toString());
    });
    QObject::connect(previous_, &QPushButton::clicked, this, [this] {
        if (acceptsInput() && currentPage_ > 0)
            renderPage(currentPage_ - 1, zoom_);
    });
    QObject::connect(next_, &QPushButton::clicked, this, [this] {
        if (acceptsInput() && currentPage_ + 1 < pageCount_)
            renderPage(currentPage_ + 1, zoom_);
    });
    QObject::connect(pageNumber_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int page) {
        if (acceptsInput() && page - 1 != currentPage_)
            renderPage(page - 1, zoom_);
    });
    QObject::connect(zoomControl_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (acceptsInput() && index >= 0)
            renderPage(currentPage_, zoomControl_->itemData(index).toDouble());
    });
    QObject::connect(notes_, &QPlainTextEdit::textChanged, this, [this] {
        if (!acceptsInput() || documentId_.isEmpty())
            return;
        QString error;
        if (!library_->setNotes(documentId_, notes_->toPlainText(), &error))
            setError(error);
        else
            refreshStatus();
    });
    QObject::connect(scroll_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (acceptsInput())
            rememberPosition();
    });
    QObject::connect(save_, &QPushButton::clicked, this, [this] { (void)saveLibrary(); });
    QObject::connect(addBookmark_, &QPushButton::clicked, this, [this] { addBookmark(); });
    QObject::connect(removeBookmark_, &QPushButton::clicked, this, [this] { removeBookmark(); });
    QObject::connect(exportNotes_, &QPushButton::clicked, this, [this] { exportNotes(); });
    QObject::connect(addAnnotation_, &QPushButton::clicked, this, [this] { addAnnotation(); });
    QObject::connect(removeAnnotation_, &QPushButton::clicked, this, [this] { removeAnnotation(); });
    QObject::connect(bookmarks_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (acceptsInput() && item)
            renderPage(item->data(Qt::UserRole).toInt(), zoom_);
    });
    QObject::connect(annotations_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (acceptsInput() && item)
            renderPage(item->data(Qt::UserRole).toInt(), zoom_);
    });
    QObject::connect(reload_, &QPushButton::clicked, this, [this] {
        if (!acceptsInput())
            return;
        if (library_->hasUnsavedChanges() && QMessageBox::question(this, QStringLiteral("丢弃未保存的更改？"),
                QStringLiteral("重新载入会丢弃当前未保存的笔记和阅读位置。"), QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No) != QMessageBox::Yes)
            return;
        QString error;
        if (!library_->reloadDiscardingChanges(&error)) {
            setError(error);
            return;
        }
        documentId_.clear();
        refreshDocuments();
        if (!library_->activeDocumentId().isEmpty())
            selectDocument(library_->activeDocumentId());
        else {
            pageCount_ = currentPage_ = 0;
            setProperty("readerDocumentId", QString());
            setProperty("readerPageCount", 0);
            setProperty("readerCurrentPage", 0);
            canvas_->clear();
            canvas_->setText(QStringLiteral("导入 PDF 后在此阅读真实页面"));
            const QSignalBlocker notesBlocker(notes_);
            notes_->clear();
            refreshMarks();
            setBusy(false);
            refreshStatus();
        }
    });
    refreshDocuments();
    setBusy(false);
    refreshStatus();
    if (!library_->activeDocumentId().isEmpty())
        QTimer::singleShot(0, this, [this] {
            if (acceptsInput())
                selectDocument(library_->activeDocumentId());
        });
}

ReaderPage::~ReaderPage() { cancelled_->store(true); }

bool ReaderPage::acceptsInput() const { return !busy_ && context_.identity().hasValue(); }

void ReaderPage::setBusy(bool busy)
{
    busy_ = busy;
    setProperty("readerBusy", busy);
    const bool documentAvailable = !documentId_.isEmpty();
    QWidget* widgets[] = {importPath_, browse_, import_, documents_, reload_, save_};
    for (auto* widget : widgets)
        widget->setEnabled(!busy);
    notes_->setEnabled(!busy && documentAvailable);
    QWidget* markWidgets[] = {bookmarkLabel_, bookmarks_, addBookmark_, removeBookmark_, exportPath_, exportNotes_,
                             quote_, annotationNote_, annotations_, addAnnotation_, removeAnnotation_};
    for (auto* widget : markWidgets)
        widget->setEnabled(!busy && documentAvailable);
    pageNumber_->setEnabled(!busy && documentAvailable && pageCount_ > 0);
    zoomControl_->setEnabled(!busy && documentAvailable);
    previous_->setEnabled(!busy && currentPage_ > 0);
    next_->setEnabled(!busy && currentPage_ + 1 < pageCount_);
    if (busy)
        status_->setText(QStringLiteral("正在读取 PDF，请稍候…"));
}

void ReaderPage::setError(const QString& message)
{
    status_->setText(QStringLiteral("操作失败：%1%2").arg(message,
        library_->hasUnsavedChanges() ? QStringLiteral("；更改仍未保存") : QString()));
}

void ReaderPage::refreshStatus()
{
    const auto saved = library_->hasUnsavedChanges() ? QStringLiteral("有未保存的更改") : QStringLiteral("书库已保存");
    status_->setText(QStringLiteral("%1 · %2 篇文献 · 修订 %3").arg(saved).arg(library_->documents().size()).arg(library_->revision()));
}

void ReaderPage::refreshDocuments()
{
    const QSignalBlocker blocker(documents_);
    documents_->clear();
    for (const auto& document : library_->documents()) {
        auto item = new QListWidgetItem(document.title, documents_);
        item->setData(Qt::UserRole, document.id);
        item->setToolTip(document.originalFileName);
        if (document.id == library_->activeDocumentId())
            documents_->setCurrentItem(item);
    }
}

void ReaderPage::importPdf()
{
    if (!acceptsInput())
        return;
    const auto source = importPath_->text().trimmed();
    const auto root = library_->rootPath();
    const auto title = QFileInfo(source).completeBaseName();
    const auto cancelled = cancelled_;
    startWork<PreparedImport>([root, source, title, cancelled](cordis::qt::WorkerCancellation stop) {
        QString error;
        if (stop.isCancellationRequested())
            return cordis::Result<PreparedImport>::failure(operationError(QStringLiteral("Cancelled.")));
        auto prepared = ReaderLibrary::prepareImport(root, source, title, cancelled.get(), &error);
        return prepared ? cordis::Result<PreparedImport>::success(std::move(*prepared))
                        : cordis::Result<PreparedImport>::failure(operationError(error));
    }, [this](const PreparedImport& prepared) {
        QString id, error;
        if (!library_->adoptPreparedImport(prepared, &id, &error)) {
            setError(error);
            return;
        }
        refreshDocuments();
        selectDocument(id);
    });
}

void ReaderPage::selectDocument(const QString& id)
{
    if (!acceptsInput())
        return;
    rememberPosition();
    const auto* found = library_->document(id);
    if (!found)
        return;
    const auto document = *found;
    QString error;
    if (!library_->setActiveDocument(id, &error)) {
        setError(error);
        return;
    }
    documentId_ = id;
    pageCount_ = 0;
    setProperty("readerDocumentId", id);
    setProperty("readerPageCount", 0);
    canvas_->clear();
    canvas_->setText(QStringLiteral("正在装载所选 PDF…"));
    const QSignalBlocker notesBlocker(notes_);
    notes_->setPlainText(document.notes);
    refreshMarks();
    refreshDocuments();
    renderPage(document.position.page, document.position.zoom);
}

void ReaderPage::renderPage(int requestedPage, double zoom)
{
    if (!acceptsInput() || documentId_.isEmpty())
        return;
    const auto* found = library_->document(documentId_);
    if (!found)
        return;
    const auto document = *found;
    const auto root = library_->rootPath();
    const auto cancelled = cancelled_;
    const auto requestedId = documentId_;
    startWork<RenderedPage>([root, document, requestedPage, zoom, cancelled](cordis::qt::WorkerCancellation stop) {
        RenderedPage result;
        QString error;
        if (stop.isCancellationRequested() || !renderPdfPage(root, document, requestedPage, zoom, cancelled.get(), &result, &error))
            return cordis::Result<RenderedPage>::failure(operationError(error.isEmpty() ? QStringLiteral("Cancelled.") : error));
        return cordis::Result<RenderedPage>::success(std::move(result));
    }, [this, requestedId, zoom, oldPosition = document.position](const RenderedPage& result) {
        if (documentId_ != requestedId)
            return;
        pageCount_ = result.pageCount;
        currentPage_ = result.page;
        zoom_ = zoom;
        canvas_->setPixmap(QPixmap::fromImage(result.image));
        canvas_->resize(result.image.size());
        {
            const QSignalBlocker pageBlocker(pageNumber_), zoomBlocker(zoomControl_);
            pageNumber_->setRange(1, pageCount_);
            pageNumber_->setValue(currentPage_ + 1);
            const int index = zoomControl_->findData(zoom_);
            if (index >= 0)
                zoomControl_->setCurrentIndex(index);
        }
        pageCountLabel_->setText(QStringLiteral("/ %1 页").arg(pageCount_));
        setProperty("readerPageCount", pageCount_);
        setProperty("readerCurrentPage", currentPage_);
        setProperty("readerRenderSerial", ++renderSerial_);
        const int restoreScroll = oldPosition.page == currentPage_ ? oldPosition.scroll : 0;
        {
            const QSignalBlocker scrollBlocker(scroll_->verticalScrollBar());
            scroll_->verticalScrollBar()->setValue(restoreScroll);
        }
        QString error;
        if (!library_->setReadingPosition(documentId_, {currentPage_, zoom_, restoreScroll}, &error))
            setError(error);
        else
            refreshStatus();
        setBusy(false);
    });
}

void ReaderPage::rememberPosition()
{
    if (documentId_.isEmpty() || pageCount_ <= 0)
        return;
    QString error;
    if (!library_->setReadingPosition(documentId_, {currentPage_, zoom_, scroll_->verticalScrollBar()->value()}, &error))
        setError(error);
}

bool ReaderPage::saveLibrary()
{
    if (!acceptsInput())
        return false;
    rememberPosition();
    QString error;
    if (!library_->save(&error)) {
        setError(error);
        return false;
    }
    refreshStatus();
    return true;
}

void ReaderPage::refreshMarks()
{
    bookmarks_->clear();
    annotations_->clear();
    const auto* document = library_->document(documentId_);
    if (!document)
        return;
    for (const auto& bookmark : document->bookmarks) {
        auto item = new QListWidgetItem(QStringLiteral("第 %1 页 · %2").arg(bookmark.page + 1).arg(bookmark.label), bookmarks_);
        item->setData(Qt::UserRole, bookmark.page);
        item->setData(Qt::UserRole + 1, bookmark.id);
    }
    for (const auto& annotation : document->annotations) {
        auto item = new QListWidgetItem(QStringLiteral("第 %1 页 · %2").arg(annotation.page + 1).arg(annotation.note), annotations_);
        item->setData(Qt::UserRole, annotation.page);
        item->setData(Qt::UserRole + 1, annotation.id);
        item->setToolTip(annotation.quote);
    }
}

void ReaderPage::addBookmark()
{
    if (!acceptsInput() || documentId_.isEmpty() || pageCount_ <= 0)
        return;
    QString error;
    if (!library_->addBookmark(documentId_, currentPage_, bookmarkLabel_->text(), nullptr, &error))
        setError(error);
    else {
        bookmarkLabel_->clear();
        refreshMarks();
        refreshStatus();
    }
}

void ReaderPage::removeBookmark()
{
    if (!acceptsInput() || !bookmarks_->currentItem())
        return;
    const auto id = bookmarks_->currentItem()->data(Qt::UserRole + 1).toString();
    QString error;
    if (!library_->removeBookmark(documentId_, id, &error))
        setError(error);
    else {
        refreshMarks();
        refreshStatus();
    }
}

void ReaderPage::addAnnotation()
{
    if (!acceptsInput() || documentId_.isEmpty() || pageCount_ <= 0)
        return;
    QString error;
    if (!library_->addAnnotation(documentId_, currentPage_, quote_->text(), annotationNote_->toPlainText(), nullptr, &error))
        setError(error);
    else {
        quote_->clear();
        annotationNote_->clear();
        refreshMarks();
        refreshStatus();
    }
}

void ReaderPage::removeAnnotation()
{
    if (!acceptsInput() || !annotations_->currentItem())
        return;
    const auto id = annotations_->currentItem()->data(Qt::UserRole + 1).toString();
    QString error;
    if (!library_->removeAnnotation(documentId_, id, &error))
        setError(error);
    else {
        refreshMarks();
        refreshStatus();
    }
}

void ReaderPage::exportNotes()
{
    if (!acceptsInput() || documentId_.isEmpty())
        return;
    QString error;
    if (!library_->exportNotes(documentId_, exportPath_->text().trimmed(), &error))
        setError(error);
    else
        status_->setText(QStringLiteral("笔记已导出；书库%1").arg(library_->hasUnsavedChanges()
                           ? QStringLiteral("仍有未保存更改") : QStringLiteral("已保存")));
}
} // namespace literature
