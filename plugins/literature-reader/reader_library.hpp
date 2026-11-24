#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <atomic>
#include <optional>

namespace literature {

struct ReadingPosition {
    int page = 0;
    double zoom = 1.0;
    int scroll = 0;
};

struct Bookmark {
    QString id;
    int page = 0;
    QString label;
};

struct Annotation {
    QString id;
    int page = 0;
    QString quote;
    QString note;
};

struct Document {
    QString id;
    QString title;
    QString originalFileName;
    QString sha256;
    qint64 bytes = 0;
    ReadingPosition position;
    QVector<Bookmark> bookmarks;
    QVector<Annotation> annotations;
    QString notes;
};

class PreparedImport {
public:
    PreparedImport(const PreparedImport&) = default;
    PreparedImport(PreparedImport&&) = default;
    PreparedImport& operator=(const PreparedImport&) = default;
    PreparedImport& operator=(PreparedImport&&) = default;
private:
    friend class ReaderLibrary;
    PreparedImport() = default;
    QString root_;
    Document document_;
    qint64 modifiedMSecs_ = 0;
};

/** Plugin-owned PDF library; no platform services or implicit save on destruction.
 * Instance methods run on the owning UI thread. The static file operations take
 * snapshots only and may run in an owned worker. Returned document pointers remain
 * valid only until the next mutation. Page numbers are zero-based.
 */
class ReaderLibrary {
public:
    bool open(const QString& root, QString* error = nullptr);
    bool reloadDiscardingChanges(QString* error = nullptr);
    bool importDocument(const QString& source, const QString& title,
                        QString* documentId = nullptr, QString* error = nullptr);
    static std::optional<PreparedImport> prepareImport(const QString& root, const QString& source, const QString& title,
                                                      const std::atomic_bool* cancelled = nullptr, QString* error = nullptr);
    bool adoptPreparedImport(PreparedImport prepared, QString* documentId = nullptr, QString* error = nullptr);
    static QString verifiedFilePathFor(const QString& root, const Document& document,
                                      const std::atomic_bool* cancelled = nullptr, QString* error = nullptr);
    QString verifiedFilePath(const QString& id, QString* error = nullptr) const;

    const QVector<Document>& documents() const noexcept { return documents_; }
    const Document* document(const QString& id) const;
    QString rootPath() const { return root_; }
    QString activeDocumentId() const { return activeDocument_; }
    int revision() const noexcept { return revision_; }
    bool hasUnsavedChanges() const noexcept { return dirty_; }

    bool setActiveDocument(const QString& id, QString* error = nullptr);
    bool setReadingPosition(const QString& id, ReadingPosition position, QString* error = nullptr);
    bool setNotes(const QString& id, const QString& notes, QString* error = nullptr);
    bool addBookmark(const QString& id, int page, const QString& label,
                     QString* bookmarkId = nullptr, QString* error = nullptr);
    bool removeBookmark(const QString& id, const QString& bookmarkId, QString* error = nullptr);
    bool addAnnotation(const QString& id, int page, const QString& quote, const QString& note,
                       QString* annotationId = nullptr, QString* error = nullptr);
    bool removeAnnotation(const QString& id, const QString& annotationId, QString* error = nullptr);
    bool exportNotes(const QString& id, const QString& absoluteDestination, QString* error = nullptr) const;
    bool save(QString* error = nullptr);

private:
    Document* edit(const QString& id, QString* error);
    bool load(const QString& root, bool discard, QString* error);
    QString root_;
    QVector<Document> documents_;
    QString activeDocument_;
    QByteArray baselineSha_;
    bool baselineExists_ = false;
    bool dirty_ = false;
    int revision_ = 0;
};

} // namespace literature
