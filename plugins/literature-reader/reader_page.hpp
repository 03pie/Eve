#pragma once

#include "reader_library.hpp"
#include <QPointer>
#include <QWidget>
#include <cordis/qt/value_worker.hpp>
#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;

namespace literature {

class ReaderPage final : public QWidget {
public:
    ReaderPage(std::shared_ptr<ReaderLibrary> library, cordis::Context context,
               std::shared_ptr<cordis::qt::QtScheduler> scheduler,
               std::shared_ptr<std::atomic_bool> cancelled, const QString& title);
    ~ReaderPage() override;
    bool saveLibrary();

private:
    template<class T>
    void startWork(std::function<cordis::Result<T>(cordis::qt::WorkerCancellation)> work,
                   std::function<void(const T&)> success)
    {
        if (busy_ || !context_.identity().hasValue())
            return;
        setBusy(true);
        const auto serial = ++jobSerial_;
        auto worker = cordis::qt::QtValueWorker<T>::start(context_, scheduler_, std::move(work), "literature.pdf");
        if (!worker.hasValue()) {
            setBusy(false);
            setError(QString::fromStdString(worker.error().message));
            return;
        }
        const QPointer<ReaderPage> page(this);
        worker.value().result().thenOn(scheduler_, [page, context = context_, serial, success = std::move(success)]
                                      (const cordis::Result<T>& result) {
            if (!page || !context.identity().hasValue() || page->jobSerial_ != serial)
                return;
            page->setBusy(false);
            if (!result.hasValue()) {
                page->setError(QString::fromStdString(result.error().message));
                return;
            }
            success(result.value());
        });
    }

    void importPdf();
    void refreshDocuments();
    void selectDocument(const QString& id);
    void renderPage(int page, double zoom);
    void rememberPosition();
    void refreshMarks();
    void addBookmark();
    void removeBookmark();
    void addAnnotation();
    void removeAnnotation();
    void exportNotes();
    void refreshStatus();
    void setError(const QString& message);
    void setBusy(bool busy);
    bool acceptsInput() const;
    std::shared_ptr<ReaderLibrary> library_;
    cordis::Context context_;
    std::shared_ptr<cordis::qt::QtScheduler> scheduler_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QString documentId_;
    bool busy_ = false;
    int pageCount_ = 0;
    int currentPage_ = 0;
    double zoom_ = 1.0;
    int jobSerial_ = 0;
    int renderSerial_ = 0;
    QLineEdit* importPath_ = nullptr;
    QListWidget* documents_ = nullptr;
    QPushButton* import_ = nullptr;
    QPushButton* browse_ = nullptr;
    QPushButton* previous_ = nullptr;
    QPushButton* next_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* reload_ = nullptr;
    QSpinBox* pageNumber_ = nullptr;
    QComboBox* zoomControl_ = nullptr;
    QLabel* pageCountLabel_ = nullptr;
    QLabel* canvas_ = nullptr;
    QLabel* status_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QPlainTextEdit* notes_ = nullptr;
    QLineEdit* bookmarkLabel_ = nullptr;
    QListWidget* bookmarks_ = nullptr;
    QPushButton* addBookmark_ = nullptr;
    QPushButton* removeBookmark_ = nullptr;
    QLineEdit* exportPath_ = nullptr;
    QPushButton* exportNotes_ = nullptr;
    QLineEdit* quote_ = nullptr;
    QPlainTextEdit* annotationNote_ = nullptr;
    QListWidget* annotations_ = nullptr;
    QPushButton* addAnnotation_ = nullptr;
    QPushButton* removeAnnotation_ = nullptr;
};
} // namespace literature
