#include "desktop_language_p.hpp"
#include "desktop_text.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <filesystem>
#include <utility>

namespace eve::ui::detail {
namespace {
constexpr int maxPackBytes = 512 * 1024;
constexpr int maxMessages = 8192;

struct LanguagePack {
    QString locale, name;
    QJsonObject messages;
    QByteArray bytes;
};

bool isEnglish(const QString& locale) { return locale.compare(QLatin1String("en"), Qt::CaseInsensitive) == 0; }

bool validDesktopLocale(const QString& locale)
{
    static const QRegularExpression localeTag(QStringLiteral("\\A[A-Za-z]{2,3}(?:-[A-Za-z0-9]{2,8}){0,4}\\z"));
    return localeTag.match(locale).hasMatch();
}

QDir languageDirectory(const QString& settingsFile)
{
    return QDir(QFileInfo(settingsFile).dir().filePath(QStringLiteral("language-packs")));
}

QString selectionFile(const QDir& directory)
{
    return QDir::cleanPath(directory.filePath(QStringLiteral("../locale.json")));
}

QString selectedLocale(const QDir& directory)
{
    QFile file(selectionFile(directory));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024) return QStringLiteral("en");
    const auto locale = QJsonDocument::fromJson(file.read(1025)).object().value("locale").toString().toLower();
    return validDesktopLocale(locale) ? locale : QStringLiteral("en");
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool selectLocale(const QDir& directory, const QString& locale)
{
    return writeFile(selectionFile(directory), QJsonDocument(QJsonObject{{"locale", locale}}).toJson(QJsonDocument::Indented));
}

QString languageFile(const QDir& directory, const QString& locale)
{
    return directory.filePath(QStringLiteral("locale-") + locale.toLower() + QStringLiteral(".evelang"));
}

QSet<QString> formatArguments(const QString& text)
{
    static const QRegularExpression argument(QStringLiteral("%L?[1-9][0-9]?(?![0-9])"));
    QSet<QString> arguments;
    auto matches = argument.globalMatch(text);
    while (matches.hasNext()) arguments.insert(matches.next().captured());
    return arguments;
}

Result<LanguagePack> readPack(const QString& source)
{
    using Output = Result<LanguagePack>;
    QFile file(source);
    if (!source.endsWith(QLatin1String(".evelang"), Qt::CaseInsensitive) || !file.open(QIODevice::ReadOnly))
        return Output::failure(makeError(ErrorCode::ConfigIo, desktopText("language.cannotReadTheEvelangFile").toStdString()));
    const auto bytes = file.read(maxPackBytes + 1);
    const auto utf8 = bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes;
    if (file.error() != QFileDevice::NoError || bytes.size() > maxPackBytes ||
        QString::fromUtf8(utf8).toUtf8() != utf8)
        return Output::failure(makeError(ErrorCode::ConfigInvalid, desktopText("language.aLanguagePackMustBeUTF8").toStdString()));
    const auto pack = QJsonDocument::fromJson(utf8).object();
    const auto locale = pack.value("locale").toString().toLower();
    const auto name = pack.value("name").toString(locale);
    const auto entries = pack.value("translations");
    const auto messages = entries.toObject();
    if (pack.value("formatVersion") != QJsonValue(1) || !validDesktopLocale(locale) || isEnglish(locale) ||
        (pack.contains("name") && !pack.value("name").isString()) ||
        name.trimmed().isEmpty() || name.size() > 128 || name.contains(QChar::Null) ||
        !entries.isObject() || messages.isEmpty() || messages.size() > maxMessages)
        return Output::failure(makeError(ErrorCode::ConfigInvalid, desktopText("language.invalidLanguagePackMetadataOrTranslations").toStdString()));
    for (auto entry = messages.constBegin(); entry != messages.constEnd(); ++entry) {
        const auto text = entry.value().toString();
        if (entry.key().isEmpty() || text.isEmpty() || entry.key().contains(QChar::Null) || text.contains(QChar::Null))
            return Output::failure(makeError(ErrorCode::ConfigInvalid,
                desktopText("language.languagePackEntriesRequireASourceAnd").toStdString()));
        const auto english = eve::detail::desktopDefaultMessages().value(entry.key()).toString();
        const auto expected = formatArguments(english);
        if (!expected.isEmpty() && expected != formatArguments(text))
            return Output::failure(makeError(ErrorCode::ConfigInvalid,
                desktopText("language.translationArgumentsDiffer").arg(entry.key()).toStdString()));
    }
    return Output::success({locale, name, messages, bytes});
}

class DesktopTranslator final : public eve::detail::DesktopMessageTranslator {
public:
    DesktopTranslator(QJsonObject messages, DesktopLanguageState language)
        : DesktopMessageTranslator(std::move(messages)), language_(std::move(language)) {}

    const DesktopLanguageState& language() const { return language_; }

    QString translate(const char* context, const char* source, const char* disambiguation, int count) const override
    {
        const QLatin1String domain(context);
        // Ela context menus still provide their upstream Chinese source labels.
        if (domain != QLatin1String("ElaLineEdit") && domain != QLatin1String("ElaSpinBox") &&
            domain != QLatin1String("ElaSpinBoxPrivate") &&
            domain != QLatin1String("ElaScrollBar") && domain != QLatin1String("ElaPlainTextEdit"))
            return DesktopMessageTranslator::translate(context, source, disambiguation, count);
        static const QHash<QByteArray, const char*> labels{
            {"撤销", "ela.menu.undo"}, {"恢复", "ela.menu.redo"},
            {"剪切", "ela.menu.cut"}, {"复制", "ela.menu.copy"},
            {"粘贴", "ela.menu.paste"}, {"删除", "ela.menu.delete"},
            {"全选", "ela.menu.selectAll"}, {"增加", "ela.menu.increase"},
            {"减少", "ela.menu.decrease"}, {"滚动到此处", "ela.menu.scrollHere"},
            {"左边缘", "ela.menu.leftEdge"}, {"右边缘", "ela.menu.rightEdge"},
            {"顶端", "ela.menu.top"}, {"底部", "ela.menu.bottom"},
            {"向左翻页", "ela.menu.pageLeft"}, {"向右翻页", "ela.menu.pageRight"},
            {"向上翻页", "ela.menu.pageUp"}, {"向下翻页", "ela.menu.pageDown"},
            {"向左滚动", "ela.menu.scrollLeft"}, {"向右滚动", "ela.menu.scrollRight"},
            {"向上滚动", "ela.menu.scrollUp"}, {"向下滚动", "ela.menu.scrollDown"}
        };
        const auto id = labels.value(source);
        return id ? message(QString::fromLatin1(id)) : QString{};
    }

private:
    DesktopLanguageState language_;
};

QString languageId(const QString& locale) { return QStringLiteral("language/") + locale; }

QString digest(const LanguagePack& pack)
{
    return QString::fromLatin1(QCryptographicHash::hash(pack.bytes, QCryptographicHash::Sha256).toHex());
}

Result<QString> installedDigest(const QString& path)
{
    using Output = Result<QString>;
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return Output::success({});
    QFile file(path);
    if (info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly))
        return Output::failure(makeError(ErrorCode::ConfigIo, desktopText("language.cannotReadTheInstalledLanguagePack").toStdString()));
    const auto bytes = file.read(maxPackBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maxPackBytes)
        return Output::failure(makeError(ErrorCode::ConfigIo, desktopText("language.cannotReadTheInstalledLanguagePack").toStdString()));
    return Output::success(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
}

ExtensionItem describePack(const LanguagePack& pack)
{
    ExtensionItem item;
    item.kind = ExtensionKind::Language;
    item.id = languageId(pack.locale);
    item.pluginId = pack.locale;
    item.name = pack.name;
    item.description = desktopText("language.displayLanguageForTheEveWorkbenchRestart");
    item.categories = QStringList{desktopText("extensions.languagePacks")};
    item.packageHash = digest(pack);
    item.packageBytes = pack.bytes.size();
    return item;
}

ErrorInfo languageConflict()
{
    return makeError(ErrorCode::ConfigConflict, desktopText("language.theLanguagePackHasChangedReviewIt").toStdString());
}

Task<void> ioFailure(const QString& message)
{
    return failedTask(makeError(ErrorCode::ConfigIo, message.toStdString()));
}

bool renamePack(const QString& source, const QString& target)
{
    // QFile::rename can fall back to copy and delete. Same-directory staging needs one rename.
    std::error_code error;
    std::filesystem::rename(std::filesystem::u8path(source.toUtf8().toStdString()),
                            std::filesystem::u8path(target.toUtf8().toStdString()), error);
    return !error;
}

Task<void> removeSelectedPack(const QDir& directory, const QString& target, const QString& locale)
{
    const auto staged = directory.filePath(QStringLiteral(".retiring-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!renamePack(target, staged))
        return ioFailure(desktopText("language.cannotRemoveTheInstalledLanguagePack"));

    const auto restoreFailure = [&] {
        return ioFailure(desktopText("language.cannotRestoreTheLanguagePack").arg(QDir::toNativeSeparators(staged)));
    };
    if (!selectLocale(directory, QStringLiteral("en"))) {
        if (!renamePack(staged, target)) return restoreFailure();
        return ioFailure(desktopText("language.cannotSaveTheSelectedDisplayLanguage"));
    }
    if (QFile::remove(staged)) return completedTask();
    if (!renamePack(staged, target)) return restoreFailure();
    if (!selectLocale(directory, locale)) return ioFailure(desktopText("language.cannotRestoreThePreviousDisplayLanguage"));
    return ioFailure(desktopText("language.cannotRemoveTheInstalledLanguagePack"));
}

Task<void> installReviewedPack(const QDir& directory, const ExtensionPreview& preview)
{
    const auto source = readPack(preview.path);
    if (!source.hasValue()) return failedTask(source.error());
    const auto& pack = source.value();
    if (languageId(pack.locale) != preview.item.id || digest(pack) != preview.sha256)
        return failedTask(languageConflict());
    if (!QDir().mkpath(directory.absolutePath()))
        return ioFailure(desktopText("language.cannotCreateTheLanguagePackDirectory"));
    QLockFile lock(directory.filePath(QStringLiteral(".language.lock")));
    if (!lock.tryLock(0)) return ioFailure(desktopText("language.languagePacksAreBeingModifiedByAnother"));
    const auto target = languageFile(directory, pack.locale);
    const auto old = installedDigest(target);
    if (!old.hasValue()) return failedTask(old.error());
    if (old.value() != preview.installedHash) return failedTask(languageConflict());
    if (!writeFile(target, pack.bytes)) return ioFailure(desktopText("language.cannotInstallTheLanguagePack"));
    return completedTask();
}
}

std::shared_ptr<QTranslator> acquireDesktopTranslator(const QString& settingsFile, DesktopLanguageState& active)
{
    // Qt resolves translations process-wide. Reuse the first window's snapshot until the last owner closes.
    static std::weak_ptr<DesktopTranslator> installed;
    if (auto translator = installed.lock()) {
        active = translator->language();
        return translator;
    }
    QJsonObject messages;
    active = {};
    if (!settingsFile.isEmpty()) {
        const auto directory = languageDirectory(settingsFile);
        const auto wanted = selectedLocale(directory);
        const auto path = languageFile(directory, wanted);
        if (!isEnglish(wanted) && !QFileInfo(path).isSymLink()) {
            const auto pack = readPack(path);
            if (pack.hasValue() && pack.value().locale == wanted) {
                active = {wanted, digest(pack.value())};
                messages = pack.value().messages;
            }
        }
    }
    auto translator = std::make_shared<DesktopTranslator>(std::move(messages), active);
    installed = translator;
    QCoreApplication::installTranslator(translator.get());
    return translator;
}

std::shared_ptr<ExtensionControls> withLanguageExtensions(std::shared_ptr<ExtensionControls> host,
    const QString& settingsFile, DesktopLanguageState active, std::function<bool()> available)
{
    if (settingsFile.isEmpty()) return host;
    const auto directory = languageDirectory(settingsFile);
    auto controls = std::make_shared<ExtensionControls>();
    controls->list = [host, directory, active] {
        auto items = host->list();
        if (!items.hasValue()) return items;
        const auto selected = selectedLocale(directory);
        for (const auto& file : directory.entryList({QStringLiteral("locale-*.evelang")}, QDir::Files | QDir::NoSymLinks)) {
            const auto path = directory.filePath(file);
            const auto pack = readPack(path);
            if (!pack.hasValue() || languageFile(directory, pack.value().locale) != path) continue;
            const auto& value = pack.value();
            auto item = describePack(value);
            item.current = value.locale == active.locale;
            const bool enabled = value.locale == selected;
            item.pending = item.current != enabled || (item.current && item.packageHash != active.packageHash);
            item.state = item.pending ? desktopText("extensions.takesEffectAfterRestart") :
                item.current ? desktopText("language.currentDisplayLanguage") : desktopText("extensions.installed");
            item.actions = {enabled ? ExtensionAction::Disable : ExtensionAction::Enable, ExtensionAction::Install,
                            ExtensionAction::Uninstall};
            if (item.pending) item.actions.push_back(ExtensionAction::Restart);
            items.value().push_back(std::move(item));
        }
        return items;
    };
    controls->preview = [host, directory, available](QString path) {
        if (!path.endsWith(QLatin1String(".evelang"), Qt::CaseInsensitive)) return host->preview(std::move(path));
        Promise<ExtensionPreview> result;
        const auto reject = [&](ErrorInfo error) {
            result.reject(std::move(error));
            return result.task();
        };
        if (!available()) return reject(makeError(ErrorCode::ContextClosing, desktopText("language.desktopClosing").toStdString()));
        const auto loaded = readPack(path);
        if (!loaded.hasValue()) return reject(loaded.error());
        const auto& pack = loaded.value();
        const auto installed = installedDigest(languageFile(directory, pack.locale));
        if (!installed.hasValue()) return reject(installed.error());
        auto item = describePack(pack);
        item.state = desktopText("language.readyToInstall");
        const auto hash = item.packageHash;
        result.resolve({std::move(item), std::move(path), hash, {}, installed.value()});
        return result.task();
    };
    controls->installReviewed = [host, directory, available](const ExtensionPreview& preview) {
        if (preview.item.kind != ExtensionKind::Language)
            return host->installReviewed ? host->installReviewed(preview)
                : host->apply(ExtensionAction::Install, preview.item.id, preview.path, preview.sha256);
        if (!available()) return failedTask(makeError(ErrorCode::ContextClosing, desktopText("language.desktopClosing").toStdString()));
        return installReviewedPack(directory, preview);
    };
    controls->apply = [host, directory, available](ExtensionAction action, QString id, QString path, QString hash) {
        using Action = ExtensionAction;
        if (!id.startsWith(QLatin1String("language/")) || action == Action::Restart)
            return host->apply(action, std::move(id), std::move(path), std::move(hash));
        if (!available()) return failedTask(makeError(ErrorCode::ContextClosing, desktopText("language.desktopClosing").toStdString()));
        if (action != Action::Enable && action != Action::Disable && action != Action::Uninstall)
            return failedTask(makeError(ErrorCode::InvalidArgument, desktopText("language.unsupportedAction").toStdString()));
        const auto locale = id.mid(QStringLiteral("language/").size());
        if (!validDesktopLocale(locale) || isEnglish(locale))
            return failedTask(makeError(ErrorCode::InvalidArgument, desktopText("language.invalidIdentity").toStdString()));
        QLockFile lock(directory.filePath(QStringLiteral(".language.lock")));
        if (!lock.tryLock(0)) return ioFailure(desktopText("language.languagePacksAreBeingModifiedByAnother"));
        const auto target = languageFile(directory, locale);
        if (QFileInfo(target).isSymLink()) return failedTask(languageConflict());
        const auto loaded = readPack(target);
        if (!loaded.hasValue() || loaded.value().locale != locale || digest(loaded.value()) != hash)
            return failedTask(languageConflict());
        const auto selected = selectedLocale(directory);
        if (action == Action::Uninstall) {
            if (selected == locale) return removeSelectedPack(directory, target, locale);
            return QFile::remove(target) ? completedTask() : ioFailure(desktopText("language.cannotRemoveTheInstalledLanguagePack"));
        }
        if (action == Action::Disable && selected != locale) {
            if (isEnglish(selected)) return completedTask();
            return failedTask(makeError(ErrorCode::ConfigConflict, desktopText("language.selectionChanged").toStdString()));
        }
        return selectLocale(directory, action == Action::Enable ? locale : QStringLiteral("en"))
            ? completedTask() : ioFailure(desktopText("language.cannotSaveTheSelectedDisplayLanguage"));
    };
    return controls;
}
}
