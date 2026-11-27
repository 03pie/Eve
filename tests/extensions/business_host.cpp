/** Real Ela host, independent launches, installed native Qt modules and persistent workspace output. */
#include "workbench.hpp"
#include "../support/module_artifacts.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTest>
#include <QUuid>
#include <algorithm>
#include <iostream>

namespace {
using namespace eve;
using ui::ExtensionAction;
namespace files = testing::modules;
constexpr char notesId[] = "org.eve.example.notebook";
constexpr char notesPlugin[] = "example.notebook";
constexpr char logId[] = "org.eve.test.logging";
constexpr char logPlugin[] = "test.logging";
constexpr char customConfig[] = R"({"text":"workspace-specific value"})";

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
    std::string(#condition) + " at line " + std::to_string(__LINE__)); } while (false)

template<class T>
T value(Result<T> result)
{
    if (!result.hasValue()) throw std::runtime_error(result.error().phase + ": " + result.error().message);
    return std::move(result).value();
}
template<class T>
typename Task<T>::Snapshot wait(const Task<T>& task)
{
    QElapsedTimer timer;
    timer.start();
    while (!task.isReady()) {
        CHECK(timer.elapsed() < 15000);
        QTest::qWait(5);
    }
    auto result = task.tryResult();
    CHECK(result);
    if (!result->hasValue()) throw std::runtime_error(result->error().phase + ": " + result->error().message);
    return result;
}

struct Scene {
    workbench::Workbench host;
    std::shared_ptr<ui::ExtensionControls> controls;
    explicit Scene(const QString& workspace)
    {
        wait(host.open(std::nullopt, workspace));
        CHECK(wait(host.applyInitial().settled)->value().outcome == loader::PlanOutcome::Applied);
        controls = host.extensionControls();
        CHECK(controls && controls->apply && controls->preview && controls->list);
    }
    void install(const QString& path)
    {
        auto preview = wait(controls->preview(path))->value();
        CHECK(preview.item.kind == ui::ExtensionKind::Business && !preview.item.builtin);
        wait(controls->installReviewed(preview));
        const auto installed = item(preview.item.id);
        CHECK(installed.readme == preview.item.readme && installed.packageBytes == QFileInfo(path).size());
        CHECK(installed.installedAt.isValid() && !preview.item.installedAt.isValid());
    }
    void apply(ExtensionAction action, const QString& id)
    {
        wait(controls->apply(action, id, {}, action == ExtensionAction::Uninstall ? item(id).packageHash : QString{}));
    }
    ui::ExtensionItem item(const QString& id)
    {
        const auto rows = value(controls->list());
        const auto match = [&](const auto& row) { return row.id == id; };
        CHECK(std::count_if(rows.begin(), rows.end(), match) == 1);
        return *std::find_if(rows.begin(), rows.end(), match);
    }
    loader::CoordinatorSnapshot snapshot() const { return value(host.session->snapshot()).coordinator; }
    loader::RunningEntry running(const std::string& plugin) const
    {
        const auto rows = snapshot().running;
        const auto match = [&](const auto& row) { return row.configuration.plugin == plugin; };
        CHECK(std::count_if(rows.begin(), rows.end(), match) == 1);
        const auto& row = *std::find_if(rows.begin(), rows.end(), match);
        CHECK(row.instance.valid() && row.state == InstanceState::Active);
        return row;
    }
    void close()
    {
        wait(host.close());
        value(host.scheduler->close());
    }
    ~Scene()
    {
        if (host.kernel) {
            try { close(); }
            catch (const std::exception& error) { std::cerr << "cleanup: " << error.what() << '\n'; }
        }
    }
};

bool mapped(const std::string& moduleId, const std::string& version)
{
    const auto generations = value(module::ModuleManager().snapshot());
    return std::any_of(generations.begin(), generations.end(), [&](const auto& row) {
        return row.nativeMapped && row.businessAccepted && row.manifest.moduleId == moduleId &&
            row.manifest.moduleVersion == version;
    });
}
void verifyLog(const QString& workspace)
{
    QDirIterator it(QDir(workspace).filePath(".eve/logs"), {"events-*.jsonl"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        for (const auto& line : files::read(it.next()).split('\n')) {
            const auto row = QJsonDocument::fromJson(line).object();
            if (row.value("source") == "plugin/test.logging" && row.value("module") == logId &&
                row.value("moduleVersion") == "1.0.0" && row.value("instance").toString().toULongLong() &&
                row.value("text").toString().startsWith("TEST_OPERATION_REJECTED:")) return;
        }
    }
    throw std::runtime_error("no persistent event from the installed logging module");
}

void installAndUpdate(Scene& s, const QDir& base)
{
    s.install(base.filePath("logging.evepkg"));
    const auto other = s.running(logPlugin);
    s.install(base.filePath("notes.evepkg"));
    const auto original = s.running(notesPlugin);
    CHECK(s.item(notesId).readme.startsWith("# Notebook"));
    s.install(base.filePath("notes.evepkg"));
    CHECK(s.running(notesPlugin).instance == original.instance && s.running(logPlugin).instance == other.instance);
    CHECK(s.item(logId).instances == 1 && s.item(notesId).instances == 1);
    const auto changed = value(loader::setEntryConfiguration(s.snapshot().desired->source(), original.path, customConfig));
    const auto draft = value(s.host.session->preview(changed.normalizedYaml()));
    CHECK(wait(value(s.host.session->apply(draft)).settled)->value().outcome == loader::PlanOutcome::Applied);
    CHECK(s.running(notesPlugin).configuration.config == customConfig);
    s.apply(ExtensionAction::Disable, notesId);
    CHECK(s.snapshot().running.size() == 1 && s.running(logPlugin).instance == other.instance);
    s.apply(ExtensionAction::Enable, notesId);
    CHECK(s.running(notesPlugin).configuration.config == customConfig);
    s.apply(ExtensionAction::Uninstall, notesId);
    CHECK(s.snapshot().running.size() == 1 && s.running(logPlugin).instance == other.instance);
    CHECK(s.item(notesId).missing);
    const auto retained = s.snapshot().desired->source().structure().entries().at(original.path);
    CHECK(retained.plugin == notesPlugin && retained.config == customConfig && !retained.disabled);
    s.install(base.filePath("notes.evepkg"));
    CHECK(s.running(notesPlugin).configuration.config == customConfig);
    CHECK(s.running(notesPlugin).path == original.path && s.running(logPlugin).instance == other.instance);
    const auto oldReview = wait(s.controls->preview(base.filePath("notes.evepkg")))->value();
    CHECK(oldReview.installedVersion == "1.0.0" && oldReview.installedHash == s.item(notesId).packageHash);
    s.install(base.filePath("update.evepkg"));
    const auto stale = s.controls->installReviewed(oldReview).tryResult();
    CHECK(stale && !stale->hasValue() && stale->error().code == ErrorCode::ConfigConflict);
    CHECK(s.item(notesId).version == "1.1.0");
    CHECK(s.item(notesId).pending && mapped(notesId, "1.0.0") && !mapped(notesId, "1.1.0"));
    CHECK(s.snapshot().running.size() == 2 && s.running(logPlugin).instance == other.instance);
}
// The catalog identifies the package, while the clicked card must retain its own instance path.
void checkInstanceCards(Scene& scene)
{
    const auto accepted = value(scene.host.session->addInstance("second-logger", logPlugin, "{}",
                                                               scene.snapshot().desiredRevision));
    CHECK(wait(accepted.settled)->value().outcome == loader::PlanOutcome::Applied);
    value(scene.host.desktop()->show());
    QElapsedTimer deadline;
    deadline.start();
    QWidget* second = nullptr;
    while (!second && deadline.elapsed() < 5000) {
        QTest::qWait(10);
        for (auto top : QApplication::topLevelWidgets()) {
            auto home = top->findChild<QWidget*>("EveWelcomePage");
            if (!home || !home->isVisible()) continue;
            int logCards = 0;
            QWidget* selected = nullptr;
            for (auto candidate : home->findChildren<QWidget*>("EvePluginCard")) {
                if (!candidate->isVisible() || candidate->property("EveExtensionId").toString() != logId) continue;
                ++logCards;
                if (candidate->property("EvePluginId").toString() == "configuration:second-logger")
                    selected = candidate;
            }
            if (logCards == 2) second = selected;
        }
    }
    CHECK(second);
    QTest::mouseClick(second, Qt::LeftButton);
    QWidget* dialog = nullptr;
    for (auto top : QApplication::topLevelWidgets())
        if (top->isVisible() && top->objectName() == "EveExtensionDialog") dialog = top;
    CHECK(dialog);
    auto tabs = dialog->findChild<QTabWidget*>("EveExtensionTabs");
    auto instances = dialog->findChild<QComboBox*>("EveExtensionInstance");
    CHECK(tabs && instances);
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->tabText(i) == QStringLiteral("工作区实例")) tabs->setCurrentIndex(i);
    CHECK(instances->isVisible() && instances->count() == 2 && instances->currentText() == "second-logger");
    dialog->close();
}
void phase(const QString& stage, const QDir& base)
{
    const auto workspace = base.filePath(stage == "independent" ? "workspace B" : "workspace A");
    Scene scene(workspace);
    if (stage == "install") installAndUpdate(scene, base);
    else if (stage == "independent") {
        CHECK(scene.item(notesId).version == "1.1.0" && scene.item(logId).instances == 0);
        CHECK(scene.snapshot().running.empty());
        scene.install(base.filePath("logging.evepkg"));
        CHECK(scene.running(logPlugin).instance.valid());
        scene.apply(ExtensionAction::Disable, logId);
        CHECK(scene.snapshot().running.empty() && scene.item(logId).instances == 1);
        checkInstanceCards(scene);
    } else {
        CHECK(stage == "reopen" && scene.snapshot().running.size() == 2);
        CHECK(mapped(notesId, "1.1.0") && !mapped(notesId, "1.0.0"));
        CHECK(!scene.item(notesId).pending && scene.item(notesId).version == "1.1.0");
        CHECK(scene.running(notesPlugin).configuration.config == customConfig && scene.running(logPlugin).instance.valid());
    }
    scene.close();
    verifyLog(workspace);
}

void run(const QString& program, const QStringList& args)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    CHECK(process.waitForFinished(55000));
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode())
        throw std::runtime_error(process.readAll().toStdString());
}

void orchestrate(const QStringList& args)
{
    CHECK(args.size() == 6);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const QDir base(temp.path());
    const auto logManifest = files::install(args[3], base.filePath("fixture"), logId, {logPlugin}, "qt-ui");
    const auto logger = QFileInfo(logManifest).dir().filePath("module.dll");
    const auto descriptor = base.filePath("package.json");
    const auto readme = base.filePath("README.md");
    files::write(readme, "# Notebook\n\nKeep notes in your workspace.\n");
    const auto pack = [&](const QString& dll, const QString& name, const QString& output, const QString& documentation = {}) {
        files::write(descriptor, QJsonDocument(QJsonObject{{"formatVersion", 1}, {"kind", "business"},
            {"name", name}, {"publisher", "Eve tests"}}).toJson());
        run(args[1], {"-DLIBRARY=" + dll, "-DPACKAGE_JSON=" + descriptor, "-DREADME=" + documentation, "-DOUTPUT=" + base.filePath(output),
                      "-P", args[2]});
    };
    pack(logger, "Logging fixture", "logging.evepkg");
    pack(args[4], "Notebook fixture", "notes.evepkg", readme);
    pack(args[5], "Notebook fixture", "update.evepkg");
    const auto name = "Eve-Extensions-Test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QCoreApplication::setApplicationName(name);
    QDir profile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    CHECK(profile.isAbsolute() && profile.dirName() == name && !profile.exists());
    for (const QString& stage : {"install", "independent", "reopen"})
        run(QCoreApplication::applicationFilePath(), {"--phase", stage, base.path(), name});
    CHECK(profile.removeRecursively());
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    try {
        const auto args = app.arguments();
        if (args.size() == 5 && args[1] == "--phase") {
            app.setApplicationName(args[4]);
            phase(args[2], QDir(args[3]));
        } else orchestrate(args);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
