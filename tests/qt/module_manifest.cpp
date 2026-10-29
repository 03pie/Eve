/** @file module_manifest.cpp
 * @brief 用真实临时文件验证装载前拒绝、规范根目录和摘要，不装载测试文件。
 */
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cordis/module/manifest.hpp>
#include <iostream>
#include <stdexcept>

using namespace cordis;
using namespace cordis::module;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error(std::string(#x) + " at " + std::to_string(__LINE__));                             \
    } while (false)

QJsonObject manifest(const QByteArray& payload = "abc")
{
    return {{"manifestVersion", 1},
            {"moduleId", "org.cordis.example"},
            {"moduleVersion", "1.0.0"},
            {"loader", "c-abi"},
            {"library", "example.dll"},
            {"sha256", QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())},
            {"platform", "windows-x64"},
            {"abi", QJsonObject{{"major", 1}, {"minor", 0}}},
            {"plugins", QJsonArray{"org.cordis.notes"}},
            {"unloadPolicy", "resident"}};
}
std::string json(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
}
void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    CHECK(file.write(bytes) == bytes.size());
    CHECK(file.flush());
}
void rejected(const QJsonObject& object)
{
    auto result = parseManifest(json(object));
    CHECK(!result.hasValue() && result.error().phase == "module.manifest");
}
void schema()
{
    auto value = parseManifest(json(manifest()));
    CHECK(value.hasValue());
    CHECK(value.value().moduleId == "org.cordis.example" && value.value().plugins.size() == 1);
    CHECK(value.value().loader == ModuleLoader::CAbi);
    auto qt = manifest();
    qt["loader"] = "qt-ui";
    CHECK(parseManifest(json(qt)).value().loader == ModuleLoader::QtUi);
    CHECK(value.value().sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    for (const auto& key : manifest().keys()) {
        auto missing = manifest();
        missing.remove(key);
        rejected(missing);
        auto null = manifest();
        null[key] = QJsonValue(QJsonValue::Null);
        rejected(null);
    }
    auto unknown = manifest();
    unknown["typo"] = true;
    rejected(unknown);
    auto abi = manifest();
    abi["abi"] = QJsonObject{{"major", 2}, {"minor", 0}};
    CHECK(parseManifest(json(abi)).error().code == ErrorCode::AbiMismatch);
    abi["abi"] = QJsonObject{{"major", 1}, {"minor", 1}};
    rejected(abi);
    abi["abi"] = QJsonObject{{"major", 1}, {"minor", 0}, {"reserved", 0}};
    rejected(abi);
    for (const auto& pair : std::vector<std::pair<QString, QJsonValue>>{{"manifestVersion", "1"},
                                                                        {"manifestVersion", 1.5},
                                                                        {"platform", "linux-x64"},
                                                                        {"loader", "unknown"},
                                                                        {"unloadPolicy", "unload"},
                                                                        {"moduleVersion", "01.0.0"},
                                                                        {"moduleVersion", "1.0.0-beta"},
                                                                        {"moduleId", "org..bad"},
                                                                        {"moduleId", "bad\n"},
                                                                        {"sha256", "实际产物摘要"},
                                                                        {"sha256", QString(63, 'a')},
                                                                        {"plugins", QJsonArray{}},
                                                                        {"plugins", QJsonArray{"same", "same"}},
                                                                        {"plugins", QJsonArray{1}}}) {
        auto bad = manifest();
        bad[pair.first] = pair.second;
        rejected(bad);
    }
    auto uppercase = manifest();
    uppercase["sha256"] = uppercase["sha256"].toString().toUpper();
    CHECK(parseManifest(json(uppercase)).value().sha256 == value.value().sha256);
    for (const auto* number : {"1.0", "1e0", "1.00000000000000000001", "4294967296", "-1"}) {
        auto text = json(manifest());
        const auto at = text.find("\"manifestVersion\":1");
        CHECK(at != std::string::npos);
        text.replace(at, std::string("\"manifestVersion\":1").size(), std::string("\"manifestVersion\":") + number);
        CHECK(!parseManifest(text).hasValue());
    }
    auto tooMany = manifest();
    QJsonArray plugins;
    for (int i = 0; i < 65; ++i)
        plugins.append("plugin" + QString::number(i));
    tooMany["plugins"] = plugins;
    rejected(tooMany);
}
void duplicates()
{
    const auto source = json(manifest());
    CHECK(!parseManifest("{\"moduleId\":\"hidden\"," + source.substr(1)).hasValue());
    CHECK(!parseManifest("{\"module\\u0049d\":\"hidden\"," + source.substr(1)).hasValue());
    auto nested = source;
    const auto at = nested.find("\"major\":1");
    CHECK(at != std::string::npos);
    nested.insert(at, "\"major\":2,");
    CHECK(!parseManifest(nested).hasValue());
    CHECK(!parseManifest(source + "{}").hasValue());
    CHECK(!parseManifest("[]").hasValue());
    CHECK(!parseManifest("{bad}").hasValue());
    CHECK(!parseManifest(std::string("\xff") + source).hasValue());
    CHECK(!parseManifest(source + std::string(1, '\0')).hasValue());
    auto deep = manifest();
    deep["unexpected"] = QJsonDocument::fromJson(QByteArray(17, '[') + "0" + QByteArray(17, ']')).array();
    rejected(deep);
}
void library_names()
{
    for (const auto* path : {"../escape.dll", "sub/a.dll", "sub\\a.dll", "C:\\a.dll", "C:a.dll", "a.dll:stream",
                             "\\\\host\\a.dll", "a.exe", "a.dll ", "CON.dll", "nul.data.dll", "LPT1.dll", "a\n.dll"}) {
        auto bad = manifest();
        bad["library"] = path;
        rejected(bad);
    }
    auto good = manifest();
    good["library"] = "a_1-2.DLL";
    CHECK(parseManifest(json(good)).hasValue());
}
void files()
{
    QTemporaryDir root;
    CHECK(root.isValid());
    CHECK(QDir(root.path()).mkdir("module"));
    const auto directory = root.path() + "/module/";
    write(directory + "module.json", QByteArray::fromStdString(json(manifest())));
    write(directory + "example.dll", "abc");
    const auto rootBytes = root.path().toUtf8().toStdString();
    auto result = preflightModule(rootBytes, "module/module.json");
    CHECK(result.hasValue() && result.value().isValid());
    CHECK(result.value().libraryBytes() == 3);
    CHECK(result.value().libraryPath() ==
          QFileInfo(directory + "example.dll").canonicalFilePath().toUtf8().toStdString());
    auto moved = std::move(result).value();
    CHECK(moved.isValid() && !result.value().isValid());
    const auto copy = moved;
    CHECK(copy.isValid() && moved.isValid());
    write(directory + "example.dll", "abd");
    auto mismatch = preflightModule(rootBytes, "module/module.json");
    CHECK(!mismatch.hasValue() && mismatch.error().phase == "module.hash");
    CHECK(QFile::remove(directory + "example.dll"));
    CHECK(!preflightModule(rootBytes, "module/module.json").hasValue());
    CHECK(QDir().mkdir(directory + "example.dll"));
    CHECK(!preflightModule(rootBytes, "module/module.json").hasValue());
}
void paths()
{
    QTemporaryDir outer;
    CHECK(outer.isValid());
    CHECK(QDir(outer.path()).mkdir("plugins"));
    CHECK(QDir(outer.path()).mkdir("plugins-other"));
    const auto root = (outer.path() + "/plugins").toUtf8().toStdString();
    const auto external = outer.path() + "/plugins-other/module.json";
    write(external, QByteArray::fromStdString(json(manifest())));
    write(outer.path() + "/plugins-other/example.dll", "abc");
    CHECK(!preflightModule(root, external.toUtf8().toStdString()).hasValue());
    CHECK(!preflightModule(root, "../plugins-other/module.json").hasValue());
    CHECK(!preflightModule("relative-root", "module.json").hasValue());
    CHECK(!preflightModule(root, "").hasValue());
    CHECK(!preflightModule(root, std::string("a\0b", 3)).hasValue());
    CHECK(!preflightModule(root, std::string(1, static_cast<char>(0xff))).hasValue());
    CHECK(!preflightModule(root, "missing.json").hasValue());
    CHECK(!preflightModule(root + "/missing", "module.json").hasValue());
}
/** @brief 根必须包含盘符或完整 UNC 共享，不能暗中依赖当前盘；非 ASCII 路径应正常工作。 */
void root_guards()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const auto directory = temporary.path() + QString::fromUtf8("/插件目录");
    CHECK(QDir().mkdir(directory));
    write(directory + "/module.json", QByteArray::fromStdString(json(manifest())));
    write(directory + "/example.dll", "abc");
    const auto root = directory.toUtf8().toStdString();
    CHECK(preflightModule(root, "module.json").hasValue());
    CHECK(preflightModule(root, (directory + "/module.json").toUtf8().toStdString()).hasValue());
    const auto extended = (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(directory)).toUtf8().toStdString();
    CHECK(preflightModule(extended, "module.json").hasValue());
    const auto driveLess = QDir::fromNativeSeparators(directory).mid(2).toUtf8().toStdString();
    CHECK(!preflightModule(driveLess, "module.json").hasValue());
    CHECK(!preflightModule(root, driveLess + "/module.json").hasValue());
    CHECK(!preflightModule(root, root.substr(0, 2) + "module.json").hasValue());
    CHECK(!preflightModule("//server", "module.json").hasValue());
    CHECK(!preflightModule("//./", "module.json").hasValue());
}
/** @brief 不能把仍可写的产物/声明当作稳定预检来源；普通只读共享应保持可用。 */
void sharing()
{
    QTemporaryDir temporary;
    CHECK(temporary.isValid());
    const auto root = temporary.path().toUtf8().toStdString();
    write(temporary.path() + "/module.json", QByteArray::fromStdString(json(manifest())));
    write(temporary.path() + "/example.dll", "abc");
    for (const auto* name : {"example.dll", "module.json"}) {
        QFile writer(temporary.path() + QLatin1Char('/') + QLatin1String(name));
        CHECK(writer.open(QIODevice::ReadWrite));
        const bool rejectedWriter = !preflightModule(root, "module.json").hasValue();
        writer.close();
        CHECK(rejectedWriter);
        CHECK(preflightModule(root, "module.json").hasValue());
    }
    QFile reader(temporary.path() + "/example.dll");
    CHECK(reader.open(QIODevice::ReadOnly));
    CHECK(preflightModule(root, "module.json").hasValue());
}
void limits()
{
    const auto source = json(manifest());
    PreflightLimits options;
    options.maximumManifestBytes = source.size();
    CHECK(parseManifest(source, options).hasValue());
    --options.maximumManifestBytes;
    CHECK(!parseManifest(source, options).hasValue());
    options.maximumManifestBytes = 0;
    CHECK(!parseManifest(source, options).hasValue());
    options.maximumManifestBytes = 1024 * 1024 + 1;
    CHECK(!parseManifest(source, options).hasValue());
    options = {};
    options.maximumLibraryBytes = 0;
    CHECK(!parseManifest(source, options).hasValue());
    QTemporaryDir root;
    CHECK(root.isValid());
    const auto payload = QByteArray(128 * 1024 + 1, 'x'); // 跨越多次 64 KiB 读取。
    write(root.path() + "/module.json", QByteArray::fromStdString(json(manifest(payload))));
    write(root.path() + "/example.dll", payload);
    const auto path = root.path().toUtf8().toStdString();
    options = {};
    options.maximumLibraryBytes = static_cast<std::uint64_t>(payload.size());
    CHECK(preflightModule(path, "module.json", options).hasValue());
    --options.maximumLibraryBytes;
    CHECK(!preflightModule(path, "module.json", options).hasValue());
    options = {};
    options.maximumManifestBytes = 1;
    CHECK(!preflightModule(path, "module.json", options).hasValue());
    write(root.path() + "/example.dll", "");
    CHECK(!preflightModule(path, "module.json").hasValue());
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "schema")
            schema();
        else if (name == "duplicates")
            duplicates();
        else if (name == "library_names")
            library_names();
        else if (name == "files")
            files();
        else if (name == "paths")
            paths();
        else if (name == "root_guards")
            root_guards();
        else if (name == "sharing")
            sharing();
        else if (name == "limits")
            limits();
        else
            throw std::runtime_error("unknown case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
