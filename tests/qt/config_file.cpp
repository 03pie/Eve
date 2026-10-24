/** @file config_file.cpp
 * @brief 使用真实临时目录验证配置文件版本、冲突、协作写入和提交失败保护。
 */
#include <cordis/loader/qt_file.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QStringList>
#include <QTemporaryDir>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <cstdlib>
#include <future>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace cordis;
using namespace cordis::loader;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__)); } while (false)
/** @brief 候选经过真实解析/schema 校验，文件层不执行 Runtime。 */
ValidatedCandidate target(const std::string& text = "one")
{
    auto parsed = parseConfiguration("formatVersion: 1\nplugins: [{id: notes, plugin: demo, config: '" + text + "'}]\n").value();
    return validateCandidate(std::move(parsed), {{"demo", {}, [](const std::string&) { return Result<void>::success(); }}}).value();
}
/** @brief 原始写入仅用于模拟外部编辑器，字节不经过平台规范化。 */
void externalWrite(const QString& path, const std::string& text)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()));
    file.close();
    CHECK(file.error() == QFileDevice::NoError);
}
/** @brief 每项测试独占目录，不写仓库、用户配置或安装目录。 */
struct FileCase {
    QTemporaryDir directory;
    QString path;
    /** @brief 同时覆盖非 ASCII 文件名和空格路径。 */
    FileCase() : path(directory.filePath(QString::fromUtf8("配置 文件.yaml"))) { CHECK(directory.isValid()); }
    /** @brief 所有事务结束后不允许留下锁或临时文件。 */
    void onlyTarget() const
    {
        const auto files = QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        CHECK(files.size() == 1 && files.front() == QFileInfo(path).fileName());
    }
};
/** @brief 缺失、空文件、原始 CRLF/注释与嵌入 NUL 均按实际字节产生版本。 */
void reads()
{
    FileCase scene;
    auto missing = readConfigurationFile(scene.path).value();
    CHECK(!missing.version.exists && missing.version.sha256.empty() && missing.text.empty());
    CHECK(!missing.version.path.empty() && !QFile::exists(scene.path + QStringLiteral(".cordis.lock")));
    externalWrite(scene.path, {});
    auto empty = readConfigurationFile(scene.path).value();
    CHECK(empty.version.exists && empty.text.empty());
    CHECK(empty.version.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    externalWrite(scene.path, "abc");
    CHECK(readConfigurationFile(scene.path).value().version.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string raw("# external\r\n\0data", 17);
    externalWrite(scene.path, raw);
    CHECK(readConfigurationFile(scene.path).value().text == raw);
    ConfigurationFileLimits exact{raw.size()};
    CHECK(readConfigurationFile(scene.path, exact).hasValue());
    --exact.maximumBytes;
    CHECK(readConfigurationFile(scene.path, exact).error().code == ErrorCode::ConfigInvalid);
    scene.onlyTarget();
}
/** @brief 创建与更新提交完整规范文本；相同字节不重写，返回版本可作为下一次基线。 */
void commits()
{
    FileCase scene;
    auto first = target();
    auto base = readConfigurationFile(scene.path).value();
    auto saved = commitConfigurationFile(scene.path, first, base.version).value();
    CHECK(saved.outcome == FileCommitOutcome::Committed && !saved.error && saved.disk.version.exists);
    CHECK(saved.disk.text == first.source().normalizedYaml());
    CHECK(readConfigurationFile(scene.path).value().version.sha256 == saved.disk.version.sha256);
    CHECK(validateCandidate(parseConfiguration(saved.disk.text).value(), {{"demo", {}, [](const std::string&) { return Result<void>::success(); }}}).hasValue());
    auto unchanged = commitConfigurationFile(scene.path, first, saved.disk.version).value();
    CHECK(unchanged.outcome == FileCommitOutcome::Unchanged && !unchanged.error && unchanged.disk.text == saved.disk.text);
    auto next = target("two");
    auto updated = commitConfigurationFile(scene.path, next, unchanged.disk.version).value();
    CHECK(updated.outcome == FileCommitOutcome::Committed && updated.disk.version.sha256 != saved.disk.version.sha256);
    auto stale = commitConfigurationFile(scene.path, first, saved.disk.version).value();
    CHECK(stale.outcome == FileCommitOutcome::Conflict && stale.disk.text == next.source().normalizedYaml());
    CHECK(stale.error && stale.error->code == ErrorCode::ConfigConflict);
    scene.onlyTarget();
}
/** @brief 外部仅修改注释仍算冲突；无效 YAML 也完整保留，目标与磁盘均不自动替换。 */
void conflicts()
{
    FileCase scene;
    auto desired = target();
    externalWrite(scene.path, desired.source().normalizedYaml());
    auto baseline = readConfigurationFile(scene.path).value();
    const auto external = "# changed externally\n" + baseline.text;
    externalWrite(scene.path, external);
    auto attempt = commitConfigurationFile(scene.path, desired, baseline.version).value();
    CHECK(attempt.outcome == FileCommitOutcome::Conflict && attempt.error->code == ErrorCode::ConfigConflict);
    CHECK(attempt.disk.text == external && readConfigurationFile(scene.path).value().text == external);
    CHECK(desired.source().normalizedYaml() == baseline.text);
    externalWrite(scene.path, "not: [valid\n");
    auto invalid = commitConfigurationFile(scene.path, desired, attempt.disk.version).value();
    CHECK(invalid.outcome == FileCommitOutcome::Conflict && invalid.disk.text == "not: [valid\n");
    CHECK(!parseConfiguration(invalid.disk.text).hasValue());
    scene.onlyTarget();
}
/** @brief 初始缺失后外部创建空文件、外部删除和旧版本复用均不能静默覆盖。 */
void existence()
{
    FileCase scene;
    auto desired = target();
    auto missing = readConfigurationFile(scene.path).value();
    externalWrite(scene.path, {});
    auto created = commitConfigurationFile(scene.path, desired, missing.version).value();
    CHECK(created.outcome == FileCommitOutcome::Conflict && created.disk.version.exists && created.disk.text.empty());
    CHECK(QFile::remove(scene.path));
    auto removed = commitConfigurationFile(scene.path, desired, created.disk.version).value();
    CHECK(removed.outcome == FileCommitOutcome::Conflict && !removed.disk.version.exists && !QFile::exists(scene.path));
    auto saved = commitConfigurationFile(scene.path, desired, removed.disk.version).value();
    CHECK(saved.outcome == FileCommitOutcome::Committed);
    scene.onlyTarget();
}
/** @brief 无效路径/预算/基线和移动后候选在创建临时文件前拒绝。 */
void guards()
{
    FileCase scene;
    auto desired = target();
    auto baseline = readConfigurationFile(scene.path).value();
    CHECK(readConfigurationFile(QString()).error().code == ErrorCode::InvalidArgument);
    CHECK(readConfigurationFile(QStringLiteral("relative.yaml")).error().code == ErrorCode::InvalidArgument);
    CHECK(readConfigurationFile(QStringLiteral(":/resource.yaml")).error().code == ErrorCode::InvalidArgument);
    CHECK(!readConfigurationFile(scene.path + QChar(0)).hasValue());
#ifdef Q_OS_WIN
    CHECK(readConfigurationFile(QStringLiteral("//server/share/config.yaml")).error().code == ErrorCode::InvalidArgument);
    CHECK(readConfigurationFile(QStringLiteral("//./NUL")).error().code == ErrorCode::InvalidArgument);
    for (const auto& name : {"NUL.yaml", "con", "COM1.txt", "LPT9.yaml", "tail.", "tail "})
        CHECK(readConfigurationFile(scene.directory.filePath(QString::fromLatin1(name))).error().code == ErrorCode::InvalidArgument);
    CHECK(readConfigurationFile(scene.directory.filePath(QString::fromUtf8("COM¹.yaml"))).error().code == ErrorCode::InvalidArgument);
#endif
    CHECK(!readConfigurationFile(scene.path, {0}).hasValue());
    CHECK(!readConfigurationFile(scene.path, {static_cast<std::size_t>(std::numeric_limits<int>::max())}).hasValue());
    CHECK(commitConfigurationFile(scene.path, desired, baseline.version, {1}).error().code == ErrorCode::ConfigInvalid);
    auto invalid = baseline.version;
    invalid.sha256 = "not-an-absent-hash";
    CHECK(commitConfigurationFile(scene.path, desired, invalid).error().code == ErrorCode::InvalidArgument);
    invalid = baseline.version; invalid.exists = true;
    CHECK(!commitConfigurationFile(scene.path, desired, invalid).hasValue());
    invalid = baseline.version; invalid.path += ".other";
    CHECK(!commitConfigurationFile(scene.path, desired, invalid).hasValue());
    auto retained = std::move(desired);
    CHECK(commitConfigurationFile(scene.path, desired, baseline.version).error().code == ErrorCode::ConfigInvalid);
    CHECK(retained.source().isValid() && !QFile::exists(scene.path));
    CHECK(QDir(scene.directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot).empty());
}
/** @brief 不可用目录及非普通文件明确报告 I/O 错误，不建立有效的磁盘配置。 */
void ioErrors()
{
    FileCase scene;
    CHECK(readConfigurationFile(scene.directory.filePath(QStringLiteral("missing/config.yaml"))).error().code == ErrorCode::ConfigIo);
    CHECK(readConfigurationFile(scene.directory.path()).error().code == ErrorCode::ConfigIo);
    CHECK(QDir().mkdir(scene.path));
    auto directory = readConfigurationFile(scene.path);
    CHECK(!directory.hasValue() && directory.error().phase == "loader.file.read");
    CHECK(!QFile::exists(scene.path + QStringLiteral(".cordis.lock")));
}
/** @brief 符号链接/Windows 快捷方式不得被跟随覆盖，即使调用方提供匹配目标内容的版本。 */
void symlink()
{
    FileCase scene;
    auto desired = target();
    externalWrite(scene.path, "protected target");
    const auto link = scene.directory.filePath(QStringLiteral("configuration-link.lnk"));
    auto baseline = readConfigurationFile(scene.path).value();
    baseline.version.path = link.toUtf8().toStdString();
    CHECK(QFile(scene.path).link(link) && QFileInfo(link).isSymLink());
    auto readLink = readConfigurationFile(link);
    CHECK(!readLink.hasValue() && readLink.error().code == ErrorCode::ConfigIo);
    auto rejected = commitConfigurationFile(link, desired, baseline.version);
    CHECK(!rejected.hasValue() && rejected.error().code == ErrorCode::ConfigIo);
    CHECK(readConfigurationFile(scene.path).value().text == "protected target");
    CHECK(QFile::remove(link));
    scene.onlyTarget();
}
/** @brief 同一路径正在被协作写入时立即拒绝，不忙等、不抢锁、不改变原文件。 */
void locked()
{
    FileCase scene;
    auto desired = target();
    externalWrite(scene.path, "original");
    auto baseline = readConfigurationFile(scene.path).value();
    {
        QLockFile lock(scene.path + QStringLiteral(".cordis.lock"));
        lock.setStaleLockTime(0);
        CHECK(lock.tryLock(0));
        auto blocked = commitConfigurationFile(scene.path, desired, baseline.version);
        CHECK(!blocked.hasValue() && blocked.error().code == ErrorCode::ConfigConflict && blocked.error().phase == "loader.file.lock");
        CHECK(readConfigurationFile(scene.path).value().text == "original" && lock.isLocked());
    }
    CHECK(commitConfigurationFile(scene.path, desired, baseline.version).value().outcome == FileCommitOutcome::Committed);
    scene.onlyTarget();
}
/** @brief 两个真实线程持相同基线竞争，每轮恰有一次提交，另一方保留可观察冲突。 */
void concurrent()
{
    FileCase scene;
    auto left = target("left"), right = target("right");
    for (int iteration = 0; iteration < 12; ++iteration) {
        externalWrite(scene.path, "initial");
        const auto baseline = readConfigurationFile(scene.path).value();
        std::promise<void> start, leftReady, rightReady;
        auto ready = start.get_future().share();
        std::optional<Result<ConfigurationFileCommit>> a, b;
        std::thread one([&] { leftReady.set_value(); ready.wait(); a.emplace(commitConfigurationFile(scene.path, left, baseline.version)); });
        std::thread two([&] { rightReady.set_value(); ready.wait(); b.emplace(commitConfigurationFile(scene.path, right, baseline.version)); });
        leftReady.get_future().wait(); rightReady.get_future().wait(); start.set_value();
        one.join(); two.join();
        const bool wonA = a->hasValue() && a->value().outcome == FileCommitOutcome::Committed;
        const bool wonB = b->hasValue() && b->value().outcome == FileCommitOutcome::Committed;
        CHECK(wonA != wonB);
        const auto& lost = wonA ? *b : *a;
        CHECK(lost.hasValue() ? lost.value().outcome == FileCommitOutcome::Conflict && lost.value().error->code == ErrorCode::ConfigConflict : lost.error().code == ErrorCode::ConfigConflict);
        CHECK(readConfigurationFile(scene.path).value().text == (wonA ? left : right).source().normalizedYaml());
        scene.onlyTarget();
    }
}
#ifdef Q_OS_WIN
/** @brief Windows 拒绝重命名的真实句柄使 commit 失败，旧内容及可重试基线必须保留。 */
void commitFailure()
{
    FileCase scene;
    auto desired = target();
    externalWrite(scene.path, "original");
    auto baseline = readConfigurationFile(scene.path).value();
    {
        /** @brief 故障句柄只锁本测试临时文件，离开作用域总会释放。 */
        struct Handle {
            HANDLE value;
            /** @brief 关闭后允许后续事务重新提交。 */
            ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        } handle{CreateFileW(reinterpret_cast<const wchar_t*>(scene.path.utf16()), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        CHECK(handle.value != INVALID_HANDLE_VALUE);
        auto failed = commitConfigurationFile(scene.path, desired, baseline.version);
        CHECK(!failed.hasValue() && failed.error().code == ErrorCode::ConfigIo && failed.error().phase == "loader.file.commit");
        auto preserved = readConfigurationFile(scene.path).value();
        CHECK(preserved.text == baseline.text && preserved.version.sha256 == baseline.version.sha256);
        scene.onlyTarget();
    }
    CHECK(commitConfigurationFile(scene.path, desired, baseline.version).value().outcome == FileCommitOutcome::Committed);
    scene.onlyTarget();
}
#endif
/** @brief 不依赖 GUI 或 sleep，所有文件都限定在 QTemporaryDir。 */
int main(int argc, char** argv)
{
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string name = argv[1];
        if (name == "without_application") { CHECK(!QCoreApplication::instance()); reads(); commits(); return EXIT_SUCCESS; }
        QCoreApplication app(argc, argv);
        if (name == "read") reads();
        else if (name == "commit") commits();
        else if (name == "conflict") conflicts();
        else if (name == "existence") existence();
        else if (name == "guards") guards();
        else if (name == "io") ioErrors();
        else if (name == "symlink") symlink();
        else if (name == "lock") locked();
        else if (name == "concurrent") concurrent();
#ifdef Q_OS_WIN
        else if (name == "commit_failure") commitFailure();
#endif
        else return EXIT_FAILURE;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
    return EXIT_SUCCESS;
}
