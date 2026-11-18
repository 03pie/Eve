/** @file upgrade_view_controls.cpp
 * @brief 真实候选输入、文件选择和预览失效，不装载伪候选或改批准名单。
 */
#include "upgrade_view_p.hpp"
#include <ElaTabWidget.h>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMouseEvent>
#include <QTabBar>
#include <set>

namespace upgrade_view_test {
namespace {
class SourceOnChooserShow final : public QObject {
public:
    SourceOnChooserShow(QLineEdit& input, QString value) : input_(input), value_(std::move(value))
    {
        QApplication::instance()->installEventFilter(this);
    }
    ~SourceOnChooserShow() override { QApplication::instance()->removeEventFilter(this); }
    bool observed = false;
    std::exception_ptr failure;

protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (!observed && event->type() == QEvent::Show && object->objectName() == "CordisUpgradeFileDialog") {
            observed = true;
            try {
                input_.setText(value_);
            } catch (...) {
                failure = std::current_exception();
            }
        }
        return false;
    }

private:
    QLineEdit& input_;
    QString value_;
};
void noExecution(const Scene& scene, std::size_t modules = 3)
{
    const auto snapshot = take(scene.state->upgradeSnapshot());
    PLATFORM_CHECK(!snapshot.active && snapshot.history.empty() && snapshot.accepting &&
                   take(scene.state->manager->snapshot()).size() == modules &&
                   take(scene.state->kernel->replacements()).empty());
}
void previewCase(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    const auto source = take(parseConfiguration(scene.state->initial));
    const auto declaration = take(scene.state->driver->configurationDefinitions()->module("example.counter"));
    const auto appended =
        take(addConfigurationEntry(source, "disabled_counter", "example.counter", "null", {}, declaration));
    scene.apply(take(setEntryEnabled(appended, "disabled_counter", false)).normalizedYaml());
    const auto original = running(scene, "example.counter");
    const auto before = take(scene.state->modulesSnapshot());
    View view(scene);
    view.select("counter");
    view.source->setText("module.json");
    view.review->click();
    PLATFORM_CHECK(!view.start->isEnabled());
    view.preview("counter", candidatePath(sourceBin, "counter"));
    const auto value =
        take(scene.state->previewUpgrade("counter", QFileInfo(view.source->text()).absolutePath(), "module.json"));
    PLATFORM_CHECK(value.previousManifest.moduleVersion == "1.1.0" &&
                   value.artifact.manifest().moduleVersion == "1.2.0" &&
                   value.previousManifest.moduleId == "org.cordis.example.counter" &&
                   (std::set<std::string>(value.affectedPaths.begin(), value.affectedPaths.end()) ==
                    std::set<std::string>{"counter", "disabled_counter", "reporter"}));
    const auto text = view.previewText->toPlainText();
    for (const auto& field :
         {value.previousManifest.moduleId, value.previousManifest.moduleVersion, value.previousManifest.sha256,
          value.artifact.manifest().moduleVersion, value.artifact.manifest().sha256, std::string("reporter"),
          std::string("disabled_counter")})
        PLATFORM_CHECK(text.contains(QString::fromStdString(field)));
    PLATFORM_CHECK(!QFile::exists(
        QDir(scene.store).filePath(QString::fromStdString(value.artifact.manifest().sha256) + "/module.json")));
    auto forgedManifest = value;
    forgedManifest.previousManifest.moduleVersion = "9.9.9";
    PLATFORM_CHECK(scene.state->upgrade(forgedManifest).error().code == ErrorCode::ConfigConflict);
    auto forgedPaths = value;
    forgedPaths.affectedPaths.clear();
    PLATFORM_CHECK(scene.state->upgrade(forgedPaths).error().code == ErrorCode::ConfigConflict);
    noExecution(scene);
    PLATFORM_CHECK(running(scene, "example.counter").instance == original.instance &&
                   take(scene.state->modulesSnapshot()).desiredRevision == before.desiredRevision);
    for (const auto& row : before.current)
        PLATFORM_CHECK(row.supportsUpgrade);
    finish(scene);
}
void pickerClose(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    View view(scene);
    view.select("counter");
    auto* tabs = view.body->findChild<ElaTabWidget*>("CordisModuleTabs");
    PLATFORM_CHECK(tabs && tabs->count() == 2 && !tabs->isMovable() && !tabs->tabsClosable() &&
                   !tabs->getIsContainerAcceptDrops());
    const auto originalWindows = QApplication::topLevelWidgets().size();
    QPointer<QWidget> page = tabs->currentWidget();
    auto* bar = tabs->findChild<QTabBar*>();
    PLATFORM_CHECK(bar && page);
    const auto position = bar->tabRect(0).center();
    QMouseEvent press(QEvent::MouseButtonPress, position, bar->mapToGlobal(position), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(bar, &press);
    const auto outside = position + QPoint(400, 400);
    QMouseEvent move(QEvent::MouseMove, outside, bar->mapToGlobal(outside), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(bar, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, outside, bar->mapToGlobal(outside), Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    QApplication::sendEvent(bar, &release);
    barrier(scene.scheduler);
    PLATFORM_CHECK(page && tabs->count() == 2 && tabs->indexOf(page) >= 0 &&
                   QApplication::topLevelWidgets().size() == originalWindows);
    view.browse->click();
    QPointer<QFileDialog> picker = view.body->findChild<QFileDialog*>("CordisUpgradeFileDialog");
    PLATFORM_CHECK(picker && picker->isVisible() && !picker->isModal() && picker->windowModality() == Qt::NonModal &&
                   picker->parentWidget() && view.body->isAncestorOf(picker->parentWidget()));
    view.select("reporter");
    until([&] { return !picker; });
    PLATFORM_CHECK(!view.start->isEnabled());
    view.select("counter");
    view.browse->click();
    picker = view.body->findChild<QFileDialog*>("CordisUpgradeFileDialog");
    PLATFORM_CHECK(picker && picker->isVisible());
    const auto chosen = candidatePath(sourceBin, "counter");
    picker->setDirectory(QFileInfo(chosen).absolutePath());
    picker->selectFile(chosen);
    auto* buttons = picker->findChild<QDialogButtonBox*>();
    PLATFORM_CHECK(buttons && buttons->button(QDialogButtonBox::Open));
    until([&] { return buttons->button(QDialogButtonBox::Open)->isEnabled(); });
    buttons->button(QDialogButtonBox::Open)->click();
    until([&] { return !picker; });
    PLATFORM_CHECK(QDir::cleanPath(view.source->text()) == QDir::cleanPath(chosen) && !view.start->isEnabled());
    view.browse->click();
    picker = view.body->findChild<QFileDialog*>("CordisUpgradeFileDialog");
    PLATFORM_CHECK(picker && picker->isVisible());
    PLATFORM_CHECK(wait(scene.state->close(), scene.scheduler)->hasValue());
    PLATFORM_CHECK(!picker && !view.body && !view.window && !page);
    finish(scene);
}
void invalidation(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    View view(scene);
    const auto path = candidatePath(sourceBin, "counter");
    view.preview("counter", path);
    view.source->setText(candidatePath(sourceBin, "reporter"));
    PLATFORM_CHECK(!view.start->isEnabled());
    view.source->setText(path);
    PLATFORM_CHECK(!view.start->isEnabled());
    view.preview("counter", path);
    view.select("reporter");
    PLATFORM_CHECK(!view.start->isEnabled());
    view.preview("counter", path);
    const auto revision = take(scene.state->modulesSnapshot()).desiredRevision;
    scene.apply(scene.state->initial);
    PLATFORM_CHECK(take(scene.state->modulesSnapshot()).desiredRevision > revision);
    // 不先刷新窗口；Start 必须由原 Workspace 入口重验旧修订。
    view.start->click();
    noExecution(scene);
    view.refresh();
    PLATFORM_CHECK(!view.start->isEnabled());
    view.source->setText(candidatePath(sourceBin, "reporter"));
    view.review->click();
    PLATFORM_CHECK(!view.start->isEnabled() && !view.status->text().isEmpty());
    noExecution(scene);
    const auto alternative = candidatePath(sourceBin, "counter", "a");
    const auto hash = take(preflightModule(utf8(sourceBin), "native-upgrade/counter-b/module.json")).manifest().sha256;
    view.preview("counter", path);
    // 先让下一次真实 Preview 必须更新文本，而非因内容相同跳过 setPlainText。
    view.source->setText(alternative);
    view.source->setText(path);
    bool reentered = false;
    std::exception_ptr failure;
    Observation observer;
    observer.connection = QObject::connect(view.previewText, &QPlainTextEdit::textChanged, view.previewText, [&] {
        if (reentered)
            return;
        reentered = true;
        try {
            view.source->setText(alternative);
        } catch (...) {
            failure = std::current_exception();
        }
    });
    view.review->click();
    QObject::disconnect(observer.connection);
    if (failure)
        std::rethrow_exception(failure);
    PLATFORM_CHECK(reentered && view.source->text() == alternative && !view.start->isEnabled() &&
                   !view.previewText->toPlainText().contains(QString::fromStdString(hash)));
    view.start->click();
    noExecution(scene);
    view.preview("counter", path);
    {
        SourceOnChooserShow probe(*view.source, alternative);
        view.browse->click();
        if (probe.failure)
            std::rethrow_exception(probe.failure);
        PLATFORM_CHECK(probe.observed && view.source->text() == alternative && !view.start->isEnabled() &&
                       !view.previewText->toPlainText().contains(QString::fromStdString(hash)));
    }
    QPointer<QFileDialog> chooser = view.body->findChild<QFileDialog*>("CordisUpgradeFileDialog");
    if (chooser)
        chooser->reject();
    until([&] { return !chooser; });
    noExecution(scene);
    finish(scene);
}
void changedSource(const QString& sourceBin)
{
    Scene scene;
    scene.open(sourceBin);
    scene.apply(scene.state->initial);
    View view(scene);
    const auto copied = copiedCandidate(scene, sourceBin, "counter");
    view.preview("counter", copied);
    const auto artifact = take(preflightModule(utf8(QFileInfo(copied).absolutePath()), "module.json"));
    {
        QFile library(QString::fromStdString(artifact.libraryPath()));
        PLATFORM_CHECK(library.open(QIODevice::Append) && library.write("changed", 7) == 7);
    }
    view.start->click();
    noExecution(scene);
    PLATFORM_CHECK(
        !view.start->isEnabled() && !view.status->text().isEmpty() &&
        !QFile::exists(
            QDir(scene.store).filePath(QString::fromStdString(artifact.manifest().sha256) + "/module.json")));
    scene.business();
    finish(scene);
}
} // namespace
bool controlsCase(const QString& name, const QString& sourceBin)
{
    if (name == "preview")
        previewCase(sourceBin);
    else if (name == "picker_close")
        pickerClose(sourceBin);
    else if (name == "invalidation")
        invalidation(sourceBin);
    else if (name == "changed_source")
        changedSource(sourceBin);
    else
        return false;
    return true;
}
} // namespace upgrade_view_test
