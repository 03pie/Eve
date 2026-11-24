#include "reader_library.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (false)

QByteArray read(const QString& path)
{
    QFile file(path); CHECK(file.open(QIODevice::ReadOnly)); return file.readAll();
}

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); CHECK(file.write(bytes) == bytes.size());
}

struct Scene {
    QTemporaryDir temporary;
    QString source, root, id, error;
    literature::ReaderLibrary library;
    Scene()
    {
        CHECK(temporary.isValid());
        source = temporary.filePath(QString::fromUtf8(u8"文献 𠮷.pdf"));
        root = temporary.filePath(QString::fromUtf8(u8"阅读 数据𠮷"));
        // More than one stream block; no renderer success is asserted by data tests.
        write(source, QByteArray("%PDF-1.7\n") + QByteArray(2 * 1024 * 1024, '%') + "\n%%EOF\n");
        CHECK(library.open(root, &error));
        CHECK(library.importDocument(source, QString::fromUtf8(u8"测试论文"), &id, &error));
    }
    QString index() const { return QDir(root).filePath("library.json"); }
};

void importReopen()
{
    Scene s;
    const auto source = read(s.source);
    const auto path = s.library.verifiedFilePath(s.id, &s.error);
    CHECK(!path.isEmpty() && read(path) == source && s.library.hasUnsavedChanges());
    CHECK(s.library.save(&s.error) && !s.library.hasUnsavedChanges() && s.library.revision() == 1);
    const auto before = read(s.index());
    QString duplicate;
    CHECK(s.library.importDocument(s.source, "duplicate", &duplicate, &s.error) && duplicate == s.id);
    CHECK(s.library.documents().size() == 1 && !s.library.hasUnsavedChanges());
    CHECK(s.library.save(&s.error) && read(s.index()) == before);
    literature::ReaderLibrary reopened;
    CHECK(reopened.open(s.root, &s.error));
    CHECK(reopened.documents().size() == 1 && reopened.activeDocumentId() == s.id && reopened.revision() == 1);
    CHECK(read(reopened.verifiedFilePath(s.id, &s.error)) == source && read(s.source) == source);
    CHECK(!before.contains(s.source.toUtf8())); // Source absolute path is not persisted.
}

void stateReopen()
{
    Scene s; QString bookmark, annotation;
    CHECK(s.library.setReadingPosition(s.id, {17, 1.5, 231}, &s.error));
    CHECK(s.library.setNotes(s.id, QString::fromUtf8(u8"阅读笔记𠮷\n第二行"), &s.error));
    CHECK(s.library.addBookmark(s.id, 17, "method", &bookmark, &s.error));
    CHECK(s.library.addAnnotation(s.id, 17, "quoted passage", "evaluate this", &annotation, &s.error));
    CHECK(s.library.save(&s.error));
    literature::ReaderLibrary reopened; CHECK(reopened.open(s.root, &s.error));
    const auto* d = reopened.document(s.id);
    CHECK(d && d->position.page == 17 && d->position.zoom == 1.5 && d->position.scroll == 231);
    CHECK(d->notes == s.library.document(s.id)->notes && d->bookmarks.size() == 1 && d->annotations.size() == 1);
    CHECK(d->bookmarks.front().id == bookmark && d->annotations.front().id == annotation);
    CHECK(reopened.removeBookmark(s.id, bookmark, &s.error));
    CHECK(reopened.removeAnnotation(s.id, annotation, &s.error) && reopened.save(&s.error));
    CHECK(s.library.reloadDiscardingChanges(&s.error) && s.library.document(s.id)->bookmarks.isEmpty() &&
          s.library.document(s.id)->annotations.isEmpty());
}

void conflict()
{
    Scene s; CHECK(s.library.save(&s.error));
    literature::ReaderLibrary second; CHECK(second.open(s.root, &s.error));
    CHECK(s.library.setNotes(s.id, "first writer", &s.error) && s.library.save(&s.error));
    const auto before = read(s.index());
    CHECK(second.setNotes(s.id, "second draft", &s.error));
    CHECK(!second.save(&s.error) && !s.error.isEmpty() && second.hasUnsavedChanges() && second.revision() == 1);
    CHECK(read(s.index()) == before && second.document(s.id)->notes == "second draft");
    CHECK(!second.open(s.root, &s.error) && second.hasUnsavedChanges());
    CHECK(second.reloadDiscardingChanges(&s.error) && second.document(s.id)->notes == "first writer");
}

void lockedRetry()
{
    Scene s; CHECK(s.library.save(&s.error));
    const auto before = read(s.index());
    CHECK(s.library.setNotes(s.id, "retained edit", &s.error));
    QLockFile lock(QDir(s.root).filePath("library.lock")); lock.setStaleLockTime(0); CHECK(lock.tryLock());
    CHECK(!s.library.save(&s.error) && s.library.hasUnsavedChanges() && read(s.index()) == before);
    CHECK(s.library.revision() == 1 && s.library.document(s.id)->notes == "retained edit");
    lock.unlock();
    CHECK(s.library.save(&s.error) && !s.library.hasUnsavedChanges() && s.library.revision() == 2);
}

void badMetadata()
{
    Scene s; CHECK(s.library.save(&s.error));
    const auto original = read(s.index());
    const auto mutate = [&](const std::function<void(QJsonObject&)>& action) {
        auto json = QJsonDocument::fromJson(original).object(); action(json);
        const auto bad = QJsonDocument(json).toJson(); write(s.index(), bad);
        CHECK(!s.library.open(s.root, &s.error));
        CHECK(s.library.documents().size() == 1 && s.library.document(s.id) && s.library.revision() == 1);
        CHECK(read(s.index()) == bad);
    };
    mutate([](QJsonObject& o) { o["schema"] = "future/2"; });
    mutate([](QJsonObject& o) { auto a = o["documents"].toArray(); a.append(a.first()); o["documents"] = a; });
    mutate([](QJsonObject& o) { auto a = o["documents"].toArray(); auto d = a.first().toObject(); d["sha256"] = "../../escape"; a[0] = d; o["documents"] = a; });
    mutate([](QJsonObject& o) { o["revision"] = true; });
    mutate([](QJsonObject& o) { o["unknownFutureField"] = "preserve by rejection"; });
    write(s.index(), "not json");
    CHECK(!s.library.open(s.root, &s.error) && read(s.index()) == "not json");
    write(s.index(), original); CHECK(s.library.open(s.root, &s.error));
}

void importFailures()
{
    Scene s; CHECK(s.library.save(&s.error)); const auto before = read(s.index());
    const auto bad = s.temporary.filePath("invalid.pdf"); write(bad, "plain text");
    CHECK(!s.library.importDocument(bad, "bad", nullptr, &s.error));
    CHECK(s.library.documents().size() == 1 && !s.library.hasUnsavedChanges() && read(s.index()) == before);
    const auto path = s.library.verifiedFilePath(s.id, &s.error);
    auto bytes = read(path); bytes[bytes.size() - 1] = '!'; write(path, bytes);
    CHECK(s.library.verifiedFilePath(s.id, &s.error).isEmpty() && !s.error.isEmpty());
    CHECK(!s.library.importDocument(s.source, "duplicate damaged object", nullptr, &s.error));
    CHECK(read(path) == bytes && read(s.index()) == before && !s.library.hasUnsavedChanges());
}

void workerPreparation()
{
    Scene s; CHECK(s.library.save(&s.error));
    std::atomic_bool cancelled{true};
    CHECK(!literature::ReaderLibrary::prepareImport(s.root, s.source, "cancel", &cancelled, &s.error));
    const auto snapshot = *s.library.document(s.id);
    CHECK(literature::ReaderLibrary::verifiedFilePathFor(s.library.rootPath(), snapshot, &cancelled, &s.error).isEmpty());
    cancelled.store(false);
    auto worker = std::async(std::launch::async, [&] {
        return literature::ReaderLibrary::prepareImport(s.root, s.source, "worker", &cancelled, &s.error);
    });
    auto prepared = worker.get();
    CHECK(prepared && !s.library.hasUnsavedChanges());
    literature::ReaderLibrary other; CHECK(other.open(s.temporary.filePath("other"), &s.error));
    CHECK(!other.adoptPreparedImport(*prepared, nullptr, &s.error) && other.documents().isEmpty());
    QString id; CHECK(s.library.adoptPreparedImport(*prepared, &id, &s.error) && id == s.id);
    CHECK(!literature::ReaderLibrary::verifiedFilePathFor(s.library.rootPath(), snapshot, nullptr, &s.error).isEmpty());
}

void readonlyRetry()
{
    Scene s; CHECK(s.library.save(&s.error)); const auto before = read(s.index());
    CHECK(s.library.setNotes(s.id, "save after restoring file permission", &s.error));
    const auto permissions = QFile::permissions(s.index());
    CHECK(QFile::setPermissions(s.index(), QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther));
    const auto saved = s.library.save(&s.error);
    const auto after = read(s.index());
    CHECK(QFile::setPermissions(s.index(), permissions));
    CHECK(!saved && !s.error.isEmpty() && after == before && s.library.hasUnsavedChanges() && s.library.revision() == 1);
    CHECK(s.library.document(s.id)->notes == "save after restoring file permission");
    CHECK(s.library.save(&s.error) && s.library.revision() == 2 && !s.library.hasUnsavedChanges());
}

void invalidEdits()
{
    Scene s; CHECK(s.library.save(&s.error)); const auto before = read(s.index());
    CHECK(!s.library.setReadingPosition(s.id, {-1, 1.0, 0}, &s.error));
    CHECK(!s.library.setReadingPosition(s.id, {0, 0.0, 0}, &s.error));
    CHECK(!s.library.addBookmark(s.id, -1, "bad", nullptr, &s.error));
    CHECK(!s.library.addAnnotation(s.id, -1, "", "bad", nullptr, &s.error));
    CHECK(!s.library.setNotes(s.id, QString(256 * 1024 + 1, 'x'), &s.error));
    CHECK(!s.library.removeBookmark(s.id, "absent", &s.error));
    CHECK(!s.library.removeAnnotation("absent", "absent", &s.error));
    CHECK(!s.library.setActiveDocument("absent", &s.error));
    CHECK(!s.library.hasUnsavedChanges() && read(s.index()) == before);
}

void exportNotes()
{
    Scene s; CHECK(s.library.save(&s.error));
    const auto index = read(s.index()), original = read(s.source);
    CHECK(s.library.setNotes(s.id, QString::fromUtf8(u8"导出笔记𠮷"), &s.error));
    CHECK(s.library.addBookmark(s.id, 4, "bookmark", nullptr, &s.error));
    CHECK(s.library.addAnnotation(s.id, 4, "quote", "comment", nullptr, &s.error));
    CHECK(!s.library.exportNotes(s.id, s.index(), &s.error));
    CHECK(!s.library.exportNotes(s.id, s.source, &s.error));
    const auto disguised = s.temporary.filePath("paper.md"); write(disguised, original);
    CHECK(!s.library.exportNotes(s.id, disguised, &s.error) && read(disguised) == original);
    const auto output = s.temporary.filePath(QString::fromUtf8(u8"导出 notes𠮷.md"));
    CHECK(s.library.exportNotes(s.id, output, &s.error));
    const auto exported = read(output);
    CHECK(exported.contains(QString::fromUtf8(u8"导出笔记𠮷").toUtf8()) && exported.contains("Page 5") &&
          exported.contains("bookmark") && exported.contains("quote") && exported.contains("comment"));
    CHECK(s.library.hasUnsavedChanges() && s.library.revision() == 1 && read(s.index()) == index && read(s.source) == original);
    CHECK(!s.library.exportNotes(s.id, "relative.md", &s.error));
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        const QString name = QString::fromLocal8Bit(argv[1]);
        if (name == "import_reopen") importReopen();
        else if (name == "state_reopen") stateReopen();
        else if (name == "conflict") conflict();
        else if (name == "locked_retry") lockedRetry();
        else if (name == "bad_metadata") badMetadata();
        else if (name == "import_failures") importFailures();
        else if (name == "worker_preparation") workerPreparation();
        else if (name == "invalid_edits") invalidEdits();
        else if (name == "readonly_retry") readonlyRetry();
        else if (name == "export_notes") exportNotes();
        else CHECK(false);
        std::cout << name.toStdString() << ": passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
