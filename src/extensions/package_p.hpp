#pragma once
#include <eve/extensions/catalog.hpp>
#include <QJsonObject>

namespace eve::extensions::detail {
ErrorInfo packageError(const QString& message, ErrorCode code = ErrorCode::ConfigInvalid);
Result<Package> unpack(const QString& file, const QString& dir);
Result<void> readPresentation(const QJsonObject& json, Package& package);
bool validIcon(const QByteArray& bytes);
bool protectedIdentity(const Package& package);
bool reclaimArtifact(const QString& store, const QString& sha256);
bool preserveDamagedArtifact(const QString& store, const QString& sha256, const QString& library);
}
