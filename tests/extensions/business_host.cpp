/** Real Ela host, independent launches, installed native Qt modules and persistent workspace output. */
#include "workbench.hpp"
#include <eve/extensions/catalog.hpp>
#include <eve/extensions/qt_module.hpp>
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
#include <future>
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
        const auto foreign = std::async(std::launch::async, [actions = controls] { return actions->list(); }).get();
        CHECK(!foreign.hasValue() && foreign.error().code == ErrorCode::WrongThread);
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
        CHECK(!controls->list().hasValue());
        const auto stale = controls->apply(ExtensionAction::Restart, {}, {}, {}).tryResult();
        CHECK(stale && !stale->hasValue());
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
        if (tabs->tabText(i) == QStringLiteral("Workspace Instances")) tabs->setCurrentIndex(i);
    CHECK(instances->isVisible() && instances->count() == 2 && instances->currentText() == "second-logger");
    dialog->close();
}
void writeLegacyCatalog(const QString& workspace)
{
    const QDir catalog(extensions::Catalog::defaultRoot());
    const auto file = catalog.filePath("extensions.json");
    auto index = QJsonDocument::fromJson(files::read(file)).object();
    auto records = index.value("extensions").toArray();
    auto releases = index.value("releases").toObject();
    for (const auto& plugin : {QStringLiteral("EVE.DESKTOP.LOGS"), QStringLiteral("eve.workspace.extensions")}) {
        const auto kind = plugin.startsWith("eve.desktop", Qt::CaseInsensitive) ? QStringLiteral("desktop") : QStringLiteral("business");
        const auto module = QStringLiteral("legacy.") + kind;
        const auto digest = QString::fromLatin1(QCryptographicHash::hash(plugin.toUtf8(), QCryptographicHash::Sha256).toHex());
        QJsonObject record{{"formatVersion", 1}, {"kind", kind}, {"name", module}, {"publisher", "v1"},
            {"description", ""}, {"defaultConfig", QJsonObject{}}, {"moduleId", module}, {"pluginId", plugin},
            {"version", "1.0.0"}, {"sha256", digest}, {"icon", ""}};
        const auto fingerprint = QCryptographicHash::hash(QJsonDocument(record).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
        releases.insert(module + "/1.0.0", QString::fromLatin1(fingerprint.toHex()));
        record.insert("packageHash", digest);
        records.append(record);
    }
    index.insert("extensions", records);
    index.insert("releases", releases);
    files::write(file, QJsonDocument(index).toJson());
    CHECK(QDir().mkpath(workspace));
    files::write(QDir(workspace).filePath("session.yaml"),
        "formatVersion: 1\nmodules: {legacy.business: {manifest: module.json}}\n"
        "plugins: [{id: previous, plugin: eve.workspace.extensions, module: legacy.business, config: {}}]\n");
    workbench::DesktopSelection selection(catalog.path());
    value(selection.request("legacy.desktop"));
}

void checkLegacyWorkspace(const QString& stage, const QDir& base)
{
    const auto workspace = base.filePath("legacy workspace");
    const bool first = stage == "legacy";
    if (first) writeLegacyCatalog(workspace);
    workbench::Workbench host;
    wait(host.open(std::nullopt, workspace));
    wait(host.applyInitial().settled);
    const auto state = value(host.session->snapshot()).coordinator;
    CHECK(state.desired && state.desired->pluginUnavailable(workbench::workspaceExtensionsId) && state.running.empty());
    CHECK(host.selection().selected() == workbench::desktopModuleId && !host.desktop()->hasExternalLayout());
    const auto actions = host.extensionControls();
    const auto rows = value(actions->list());
    CHECK(std::any_of(rows.begin(), rows.end(), [](const auto& row) { return row.id == notesId && !row.packageHash.isEmpty(); }));
    for (const auto& row : rows) {
        if (!row.id.startsWith("legacy.")) continue;
        if (!first) {
            CHECK(row.id == "legacy.business" && row.missing && row.actions.empty());
            continue;
        }
        CHECK(row.actions == std::vector<ExtensionAction>{ExtensionAction::Uninstall});
        const auto enable = actions->apply(ExtensionAction::Enable, row.id, {}, {}).tryResult();
        CHECK(enable && !enable->hasValue());
        wait(actions->apply(ExtensionAction::Uninstall, row.id, {}, row.packageHash));
    }
    const auto remaining = value(extensions::Catalog().installed());
    CHECK(std::none_of(remaining.begin(), remaining.end(), [](const auto& item) { return item.moduleId.startsWith("legacy."); }));
    CHECK(value(actions->list()).size() >= 2 && host.writeOutput("legacy catalog verified").hasValue());
    wait(host.close());
    value(host.scheduler->close());
}

void phase(const QString& stage, const QDir& base)
{
    if (stage == "developer") {
        workbench::Workbench host;
        wait(host.open(base.filePath("alias/module.json"), base.filePath("developer workspace")));
        wait(host.applyInitial().settled);
        const auto running = value(host.session->snapshot()).coordinator.running;
        CHECK(running.size() == 1 && running.front().configuration.plugin == logPlugin && mapped("extension", "1.0.0"));
        wait(host.close());
        value(host.scheduler->close());
        return;
    }
    if (stage == "legacy" || stage == "legacy-reopen") {
        checkLegacyWorkspace(stage, base);
        return;
    }
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
    CHECK(args.size() == 7);
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
    files::install(args[6], base.filePath("alias"), "extension", {logPlugin}, "qt-ui");
    pack(base.filePath("alias/module.dll"), "Development alias", "alias.evepkg");
    CHECK(!extensions::Catalog::preview(base.filePath("alias.evepkg")).hasValue());
    for (const auto id : {"builtin", "extension"}) {
        CHECK(extensions::isReservedIdentity(id, logPlugin));
        CHECK(!extensions::isReservedIdentity(id, logPlugin, extensions::ExtensionSource::DevelopmentManifest));
    }
    const auto conflict = files::install(args[3], base.filePath("conflict"), "legacy.business",
                                        {workbench::workspaceExtensionsId}, "qt-ui");
    pack(QFileInfo(conflict).dir().filePath("module.dll"), "Retired identity", "conflict.evepkg");
    const auto forbidden = base.filePath("conflict.evepkg");
    const auto reviewed = extensions::Catalog::preview(forbidden);
    CHECK(!reviewed.hasValue() && reviewed.error().message == "This identity belongs to the platform.");
    CHECK(!extensions::Catalog(base.filePath("isolated")).install(forbidden, {}).hasValue());
    const auto admission = extensions::loadQtExtension(conflict, "legacy.business", workbench::workspaceExtensionsId, {});
    CHECK(!admission.hasValue() && admission.error().message == "Workspace extension uses a reserved identity.");
    CHECK(!extensions::loadQtExtension(conflict, "legacy.business", workbench::workspaceExtensionsId, {},
                                       extensions::ExtensionSource::DevelopmentManifest).hasValue());
    const auto name = "Eve-Extensions-Test-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QCoreApplication::setApplicationName(name);
    QDir profile(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    CHECK(profile.isAbsolute() && profile.dirName() == name && !profile.exists());
    for (const QString& stage : {"developer", "install", "independent", "reopen", "legacy", "legacy-reopen"})
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
