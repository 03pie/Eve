/** @file qt_metadata.cpp
 * @brief 元数据夹具；预检不得加载映像或创建该 QObject。
 */
#include <QObject>
class MetadataFixture final : public QObject {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.cordis.IQtPluginV1/1.0" FILE "qt_metadata.json")
};
#include "qt_metadata.moc"
