#include "workbench.hpp"
#include <ElaToggleSwitch.h>
#include <ElaWindow.h>
#include <QApplication>
#include <QAction>
#include <QAbstractButton>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPageSize>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>

#define P7_REQUIRE(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

namespace {
using cordis::workbench::Workbench;
const QString seedNote = QString::fromUtf8(u8"P7 阅读笔记：跨进程保留。\nA reproducible PDF reading session.");
const QString continuedNote = seedNote + QString::fromUtf8(u8"\n新版继续阅读并保存。");

QByteArray read(const QString& path)
{
    QFile file(path);
    P7_REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
QString sha(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QJsonObject json(const QString& path)
{
    QJsonParseError error;
    const auto value = QJsonDocument::fromJson(read(path), &error);
    P7_REQUIRE(error.error == QJsonParseError::NoError && value.isObject());
    return value.object();
}
void writeJson(const QString& path, const QJsonObject& value)
{
    QFile file(path);
    P7_REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
    P7_REQUIRE(file.write(bytes) == bytes.size());
}
void until(const std::function<bool()>& predicate, int timeout = 30000)
{
    if (predicate()) return;
    QEventLoop loop;
    QTimer poll, deadline;
    deadline.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (predicate()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    deadline.start(timeout);
    loop.exec();
    P7_REQUIRE(predicate());
}
template<class T> T take(cordis::Result<T> result)
{
    if (!result.hasValue()) throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
template<class T> typename cordis::Task<T>::Snapshot wait(const cordis::Task<T>& task,
                                                        const std::shared_ptr<cordis::qt::QtScheduler>& scheduler)
{
    auto delivered = std::make_shared<bool>(false);
    auto receipt = task.thenOn(scheduler, [delivered](const cordis::Result<T>&) { *delivered = true; });
    until([&] { return receipt.isReady(); });
    P7_REQUIRE(*delivered && receipt.tryResult()->hasValue());
    P7_REQUIRE(task.tryResult());
    return task.tryResult();
}

struct Scene {
    Workbench workbench;
    bool closed = false;
    ~Scene()
    {
        if (!closed) {
            try { close(); }
            catch (const std::exception& error) { std::cerr << "normal cleanup failed: " << error.what() << '\n'; }
        }
    }
    void open(const QString& manifest, const QString& workspace, bool publish = true)
    {
        workbench.open(manifest, workspace);
        if (publish) workbench.publishUi();
        const auto accepted = workbench.applyInitial();
        const auto result = wait(accepted.settled, workbench.scheduler);
        P7_REQUIRE(result->hasValue());
        P7_REQUIRE(result->value().outcome == (publish ? cordis::loader::PlanOutcome::Applied : cordis::loader::PlanOutcome::Pending));
    }
    void close()
    {
        P7_REQUIRE(wait(workbench.close(), workbench.scheduler)->hasValue());
        P7_REQUIRE(!workbench.kernel && !workbench.session);
        P7_REQUIRE(workbench.scheduler->close().hasValue());
        const auto snapshot = take(workbench.scheduler->snapshot());
        P7_REQUIRE(!snapshot.open && snapshot.queued == 0 && snapshot.callbackErrors == 0);
        closed = true;
    }
};

QList<QWidget*> pages()
{
    QList<QWidget*> result;
    for (auto* widget : QApplication::allWidgets())
        if (widget->objectName() == QStringLiteral("LiteratureReaderPage")) result.push_back(widget);
    std::sort(result.begin(), result.end(), [](QWidget* left, QWidget* right) {
        return left->property("cordis.instance").toULongLong() < right->property("cordis.instance").toULongLong();
    });
    return result;
}
QWidget* onlyPage()
{
    until([] { return pages().size() == 1; });
    return pages().front();
}
template<class T> T* control(QWidget* page, const char* name)
{
    auto* value = page->findChild<T*>(QString::fromLatin1(name));
    P7_REQUIRE(value);
    return value;
}
void click(QWidget* page, const char* name)
{
    auto* button = control<QAbstractButton>(page, name);
    P7_REQUIRE(button->isEnabled());
    button->click();
}
void idle(QWidget* page)
{
    until([page] { return !page->property("readerBusy").toBool(); });
}
void pdf(const QString& path)
{
    QPdfWriter writer(path);
    writer.setTitle(QStringLiteral("P7 reproducible literature fixture"));
    writer.setResolution(96);
    writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);
    P7_REQUIRE(painter.isActive());
    for (int page = 0; page < 3; ++page) {
        if (page) P7_REQUIRE(writer.newPage());
        painter.fillRect(QRect(35, 35, 700, 130), QColor::fromHsv(40 + page * 85, 90, 245));
        painter.setPen(Qt::black);
        painter.setFont(QFont(QStringLiteral("Arial"), 26));
        painter.drawText(QRect(50, 60, 650, 90), QStringLiteral("P7 literature test - page %1").arg(page + 1));
        painter.setFont(QFont(QStringLiteral("Arial"), 14));
        painter.drawText(QRect(50, 220, 650, 300), Qt::TextWordWrap,
                         QStringLiteral("A real three-page PDF generated by QPdfWriter. Page %1 has independent content, colour and a visible reading target.").arg(page + 1));
        painter.drawRect(QRect(60 + page * 50, 600, 140 + page * 65, 80));
    }
    painter.end();
}
void importPdf(QWidget* page, const QString& path, int expectedPages = 3)
{
    control<QLineEdit>(page, "ReaderImportPath")->setText(path);
    click(page, "ReaderImport");
    try {
        until([page, expectedPages] {
            const auto count = page->property("readerPageCount").toInt();
            const bool rendered = (expectedPages ? count == expectedPages : count >= 2) &&
                                  !page->property("readerDocumentId").toString().isEmpty();
            const bool rejected = control<QLabel>(page, "ReaderStatus")->text().startsWith(QString::fromUtf8(u8"操作失败"));
            return !page->property("readerBusy").toBool() && (rendered || rejected);
        });
    } catch (...) {
        std::cerr << "import/render timeout: " << control<QLabel>(page, "ReaderStatus")->text().toUtf8().constData()
                  << " busy=" << page->property("readerBusy").toBool()
                  << " count=" << page->property("readerPageCount").toInt()
                  << " document=" << page->property("readerDocumentId").toString().toUtf8().constData() << '\n';
        throw;
    }
    if (page->property("readerPageCount").toInt() < 2)
        throw std::runtime_error(std::string("PDF import/render rejected: ") +
                                 control<QLabel>(page, "ReaderStatus")->text().toUtf8().constData());
    P7_REQUIRE(!control<QLabel>(page, "ReaderCanvas")->pixmap(Qt::ReturnByValue).isNull());
    P7_REQUIRE(control<QListWidget>(page, "ReaderDocuments")->count() == 1);
}
QJsonObject savedDocument(const QString& root, const QString& notes, int page)
{
    const auto library = json(QDir(root).filePath("library.json"));
    P7_REQUIRE(library["schema"] == "org.cordis.literature-reader.library/1" && library["revision"].toInt() > 0);
    const auto documents = library["documents"].toArray();
    P7_REQUIRE(documents.size() == 1);
    const auto document = documents.at(0).toObject();
    P7_REQUIRE(document["notes"].toString() == notes && document["position"].toObject()["page"].toInt() == page);
    P7_REQUIRE(library["activeDocument"].toString() == document["id"].toString());
    const auto source = read(QDir(root).filePath("objects/" + document["sha256"].toString() + ".pdf"));
    P7_REQUIRE(sha(source) == document["sha256"].toString() && source.size() == document["bytes"].toString().toLongLong());
    return document;
}
void save(QWidget* page, const QString& root, const QString& notes, int expectedPage)
{
    control<QPlainTextEdit>(page, "ReaderNotes")->setPlainText(notes);
    click(page, "ReaderSave");
    idle(page);
    savedDocument(root, notes, expectedPage);
}
void screenshot(Scene& scene, QWidget* page, const QString& path)
{
    P7_REQUIRE(scene.workbench.shell.show().hasValue());
    auto* window = qobject_cast<ElaWindow*>(page->window());
    P7_REQUIRE(window && !page->property("ElaPageKey").toString().isEmpty());
    window->navigation(page->property("ElaPageKey").toString());
    until([page] { return page->isVisible(); });
    QTest::qWait(100);
    P7_REQUIRE(window->grab().save(path, "PNG"));
}

void readerSession(const QString& mode, const QString& manifest, const QString& workspace,
                   const QString& pdfPath, const QString& artifact)
{
    Scene scene;
    scene.open(manifest, workspace);
    QPointer<QWidget> page = onlyPage();
    until([&] { const auto rows = take(scene.workbench.session->snapshot()).coordinator.running;
        return rows.size() == 1 && rows.front().state == cordis::InstanceState::Active; });
    const auto root = QDir(workspace).filePath("library");
    if (mode == "seed" || mode == "paper-seed") {
        importPdf(page, pdfPath, mode == "paper-seed" ? 0 : 3);
        const auto firstImage = control<QLabel>(page, "ReaderCanvas")->pixmap(Qt::ReturnByValue).toImage();
        P7_REQUIRE(firstImage.save(QDir(artifact).filePath(mode + "-page-1.png")));
        const int serial = page->property("readerRenderSerial").toInt();
        click(page, "ReaderNextPage");
        until([page, serial] { return !page->property("readerBusy").toBool() && page->property("readerCurrentPage").toInt() == 1 &&
                                    page->property("readerRenderSerial").toInt() > serial; });
        P7_REQUIRE(control<QSpinBox>(page, "ReaderPageNumber")->value() == 2);
        P7_REQUIRE(firstImage != control<QLabel>(page, "ReaderCanvas")->pixmap(Qt::ReturnByValue).toImage());
        P7_REQUIRE(control<QLabel>(page, "ReaderCanvas")->pixmap(Qt::ReturnByValue).save(QDir(artifact).filePath(mode + "-page-2.png")));
        auto* zoom = control<QComboBox>(page, "ReaderZoom");
        const int zoomSerial = page->property("readerRenderSerial").toInt();
        const int zoomIndex = zoom->findData(1.25);
        P7_REQUIRE(zoomIndex >= 0 && zoomIndex != zoom->currentIndex());
        zoom->setCurrentIndex(zoomIndex);
        until([page, zoomSerial] { return !page->property("readerBusy").toBool() &&
            page->property("readerRenderSerial").toInt() > zoomSerial; });
        save(page, root, seedNote, 1);
        P7_REQUIRE(savedDocument(root, seedNote, 1)["position"].toObject()["zoom"].toDouble() == 1.25);
    } else {
        until([page] { return !page->property("readerBusy").toBool() && page->property("readerPageCount").toInt() >= 2 &&
                                    page->property("readerCurrentPage").toInt() == 1; });
        P7_REQUIRE(control<QPlainTextEdit>(page, "ReaderNotes")->toPlainText() ==
                   (mode == "upgrade-verify" ? continuedNote : seedNote));
        P7_REQUIRE(control<QComboBox>(page, "ReaderZoom")->currentData().toDouble() == 1.25);
        P7_REQUIRE(!control<QLabel>(page, "ReaderCanvas")->pixmap(Qt::ReturnByValue).isNull());
        if (mode == "upgrade-reopen") {
            control<QLineEdit>(page, "ReaderBookmarkLabel")->setText(QString::fromUtf8(u8"升级后的书签"));
            click(page, "ReaderAddBookmark");
            P7_REQUIRE(control<QListWidget>(page, "ReaderBookmarks")->count() == 1);
            P7_REQUIRE(control<QListWidget>(page, "ReaderBookmarks")->item(0)->data(Qt::UserRole).toInt() == 1);
            control<QTabWidget>(page, "ReaderMarkTabs")->setCurrentIndex(1);
            control<QLineEdit>(page, "ReaderQuote")->setText(QStringLiteral("Page 2 reading target"));
            control<QPlainTextEdit>(page, "ReaderAnnotationNote")->setPlainText(QString::fromUtf8(u8"已核对原文的页附注。"));
            click(page, "ReaderAddAnnotation");
            P7_REQUIRE(control<QListWidget>(page, "ReaderAnnotations")->count() == 1);
        }
        if (mode != "upgrade-verify")
            save(page, root, continuedNote, 1);
        if (mode == "upgrade-reopen") {
            P7_REQUIRE(savedDocument(root, continuedNote, 1)["bookmarks"].toArray().size() == 1);
            const auto annotations = savedDocument(root, continuedNote, 1)["annotations"].toArray();
            P7_REQUIRE(annotations.size() == 1 && annotations.at(0).toObject()["page"].toInt() == 1);
            const auto exportPath = QDir(artifact).filePath("exported-notes.md");
            control<QLineEdit>(page, "ReaderExportNotesPath")->setText(exportPath);
            click(page, "ReaderExportNotes");
            idle(page);
            P7_REQUIRE(QString::fromUtf8(read(exportPath)).contains(continuedNote));
        }
        if (mode == "upgrade-verify") {
            auto* bookmarks = control<QListWidget>(page, "ReaderBookmarks");
            auto* annotations = control<QListWidget>(page, "ReaderAnnotations");
            P7_REQUIRE(bookmarks->count() == 1 && annotations->count() == 1);
            P7_REQUIRE(bookmarks->item(0)->data(Qt::UserRole).toInt() == 1 &&
                       bookmarks->item(0)->text().contains(QString::fromUtf8(u8"升级后的书签")));
            P7_REQUIRE(annotations->item(0)->data(Qt::UserRole).toInt() == 1 &&
                       annotations->item(0)->text().contains(QString::fromUtf8(u8"已核对原文的页附注。")) &&
                       annotations->item(0)->toolTip() == QStringLiteral("Page 2 reading target"));
            writeJson(QDir(artifact).filePath("upgrade-verify-ui.json"), {{"bookmarks", bookmarks->count()},
                {"bookmarkText", bookmarks->item(0)->text()}, {"annotations", annotations->count()},
                {"annotationText", annotations->item(0)->text()}, {"annotationQuote", annotations->item(0)->toolTip()},
                {"pageZeroBased", page->property("readerCurrentPage").toInt()},
                {"notes", control<QPlainTextEdit>(page, "ReaderNotes")->toPlainText()}, {"dataRoot", root}});
            screenshot(scene, page, QDir(artifact).filePath("upgrade-verify-bookmarks.png"));
            control<QTabWidget>(page, "ReaderMarkTabs")->setCurrentIndex(1);
        }
    }
    screenshot(scene, page, QDir(artifact).filePath(mode + "-workbench.png"));
    scene.close();
    P7_REQUIRE(page.isNull() && pages().isEmpty());
    writeJson(QDir(artifact).filePath(mode + "-closed.json"), {{"mode", mode}, {"manifest", manifest},
        {"manifestSha256", sha(read(manifest))}, {"normalClose", true}, {"pageDestroyed", true},
        {"pid", QString::number(QCoreApplication::applicationPid())}, {"librarySha256", sha(read(QDir(root).filePath("library.json")))}});
}

void child(const QString& mode, const QString& manifest, const QString& workspace, const QString& fixture,
           const QString& artifact)
{
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({"--child", mode, manifest, workspace, fixture, artifact});
    process.setStandardOutputFile(QDir(artifact).filePath(mode + ".stdout.raw"));
    process.setStandardErrorFile(QDir(artifact).filePath(mode + ".stderr.raw"));
    process.start();
    P7_REQUIRE(process.waitForStarted());
    const auto pid = process.processId();
    // This process owns the child until natural exit; no deadline-triggered kill or retry.
    P7_REQUIRE(process.waitForFinished(-1));
    writeJson(QDir(artifact).filePath(mode + "-process.json"), {{"pid", QString::number(pid)},
        {"exitCode", process.exitCode()}, {"normalExit", process.exitStatus() == QProcess::NormalExit},
        {"program", process.program()}, {"argv", QJsonArray::fromStringList(process.arguments())}});
    P7_REQUIRE(process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
}

void feedbackSession(bool reopen, const QString& manifest, const QString& workspace, const QString& artifact)
{
    QCoreApplication::setApplicationName(QStringLiteral("CordisFeedback-") + sha(workspace.toUtf8()).left(16));
    QStandardPaths::setTestModeEnabled(true);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    Scene scene;
    scene.open(manifest, workspace);
    P7_REQUIRE(scene.workbench.shell.show().hasValue());
    until([] { return !pages().isEmpty(); });
    auto* window = qobject_cast<ElaWindow*>(pages().front()->window());
    P7_REQUIRE(window);
    auto action = [&](const char* name) { control<QAction>(window,name)->trigger(); };
    auto* runs = control<QComboBox>(window,"CordisOutputRun");
    auto* sources = control<QComboBox>(window,"CordisOutputTarget");
    auto* instances = control<QComboBox>(window,"CordisOutputInstance");
    auto* output = control<QPlainTextEdit>(window,"CordisWorkbenchOutput");
    const QString thisRun = runs->currentData().toString();
    const QString receipt = QDir(artifact).filePath("feedback-live.json");
    const QString fail = QStringLiteral("保存配置失败"), recovered = QStringLiteral("配置已保存");
    const QString plugin = QStringLiteral("Literature reader");
    const QString pluginSource = "plugin/" + json(manifest)["plugins"].toArray().first().toString();
    const QString imagePath = QDir(artifact).filePath("preferences-wallpaper.png");
    action("CordisOpenSettings");
    auto* theme = control<QComboBox>(window, "CordisSettingsTheme");
    auto* navWidth = control<QSpinBox>(window, "CordisSettingsNavigationWidth");
    auto* dnd = control<ElaToggleSwitch>(window, "CordisSettingsDoNotDisturb");
    auto* userCard = control<ElaToggleSwitch>(window, "CordisSettingsUserCard");
    auto* material = control<QComboBox>(window, "CordisSettingsMaterial");
    auto* transition = control<QComboBox>(window, "CordisSettingsTransition");
    if (!reopen) {
        theme->setCurrentIndex(1);
        navWidth->setValue(286);
        dnd->setIsToggled(true);
        userCard->setIsToggled(false);
        material->setCurrentIndex(1);
        transition->setCurrentIndex(2);
        QImage image(240, 160, QImage::Format_RGB32);
        image.fill(QColor("#335779"));
        P7_REQUIRE(image.save(imagePath));
        click(window, "CordisBackgroundPicker");
        auto* picker = control<QWidget>(window, "CordisSettingsPage")->findChild<QFileDialog*>();
        P7_REQUIRE(picker);
        picker->selectFile(imagePath);
        P7_REQUIRE(QMetaObject::invokeMethod(picker, "accept"));
    }
    P7_REQUIRE(theme->currentIndex() == 1 && navWidth->value() == 286);
    P7_REQUIRE(dnd->getIsToggled() && window->property("CordisDoNotDisturb").toBool());
    P7_REQUIRE(!userCard->getIsToggled());
    P7_REQUIRE(material->currentIndex() == 1 && transition->currentIndex() == 2);
    P7_REQUIRE(window->getWindowPixmap(ElaThemeType::Dark).size() == QSize(240, 160));
    P7_REQUIRE(control<QTabWidget>(window, "CordisWorkbenchPanel")->isHidden());
    QJsonObject lines;
    auto displayed = [&](const QString& marker, const QString& key) {
        until([&]{ return output->toPlainText().contains(marker); },5000);
        for (const auto& line : output->toPlainText().split('\n'))
            if (line.contains(marker)) { lines.insert(key,line); return; }
    };
    auto instancesDisplayed = [&](const QJsonArray& expected) {
        const int source = sources->findData(pluginSource);
        P7_REQUIRE(source > 0);
        sources->setCurrentIndex(source);
        P7_REQUIRE(expected.size() == 2);
        for (const auto& value : expected) {
            const auto id = value.toString();
            const auto index = instances->findData(id);
            P7_REQUIRE(index > 0);
            instances->setCurrentIndex(index);
            displayed(plugin, QStringLiteral("reader/") + id);
            P7_REQUIRE(output->toPlainText().count(plugin) == 1);
            P7_REQUIRE(output->toPlainText().contains(QStringLiteral(" #%1/").arg(id)));
        }
        instances->setCurrentIndex(0);
        P7_REQUIRE(output->toPlainText().count(plugin) == 2);
    };
    if (!reopen) {
        action("CordisConfigurationFile");
        QWidget* config = nullptr;
        until([&]{
            for (auto* w : QApplication::allWidgets())
                if (w->objectName()=="CordisConfigurationPage" && w->isVisible()) config=w;
            return config!=nullptr;
        });
        QFile conflicting(QDir(workspace).filePath("session.yaml"));
        P7_REQUIRE(conflicting.open(QIODevice::WriteOnly | QIODevice::Truncate));
        conflicting.write("# external edit for optimistic save test\n");
        conflicting.close();
        click(config,"CordisConfigSave");
        until([&]{ return control<QAbstractButton>(config,"CordisConfigRebase")->isEnabled() &&
                          control<QAbstractButton>(config,"CordisConfigOpenLog")->isVisible(); });
        auto* currentProblems = control<QTreeWidget>(window, "CordisProblemsTree");
        until([&]{ return currentProblems->topLevelItemCount() > 0; }, 5000);
        click(config,"CordisConfigOpenLog");
        displayed(fail, fail);
        QTest::keyClick(output,Qt::Key_Escape);
        click(config,"CordisConfigRebase");
        click(config,"CordisConfigSave");
        until([&]{return !take(scene.workbench.session->snapshot()).store.dirty;});
        until([&]{return currentProblems->topLevelItemCount() == 0;}, 5000);
        config->window()->close();
        action("CordisOpenOutput");
        displayed(recovered, recovered);
        action("CordisAddPlugin");
        auto* dialog = control<QWidget>(window, "CordisAddPluginDialog");
        P7_REQUIRE(dialog->isVisible());
        control<QLineEdit>(dialog, "CordisAddPluginId")->setText("second");
        control<QPlainTextEdit>(dialog, "CordisAddPluginConfiguration")->setPlainText(
            QString::fromUtf8(QJsonDocument(QJsonObject{{"dataRoot", QDir(workspace).filePath("second-library")}})
                                 .toJson(QJsonDocument::Compact)));
        click(dialog, "CordisAddPluginSubmit");
        until([&]{ return dialog->isHidden() && pages().size() == 2; });
        QJsonArray owners;
        for (auto* page : pages())
            owners.append(QString::number(page->property("cordis.instance").toULongLong()));
        P7_REQUIRE(owners[0] != owners[1]);
        action("CordisOpenOutput");
        instancesDisplayed(owners);
        writeJson(receipt,{{"run",thisRun},{"pid",QString::number(QCoreApplication::applicationPid())},
                           {"lines",lines},{"pluginSource",sources->currentData().toString()},{"instances",owners}});
    } else {
        const auto previous = json(receipt);
        P7_REQUIRE(thisRun!=previous["run"].toString());
        P7_REQUIRE(sources->currentData().toString() == pluginSource);
        action("CordisPreviousOutput");
        P7_REQUIRE(runs->currentData().toString()==previous["run"].toString());
        sources->setCurrentIndex(sources->findData(QStringLiteral("platform")));
        displayed(fail, fail);
        displayed(recovered, recovered);
        instancesDisplayed(previous["instances"].toArray());
        P7_REQUIRE(lines==previous["lines"].toObject());
        P7_REQUIRE(window->grab().save(QDir(artifact).filePath("feedback-history.png")));
        writeJson(QDir(artifact).filePath("feedback-reopened.json"),
                  {{"run",thisRun},{"pid",QString::number(QCoreApplication::applicationPid())},
                   {"priorRun",previous["run"]},{"lines",lines}});
    }
    scene.close();
    const auto preferences = json(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                  .filePath("preferences/workbench.json"));
    P7_REQUIRE(preferences.value("theme").toInt() == 1 && preferences.value("navigationWidth").toInt() == 286);
    P7_REQUIRE(preferences.value("material").toInt() == 1 && preferences.value("transition").toInt() == 2);
    P7_REQUIRE(preferences.value("doNotDisturb").toBool() && !preferences.value("userCardVisible").toBool(true));
    P7_REQUIRE(preferences.value("background").toString() == imagePath &&
               preferences.value("outputSource").toString() == pluginSource);
    writeJson(QDir(artifact).filePath(reopen ? "preferences-reopened.json" : "preferences-live.json"), preferences);
}

void run(const QString& mode, const QString& v1, const QString& v2, const QString& artifact, const QString& paper = {})
{
    const auto workspace = QDir(artifact).filePath(QString::fromUtf8(u8"工作区 𠮷"));
    if (mode == "feedback") {
        child("feedback-live",v2,workspace,{},artifact);
        child("feedback-reopen",v2,workspace,{},artifact);
        const auto live=json(QDir(artifact).filePath("feedback-live.json"));
        const auto past=json(QDir(artifact).filePath("feedback-reopened.json"));
        P7_REQUIRE(live["pid"]!=past["pid"] && live["run"]==past["priorRun"]);
        const QDir archive(QDir(workspace).filePath(".cordis/logs/"+live["run"].toString()));
        P7_REQUIRE(json(archive.filePath("run.json")).contains("ended"));
        const auto expected=live["lines"].toObject();
        QJsonObject events;
        for (const auto& part : archive.entryList({"events-*.jsonl"},QDir::Files)) {
            for (const auto& raw : read(archive.filePath(part)).trimmed().split('\n')) {
                const auto row=QJsonDocument::fromJson(raw).object();
                const auto text=row["text"].toString();
                for(auto it=expected.begin();it!=expected.end();++it) {
                    const auto line = it.value().toString();
                    const auto timestamp = QDateTime::fromMSecsSinceEpoch(row["utcMs"].toString().toLongLong())
                                               .toString("HH:mm:ss.zzz");
                    const auto prefix = QStringLiteral("[%1] %2 ").arg(row["sequence"].toString(), timestamp);
                    if (text.isEmpty() || !line.endsWith(text) || !line.startsWith(prefix))
                        continue;
                    if (it.key().startsWith("reader/")) {
                        const auto instance = it.key().section('/', 1);
                        const auto expectedChannel = QStringLiteral("plugin/%1/%2/literature-reader")
                                                         .arg(instance, row["epoch"].toString());
                        P7_REQUIRE(row["source"] == live["pluginSource"] && row["channel"] == expectedChannel);
                        P7_REQUIRE(row["instance"].toString() == instance);
                    } else {
                        P7_REQUIRE(row["source"] == "platform" && text.contains(it.key()));
                    }
                    P7_REQUIRE(row["event"].toString().toULongLong()>0);
                    P7_REQUIRE(!events.contains(it.key()));
                    events.insert(it.key(),row["event"]);
                }
            }
        }
        P7_REQUIRE(events.size()==expected.size() && events.size()==4);
        writeJson(QDir(artifact).filePath("feedback-events.json"),events);
        return;
    }
    const auto fixture = paper.isEmpty() ? QDir(artifact).filePath("paper.pdf") : paper;
    if (paper.isEmpty()) pdf(fixture);
    if (mode == "persistence" || mode == "upgrade" || mode == "paper") {
        if (mode == "upgrade") {
            const auto oldManifest = json(v1), newManifest = json(v2);
            P7_REQUIRE(oldManifest["moduleVersion"] == "1.0.0" && newManifest["moduleVersion"] == "1.1.0");
            P7_REQUIRE(oldManifest["moduleId"] == newManifest["moduleId"] && oldManifest["plugins"] == newManifest["plugins"]);
            P7_REQUIRE(oldManifest["sha256"] != newManifest["sha256"]);
        }
        child(mode == "paper" ? "paper-seed" : "seed", v1, workspace, fixture, artifact);
        const auto before = read(QDir(workspace).filePath("library/library.json"));
        child(mode == "upgrade" ? "upgrade-reopen" : mode == "paper" ? "paper-reopen" : "reopen",
              mode == "upgrade" ? v2 : v1, workspace, fixture, artifact);
        P7_REQUIRE(before != read(QDir(workspace).filePath("library/library.json")));
        savedDocument(QDir(workspace).filePath("library"), continuedNote, 1);
        if (mode == "upgrade") {
            const auto savedV2 = read(QDir(workspace).filePath("library/library.json"));
            child("upgrade-verify", v2, workspace, fixture, artifact);
            P7_REQUIRE(savedV2 == read(QDir(workspace).filePath("library/library.json")));
        }
        return;
    }
    Scene scene;
    if (mode == "diagnostics") {
        const auto badRoot = QDir(artifact).filePath("bad-module");
        P7_REQUIRE(QDir().mkpath(badRoot));
        auto manifest = json(v1);
        P7_REQUIRE(QFile::copy(QDir(QFileInfo(v1).absolutePath()).filePath(manifest["library"].toString()),
                              QDir(badRoot).filePath(manifest["library"].toString())));
        manifest["sha256"] = QString(64, QLatin1Char('0'));
        const auto badManifest = QDir(badRoot).filePath("module.json");
        writeJson(badManifest, manifest);
        QString error;
        try { scene.workbench.open(badManifest, workspace); }
        catch (const std::exception& failure) { error = QString::fromUtf8(failure.what()); }
        P7_REQUIRE(!error.isEmpty() && !scene.workbench.session && pages().isEmpty());
        P7_REQUIRE(error.contains("SHA256 mismatch"));
        writeJson(QDir(artifact).filePath("diagnostic.json"), {{"actualError", error}, {"badManifest", badManifest},
            {"inputManifestSha256", sha(read(v1))}, {"instances", static_cast<int>(take(scene.workbench.kernel->snapshot()).instances.size())}});
        P7_REQUIRE(take(scene.workbench.kernel->snapshot()).instances.empty());
    } else if (mode == "template") {
        scene.open(v1, workspace);
        until([&] { const auto rows = take(scene.workbench.shell.pages()->snapshot()); return std::any_of(rows.begin(), rows.end(),
            [](const auto& row) { return row.id == "external-page" && row.widgetAlive; }); });
        P7_REQUIRE(pages().isEmpty());
    } else {
        scene.open(v1, workspace, mode != "waiting");
        if (mode == "waiting") {
            const auto snapshot = take(scene.workbench.session->snapshot());
            P7_REQUIRE(snapshot.latestPlan && snapshot.latestPlan->outcome == cordis::loader::PlanOutcome::Pending);
            P7_REQUIRE(snapshot.coordinator.running.size() == 1 && pages().isEmpty());
            P7_REQUIRE(snapshot.coordinator.running.front().state != cordis::InstanceState::Active);
            scene.workbench.publishUi();
        }
        QPointer<QWidget> page = onlyPage();
        until([&] { const auto rows = take(scene.workbench.session->snapshot()).coordinator.running;
            return rows.size() == 1 && rows.front().state == cordis::InstanceState::Active; });
        if (mode == "close_busy") {
            control<QLineEdit>(page, "ReaderImportPath")->setText(fixture);
            click(page, "ReaderImport");
            P7_REQUIRE(page->property("readerBusy").toBool());
        } else {
            importPdf(page, fixture);
            if (mode == "multi") {
                const auto snapshot = take(scene.workbench.session->snapshot());
                const auto plugins = take(scene.workbench.session->registeredPlugins());
                P7_REQUIRE(plugins.size() == 1);
                const auto otherRoot = QDir(workspace).filePath("other-library");
                const auto config = QJsonDocument(QJsonObject{{"dataRoot", otherRoot}}).toJson(QJsonDocument::Compact).toStdString();
                const auto accepted = take(scene.workbench.session->addInstance("second", plugins.front(), config,
                    snapshot.coordinator.desiredRevision));
                P7_REQUIRE(wait(accepted.settled, scene.workbench.scheduler)->hasValue());
                until([] { return pages().size() == 2; });
                until([&] { const auto rows = take(scene.workbench.session->snapshot()).coordinator.running;
                    return rows.size() == 2 && std::all_of(rows.begin(), rows.end(), [](const auto& row) {
                        return row.state == cordis::InstanceState::Active; }); });
                const auto all = pages();
                P7_REQUIRE(all[0]->property("cordis.instance") != all[1]->property("cordis.instance"));
                QWidget* second = all[0] == page ? all[1] : all[0];
                P7_REQUIRE(control<QListWidget>(second, "ReaderDocuments")->count() == 0);
                importPdf(second, fixture);
                save(page, QDir(workspace).filePath("library"), seedNote, 0);
                save(second, otherRoot, continuedNote, 0);
                P7_REQUIRE(savedDocument(QDir(workspace).filePath("library"), seedNote, 0)["notes"] !=
                           savedDocument(otherRoot, continuedNote, 0)["notes"]);
            }
        }
        scene.close();
        P7_REQUIRE(page.isNull() && pages().isEmpty());
        return;
    }
    scene.close();
    P7_REQUIRE(pages().isEmpty());
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const auto args = app.arguments();
    try {
        if (args.size() == 7 && args[1] == "--child") {
            if (args[2]=="feedback-live" || args[2]=="feedback-reopen") {
                feedbackSession(args[2]=="feedback-reopen",args[3],args[4],args[6]);
                return 0;
            }
            P7_REQUIRE(QStringList({"seed", "reopen", "upgrade-reopen", "upgrade-verify", "paper-seed", "paper-reopen"}).contains(args[2]));
            readerSession(args[2], args[3], args[4], args[5], args[6]);
            return 0;
        }
        P7_REQUIRE(args.size() == 5 || (args.size() == 6 && args[1] == "paper"));
        P7_REQUIRE(QStringList({"waiting", "multi", "diagnostics", "template", "persistence", "upgrade", "close_busy", "paper","feedback"}).contains(args[1]));
        P7_REQUIRE(QDir().mkpath(args[4]));
        QTemporaryDir scratch(QDir(args[4]).filePath(args[1] + "-XXXXXX"));
        P7_REQUIRE(scratch.isValid());
        scratch.setAutoRemove(false);
        std::cout << "P7_ARTIFACTS=" << scratch.path().toUtf8().constData() << '\n' << std::flush;
        P7_REQUIRE(QFile::copy(args[2], QDir(scratch.path()).filePath("input-v1-module.json")));
        P7_REQUIRE(QFile::copy(args[3], QDir(scratch.path()).filePath("input-v2-module.json")));
        run(args[1], args[2], args[3], scratch.path(), args.size() == 6 ? args[5] : QString{});
        writeJson(QDir(scratch.path()).filePath("result.json"), {{"schema", "cordis.p7.external-acceptance/1"},
            {"case", args[1]}, {"passed", true}, {"v1ManifestSha256", sha(read(args[2]))},
            {"v2ManifestSha256", sha(read(args[3]))}, {"humanAcceptanceClaimed", false}});
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "P7 acceptance failed: " << error.what() << '\n';
        return 1;
    }
}
