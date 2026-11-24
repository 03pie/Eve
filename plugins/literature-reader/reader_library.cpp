#include "reader_library.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryFile>
#include <QUuid>
#include <cmath>
#include <limits>

namespace literature {
namespace {
constexpr qint64 maxDocumentBytes = 512LL * 1024 * 1024;
constexpr qint64 maxIndexBytes = 32LL * 1024 * 1024;
constexpr int maxDocuments = 4096;
constexpr int maxMarks = 4096;
constexpr int maxPage = 1000000;
constexpr int maxNote = 256 * 1024;
const QString schema = QStringLiteral("org.cordis.literature-reader.library/1");

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool keys(const QJsonObject& object, std::initializer_list<const char*> expected)
{
    QSet<QString> fields;
    for (const auto* name : expected) fields.insert(QString::fromLatin1(name));
    const auto actual = object.keys();
    return fields == QSet<QString>(actual.begin(), actual.end());
}

bool integer(const QJsonValue& value, int maximum, int* result)
{
    if (!value.isDouble()) return false;
    const auto number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > maximum || std::floor(number) != number) return false;
    *result = static_cast<int>(number);
    return true;
}

bool uuid(const QString& value)
{
    const QUuid id(value);
    return !id.isNull() && id.toString(QUuid::WithoutBraces) == value;
}

bool text(const QJsonValue& value, int maximum)
{
    return value.isString() && value.toString().size() <= maximum && !value.toString().contains(QChar::Null);
}

QString objectPath(const QString& root, const Document& document)
{
    return QDir(root).filePath(QStringLiteral("objects/") + document.sha256 + QStringLiteral(".pdf"));
}

bool readIndex(const QString& root, QByteArray* bytes, bool* exists, QString* error)
{
    const auto path = QDir(root).filePath(QStringLiteral("library.json"));
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isFile())) return fail(error, QStringLiteral("Library index is not an ordinary file."));
    *exists = info.exists();
    bytes->clear();
    if (!*exists) return true;
    if (info.size() > maxIndexBytes) return fail(error, QStringLiteral("Library metadata exceeds 32 MiB."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    *bytes = file.read(maxIndexBytes + 1);
    return (file.error() == QFile::NoError && bytes->size() == info.size() && bytes->size() <= maxIndexBytes)
               || fail(error, QStringLiteral("Library index could not be read completely."));
}

bool verifyObject(const QString& root, const Document& document, bool fullHash, QString* error, const std::atomic_bool* cancelled = nullptr)
{
    const QFileInfo directory(QDir(root).filePath(QStringLiteral("objects")));
    const QFileInfo info(objectPath(root, document));
    if (!directory.isDir() || directory.isSymLink() || !info.isFile() || info.isSymLink() || info.size() != document.bytes)
        return fail(error, QStringLiteral("Imported PDF is missing, linked or has a different length."));
    if (!fullHash) return true;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 count = 0;
    while (!file.atEnd()) {
        if (cancelled && cancelled->load()) return fail(error, QStringLiteral("PDF verification cancelled."));
        const auto block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) return fail(error, file.errorString());
        count += block.size();
        if (count > document.bytes) return fail(error, QStringLiteral("Imported PDF changed while reading."));
        hash.addData(block);
    }
    return (file.error() == QFile::NoError && count == document.bytes &&
            QString::fromLatin1(hash.result().toHex()) == document.sha256)
               || fail(error, QStringLiteral("Imported PDF SHA256 differs; original bytes were preserved."));
}

QJsonObject positionJson(const ReadingPosition& p)
{
    return {{QStringLiteral("page"), p.page}, {QStringLiteral("zoom"), p.zoom}, {QStringLiteral("scroll"), p.scroll}};
}

bool validPosition(ReadingPosition p)
{
    return p.page >= 0 && p.page <= maxPage && p.scroll >= 0 && std::isfinite(p.zoom) && p.zoom >= 0.1 && p.zoom <= 8.0;
}

bool parseDocument(const QJsonValue& value, Document* result)
{
    if (!value.isObject()) return false;
    const auto o = value.toObject();
    if (!keys(o, {"id", "title", "originalFileName", "sha256", "bytes", "position", "bookmarks", "annotations", "notes"}) ||
        !text(o["id"], 36) || !uuid(o["id"].toString()) || !text(o["title"], 512) || o["title"].toString().trimmed().isEmpty() ||
        !text(o["originalFileName"], 1024) || !text(o["sha256"], 64) || !text(o["notes"], maxNote) || !o["bytes"].isString()) return false;
    Document document;
    document.id = o["id"].toString(); document.title = o["title"].toString();
    document.originalFileName = o["originalFileName"].toString(); document.sha256 = o["sha256"].toString();
    bool ok = false;
    document.bytes = o["bytes"].toString().toLongLong(&ok);
    static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
    if (!ok || document.bytes <= 0 || document.bytes > maxDocumentBytes || QString::number(document.bytes) != o["bytes"].toString() ||
        !hash.match(document.sha256).hasMatch() || document.originalFileName.isEmpty() ||
        document.originalFileName.contains('/') || document.originalFileName.contains('\\') || document.originalFileName == "." ||
        document.originalFileName == ".." || !o["position"].isObject()) return false;
    const auto p = o["position"].toObject();
    if (!keys(p, {"page", "zoom", "scroll"}) || !integer(p["page"], maxPage, &document.position.page) ||
        !integer(p["scroll"], std::numeric_limits<int>::max(), &document.position.scroll) || !p["zoom"].isDouble()) return false;
    document.position.zoom = p["zoom"].toDouble();
    if (!validPosition(document.position) || !o["bookmarks"].isArray() || !o["annotations"].isArray() ||
        o["bookmarks"].toArray().size() > maxMarks || o["annotations"].toArray().size() > maxMarks) return false;
    QSet<QString> ids;
    for (const auto entry : o["bookmarks"].toArray()) {
        if (!entry.isObject()) return false;
        const auto mark = entry.toObject();
        Bookmark b;
        if (!keys(mark, {"id", "page", "label"}) || !text(mark["id"], 36) || !uuid(mark["id"].toString()) ||
            !text(mark["label"], 1024) || !integer(mark["page"], maxPage, &b.page) || ids.contains(mark["id"].toString())) return false;
        b.id = mark["id"].toString(); b.label = mark["label"].toString(); ids.insert(b.id); document.bookmarks.push_back(b);
    }
    for (const auto entry : o["annotations"].toArray()) {
        if (!entry.isObject()) return false;
        const auto mark = entry.toObject();
        Annotation a;
        if (!keys(mark, {"id", "page", "quote", "note"}) || !text(mark["id"], 36) || !uuid(mark["id"].toString()) ||
            !text(mark["quote"], maxNote) || !text(mark["note"], maxNote) || !integer(mark["page"], maxPage, &a.page) ||
            ids.contains(mark["id"].toString())) return false;
        a.id = mark["id"].toString(); a.quote = mark["quote"].toString(); a.note = mark["note"].toString();
        ids.insert(a.id); document.annotations.push_back(a);
    }
    document.notes = o["notes"].toString();
    *result = std::move(document);
    return true;
}

QByteArray serialize(const QVector<Document>& documents, const QString& active, int revision)
{
    QJsonArray entries;
    for (const auto& d : documents) {
        QJsonArray bookmarks, annotations;
        for (const auto& b : d.bookmarks) bookmarks.append(QJsonObject{{"id", b.id}, {"page", b.page}, {"label", b.label}});
        for (const auto& a : d.annotations) annotations.append(QJsonObject{{"id", a.id}, {"page", a.page}, {"quote", a.quote}, {"note", a.note}});
        entries.append(QJsonObject{{"id", d.id}, {"title", d.title}, {"originalFileName", d.originalFileName},
                                   {"sha256", d.sha256}, {"bytes", QString::number(d.bytes)}, {"position", positionJson(d.position)},
                                   {"bookmarks", bookmarks}, {"annotations", annotations}, {"notes", d.notes}});
    }
    return QJsonDocument(QJsonObject{{"schema", schema}, {"revision", revision}, {"activeDocument", active}, {"documents", entries}})
        .toJson(QJsonDocument::Indented);
}
} // namespace

bool ReaderLibrary::open(const QString& root, QString* error) { return load(root, false, error); }

bool ReaderLibrary::reloadDiscardingChanges(QString* error)
{
    if (root_.isEmpty()) return fail(error, QStringLiteral("No library is open."));
    return load(root_, true, error);
}

bool ReaderLibrary::load(const QString& root, bool discard, QString* error)
{
    if (dirty_ && !discard) return fail(error, QStringLiteral("Unsaved changes: save or explicitly reload discarding changes."));
    if (!QDir::isAbsolutePath(root) || QDir(root).isRoot()) return fail(error, QStringLiteral("Choose an absolute workspace directory, not a volume root."));
    const QFileInfo rootInfo(root);
    if (rootInfo.isSymLink() || (rootInfo.exists() && !rootInfo.isDir())) return fail(error, QStringLiteral("Workspace must be an ordinary directory."));
    if (!QDir().mkpath(root)) return fail(error, QStringLiteral("Cannot create the chosen workspace."));
    const auto canonical = QFileInfo(root).canonicalFilePath();
    if (canonical.isEmpty()) return fail(error, QStringLiteral("Workspace disappeared while opening."));
    QByteArray bytes; bool exists = false;
    if (!readIndex(canonical, &bytes, &exists, error)) return false;
    QVector<Document> documents; QString active; int revision = 0;
    if (exists) {
        QJsonParseError parse;
        const auto json = QJsonDocument::fromJson(bytes, &parse);
        const auto o = json.object();
        if (parse.error != QJsonParseError::NoError || !json.isObject() || !keys(o, {"schema", "revision", "activeDocument", "documents"}) ||
            o["schema"] != schema || !integer(o["revision"], std::numeric_limits<int>::max(), &revision) ||
            !o["activeDocument"].isString() || !o["documents"].isArray() || o["documents"].toArray().size() > maxDocuments)
            return fail(error, QStringLiteral("Unsupported or malformed library metadata; file was not replaced."));
        QSet<QString> ids, hashes;
        for (const auto entry : o["documents"].toArray()) {
            Document document;
            if (!parseDocument(entry, &document) || ids.contains(document.id) || hashes.contains(document.sha256))
                return fail(error, QStringLiteral("Invalid or duplicate document metadata."));
            if (!verifyObject(canonical, document, false, error)) return false;
            ids.insert(document.id); hashes.insert(document.sha256); documents.push_back(std::move(document));
        }
        active = o["activeDocument"].toString();
        if (!active.isEmpty() && !ids.contains(active)) return fail(error, QStringLiteral("Active document is absent."));
    }
    root_ = canonical; documents_ = std::move(documents); activeDocument_ = active; revision_ = revision;
    baselineExists_ = exists; baselineSha_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); dirty_ = false;
    if (error) error->clear();
    return true;
}

const Document* ReaderLibrary::document(const QString& id) const
{
    for (const auto& d : documents_) if (d.id == id) return &d;
    return nullptr;
}

Document* ReaderLibrary::edit(const QString& id, QString* error)
{
    for (auto& d : documents_) if (d.id == id) return &d;
    fail(error, QStringLiteral("Document does not exist."));
    return nullptr;
}

std::optional<PreparedImport> ReaderLibrary::prepareImport(const QString& root, const QString& source, const QString& title,
                                                         const std::atomic_bool* cancelled, QString* error)
{
    const auto reject = [error](const QString& message) -> std::optional<PreparedImport> { fail(error, message); return std::nullopt; };
    const auto rootPath = QFileInfo(root).canonicalFilePath();
    if (!QDir::isAbsolutePath(root) || rootPath.isEmpty() || !QFileInfo(root).isDir() || QFileInfo(root).isSymLink())
        return reject(QStringLiteral("Open an ordinary workspace before importing."));
    if (cancelled && cancelled->load()) return reject(QStringLiteral("PDF import cancelled."));
    const QFileInfo initial(source);
    const auto name = title.trimmed().isEmpty() ? initial.completeBaseName() : title.trimmed();
    if (!initial.isFile() || initial.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0 ||
        initial.size() <= 0 || initial.size() > maxDocumentBytes || name.isEmpty() || name.size() > 512 || name.contains(QChar::Null))
        return reject(QStringLiteral("Select a PDF up to 512 MiB with a nonempty title of at most 512 characters."));
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly)) return reject(input.errorString());
    static const QRegularExpression header(QStringLiteral("^%PDF-[12]\\.[0-9]"));
    if (!header.match(QString::fromLatin1(input.peek(8))).hasMatch()) return reject(QStringLiteral("PDF header is missing; source was not modified."));
    const auto directory = QDir(rootPath).filePath(QStringLiteral("objects"));
    if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) return reject(QStringLiteral("Cannot create ordinary PDF object directory."));
    QTemporaryFile copy(QDir(directory).filePath(QStringLiteral(".import-XXXXXX")));
    if (!copy.open()) return reject(copy.errorString());
    QCryptographicHash hash(QCryptographicHash::Sha256); qint64 total = 0;
    while (!input.atEnd()) {
        if (cancelled && cancelled->load()) return reject(QStringLiteral("PDF import cancelled."));
        const auto block = input.read(1024 * 1024);
        if ((block.isEmpty() && input.error() != QFile::NoError) || copy.write(block) != block.size()) return reject(QStringLiteral("PDF copy failed; import was not added."));
        total += block.size();
        if (total > maxDocumentBytes) return reject(QStringLiteral("PDF grew beyond the size limit during import."));
        hash.addData(block);
    }
    const QFileInfo after(source);
    if (input.error() != QFile::NoError || total != initial.size() || after.size() != initial.size() ||
        after.lastModified() != initial.lastModified() || !copy.flush()) return reject(QStringLiteral("PDF changed or could not be copied completely."));
    Document d;
    d.id = QUuid::createUuid().toString(QUuid::WithoutBraces); d.title = name; d.originalFileName = initial.fileName();
    d.sha256 = QString::fromLatin1(hash.result().toHex()); d.bytes = total;
    if (cancelled && cancelled->load()) return reject(QStringLiteral("PDF import cancelled."));
    copy.close();
    const auto destination = objectPath(rootPath, d);
    if (!QFileInfo::exists(destination)) {
        if (!copy.rename(destination) && !QFileInfo::exists(destination)) return reject(copy.errorString());
        if (copy.fileName() == destination) copy.setAutoRemove(false);
    }
    if (!verifyObject(rootPath, d, true, error, cancelled)) return std::nullopt;
    PreparedImport prepared;
    prepared.root_ = rootPath; prepared.document_ = d;
    prepared.modifiedMSecs_ = QFileInfo(destination).lastModified().toMSecsSinceEpoch();
    if (error) error->clear();
    return prepared;
}

bool ReaderLibrary::importDocument(const QString& source, const QString& title, QString* documentId, QString* error)
{
    auto prepared = prepareImport(root_, source, title, nullptr, error);
    return prepared && adoptPreparedImport(std::move(*prepared), documentId, error);
}

bool ReaderLibrary::adoptPreparedImport(PreparedImport prepared, QString* documentId, QString* error)
{
    const auto& d = prepared.document_;
    if (root_.isEmpty() || prepared.root_ != root_ ||
        QFileInfo(objectPath(root_, d)).lastModified().toMSecsSinceEpoch() != prepared.modifiedMSecs_ ||
        !verifyObject(root_, d, false, error)) return fail(error, QStringLiteral("Prepared import belongs to another workspace or has changed."));
    for (const auto& existing : documents_) if (existing.sha256 == d.sha256) {
        if (documentId) *documentId = existing.id;
        if (error) error->clear();
        return true;
    }
    if (documents_.size() >= maxDocuments) return fail(error, QStringLiteral("Library document limit reached."));
    documents_.push_back(d); activeDocument_ = d.id; dirty_ = true;
    if (documentId) *documentId = d.id;
    if (error) error->clear();
    return true;
}

QString ReaderLibrary::verifiedFilePathFor(const QString& root, const Document& document,
                                           const std::atomic_bool* cancelled, QString* error)
{
    Document checked;
    const auto encoded = QJsonDocument::fromJson(serialize({document}, document.id, 0)).object()["documents"].toArray();
    if (QFileInfo(root).canonicalFilePath() != root || !parseDocument(encoded.first(), &checked) ||
        !verifyObject(root, checked, true, error, cancelled)) {
        if (error && error->isEmpty()) *error = QStringLiteral("Invalid document snapshot or workspace.");
        return {};
    }
    if (error) error->clear();
    return objectPath(root, checked);
}

QString ReaderLibrary::verifiedFilePath(const QString& id, QString* error) const
{
    const auto* d = document(id);
    if (!d) { fail(error, QStringLiteral("Document does not exist.")); return {}; }
    return verifiedFilePathFor(root_, *d, nullptr, error);
}

bool ReaderLibrary::setActiveDocument(const QString& id, QString* error)
{
    if (!document(id)) return fail(error, QStringLiteral("Document does not exist."));
    if (activeDocument_ != id) { activeDocument_ = id; dirty_ = true; }
    if (error) error->clear();
    return true;
}

bool ReaderLibrary::setReadingPosition(const QString& id, ReadingPosition position, QString* error)
{
    auto* d = edit(id, error);
    if (!d) return false;
    if (!validPosition(position)) return fail(error, QStringLiteral("Reading position is out of range."));
    if (d->position.page != position.page || d->position.zoom != position.zoom || d->position.scroll != position.scroll) {
        d->position = position; dirty_ = true;
    }
    if (error) error->clear();
    return true;
}

bool ReaderLibrary::setNotes(const QString& id, const QString& notes, QString* error)
{
    auto* d = edit(id, error);
    if (!d) return false;
    if (notes.size() > maxNote || notes.contains(QChar::Null)) return fail(error, QStringLiteral("Notes exceed the 256 Ki character limit."));
    if (d->notes != notes) { d->notes = notes; dirty_ = true; }
    if (error) error->clear();
    return true;
}

bool ReaderLibrary::addBookmark(const QString& id, int page, const QString& label, QString* bookmarkId, QString* error)
{
    auto* d = edit(id, error);
    if (!d) return false;
    if (page < 0 || page > maxPage || label.size() > 1024 || label.contains(QChar::Null) || d->bookmarks.size() >= maxMarks)
        return fail(error, QStringLiteral("Bookmark is out of range."));
    Bookmark b{QUuid::createUuid().toString(QUuid::WithoutBraces), page, label};
    d->bookmarks.push_back(b); dirty_ = true;
    if (bookmarkId) *bookmarkId = b.id;
    if (error) error->clear();
    return true;
}

bool ReaderLibrary::removeBookmark(const QString& id, const QString& bookmarkId, QString* error)
{
    auto* d = edit(id, error);
    if (!d) return false;
    for (int i = 0; i < d->bookmarks.size(); ++i) if (d->bookmarks[i].id == bookmarkId) {
        d->bookmarks.removeAt(i); dirty_ = true; if (error) error->clear(); return true;
    }
    return fail(error, QStringLiteral("Bookmark does not exist."));
}

bool ReaderLibrary::addAnnotation(const QString& id, int page, const QString& quote, const QString& note,
                                  QString* annotationId, QString* error)
{
    auto* d = edit(id, error);
    if (!d) return false;
    if (page < 0 || page > maxPage || quote.size() > maxNote || note.size() > maxNote ||
        quote.contains(QChar::Null) || note.contains(QChar::Null) || d->annotations.size() >= maxMarks)
        return fail(error, QStringLiteral("Annotation is out of range."));
    Annotation a{QUuid::createUuid().toString(QUuid::WithoutBraces), page, quote, note};
    d->annotations.push_back(a); dirty_ = true;
    if (annotationId) *annotationId = a.id;
    if (error) error->clear();
    return true;
}

bool ReaderLibrary::removeAnnotation(const QString& id, const QString& annotationId, QString* error)
{
    auto* d = edit(id, error);
    if (!d) return false;
    for (int i = 0; i < d->annotations.size(); ++i) if (d->annotations[i].id == annotationId) {
        d->annotations.removeAt(i); dirty_ = true; if (error) error->clear(); return true;
    }
    return fail(error, QStringLiteral("Annotation does not exist."));
}

bool ReaderLibrary::exportNotes(const QString& id, const QString& absoluteDestination, QString* error) const
{
    const auto* d = document(id);
    if (!d) return fail(error, QStringLiteral("Document does not exist."));
    const QFileInfo target(absoluteDestination);
    for (auto ancestor = target.absolutePath();;) {
        const QFileInfo info(ancestor);
        if (info.isSymLink()) return fail(error, QStringLiteral("Notes export cannot follow a linked directory."));
        const auto next = info.dir().absolutePath();
        if (next == ancestor) break;
        ancestor = next;
    }
    const auto parent = QFileInfo(target.absolutePath()).canonicalFilePath();
    const auto path = QDir(parent).filePath(target.fileName());
    static const QRegularExpression reserved(QStringLiteral("^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\..*)?$"),
                                              QRegularExpression::CaseInsensitiveOption);
    const auto sensitivity = Qt::CaseInsensitive; // Windows workspace/aliases must not bypass this boundary.
    if (!QDir::isAbsolutePath(absoluteDestination) || parent.isEmpty() || target.fileName().isEmpty() ||
        target.isSymLink() || (target.exists() && !target.isFile()) || reserved.match(target.fileName()).hasMatch() ||
        target.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0 ||
        path.compare(root_, sensitivity) == 0 || path.startsWith(root_ + '/', sensitivity))
        return fail(error, QStringLiteral("Choose an ordinary notes file outside the library, never a PDF."));
    if (target.exists()) {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadOnly)) return fail(error, existing.errorString());
        const auto header = existing.read(1024);
        if (existing.error() != QFile::NoError || header.contains("%PDF-"))
            return fail(error, QStringLiteral("Existing PDF or unreadable destination was preserved."));
    }
    QString output = QStringLiteral("# ") + d->title + QStringLiteral("\n\n## Notes\n\n") + d->notes + QStringLiteral("\n\n## Bookmarks\n");
    for (const auto& bookmark : d->bookmarks)
        output += QStringLiteral("\n- Page %1: %2\n").arg(bookmark.page + 1).arg(bookmark.label);
    output += QStringLiteral("\n## Annotations\n");
    for (const auto& annotation : d->annotations)
        output += QStringLiteral("\n### Page %1\n\n%2\n\n%3\n").arg(annotation.page + 1).arg(annotation.quote, annotation.note);
    const auto bytes = output.toUtf8();
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("Notes export failed: ") + file.errorString());
    if (error) error->clear();
    return true;
}

bool ReaderLibrary::save(QString* error)
{
    if (root_.isEmpty()) return fail(error, QStringLiteral("No library is open."));
    if (!dirty_) { if (error) error->clear(); return true; }
    if (revision_ == std::numeric_limits<int>::max()) return fail(error, QStringLiteral("Library revision limit reached."));
    const auto lockPath = QDir(root_).filePath(QStringLiteral("library.lock"));
    if (QFileInfo(lockPath).isSymLink()) return fail(error, QStringLiteral("Library lock cannot be a link."));
    QLockFile lock(lockPath); lock.setStaleLockTime(0);
    if (!lock.tryLock()) return fail(error, QStringLiteral("Library is being saved by another instance; retry after it finishes."));
    QByteArray current; bool exists = false;
    if (!readIndex(root_, &current, &exists, error)) return false;
    if (exists != baselineExists_ || QCryptographicHash::hash(current, QCryptographicHash::Sha256) != baselineSha_)
        return fail(error, QStringLiteral("Library changed in another instance. Unsaved edits remain; explicitly reload to discard them."));
    for (const auto& d : documents_) if (!verifyObject(root_, d, false, error)) return false;
    const auto bytes = serialize(documents_, activeDocument_, revision_ + 1);
    if (bytes.size() > maxIndexBytes) return fail(error, QStringLiteral("Library metadata exceeds 32 MiB; unsaved edits remain."));
    QSaveFile output(QDir(root_).filePath(QStringLiteral("library.json")));
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
        return fail(error, QStringLiteral("Library save failed: ") + output.errorString());
    baselineSha_ = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); baselineExists_ = true;
    ++revision_; dirty_ = false;
    if (error) error->clear();
    return true;
}
} // namespace literature
