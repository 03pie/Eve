#pragma once
#include <eve/extensions/catalog.hpp>
#include <QJsonObject>

namespace eve::extensions::detail {
ErrorInfo packageError(const QString& message, ErrorCode code = ErrorCode::ConfigInvalid);
Result<Package> unpack(const QString& file, const QString& dir);
Result<void> readPresentation(const QJsonObject& json, Package& package);
struct MarkdownMember {
    const char* filename;
    const char* catalogKey;
    QString Package::*text;
};
inline constexpr MarkdownMember markdownMembers[]{
    {"README.md", "readme", &Package::readme},
    {"CHANGELOG.md", "changelog", &Package::changelog}
};
Result<QString> decodeMarkdown(const QByteArray& bytes, const MarkdownMember& member);
bool validIcon(const QByteArray& bytes);
bool protectedIdentity(const Package& package);
bool reclaimArtifact(const QString& store, const QString& sha256);
bool preserveDamagedArtifact(const QString& store, const QString& sha256, const QString& library);
}
