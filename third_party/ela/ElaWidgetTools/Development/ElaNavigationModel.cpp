#include "ElaNavigationModel.h"
#include <utility>

#include "ElaNavigationNode.h"
#include <QPointer>
#include <memory>

ElaNavigationModel::ElaNavigationModel(QObject* parent)
    : QAbstractItemModel{parent}
{
    _rootNode = new ElaNavigationNode("root");
    _rootNode->setIsRootNode(true);
    _rootNode->setIsExpanderNode(true);
    _pSelectedNode = nullptr;
    _pSelectedExpandedNode = nullptr;
}

ElaNavigationModel::~ElaNavigationModel()
{
    _rootNode->deleteLater();
}

void ElaNavigationModel::_rebuildPresentation(ElaNavigationNode* parentNode, bool presented)
{
    parentNode->_presentedChildren.clear();
    for (auto child : parentNode->getChildrenNodes()) {
        const bool visible = presented && child->isPresentable() &&
                             !(parentNode == _rootNode && !_isMaximalMode && child->getIsCategoryNode());
        QModelIndex childIndex;
        if (visible) {
            const auto row = parentNode->_presentedChildren.size();
            parentNode->_presentedChildren.append(child);
            childIndex = createIndex(row, 0, child);
        }
        child->setModelIndex(childIndex);
        _rebuildPresentation(child, visible);
    }
}

void ElaNavigationModel::refreshPresentation()
{
    if (_changing)
        return;
    const QPointer<ElaNavigationModel> alive(this);
    _changing = true;
    beginResetModel();
    if (!alive)
        return;
    _rebuildPresentation(_rootNode, true);
    if (_pSelectedNode && !_pSelectedNode->getModelIndex().isValid())
        _pSelectedNode = nullptr;
    if (_pSelectedExpandedNode && !_pSelectedExpandedNode->getModelIndex().isValid())
        _pSelectedExpandedNode = nullptr;
    endResetModel();
    if (alive)
        _changing = false;
}

void ElaNavigationModel::setIsMaximalMode(bool isMaximal)
{
    if (_changing || _isMaximalMode == isMaximal)
        return;
    const QPointer<ElaNavigationModel> alive(this);
    _changing = true;
    beginResetModel();
    if (!alive)
        return;
    _isMaximalMode = isMaximal;
    _rebuildPresentation(_rootNode, true);
    endResetModel();
    if (alive)
        _changing = false;
}

bool ElaNavigationModel::getIsMaximalMode()
{
    return _isMaximalMode;
}

QModelIndex ElaNavigationModel::parent(const QModelIndex& child) const
{
    if (!child.isValid() || child.model() != this)
        return {};
    const auto childNode = static_cast<ElaNavigationNode*>(child.internalPointer());
    const auto parentNode = childNode ? childNode->getParentNode() : nullptr;
    if (!parentNode || parentNode == _rootNode || parentNode->getRow() < 0)
        return {};
    return createIndex(parentNode->getRow(), 0, parentNode);
}

QModelIndex ElaNavigationModel::index(int row, int column, const QModelIndex& parentIndex) const
{
    if (row < 0 || column != 0 || (parentIndex.isValid() && (parentIndex.model() != this || parentIndex.column() != 0)))
        return {};
    const auto parentNode =
        parentIndex.isValid() ? static_cast<ElaNavigationNode*>(parentIndex.internalPointer()) : _rootNode;
    if (!parentNode || row >= parentNode->getPresentedChildren().size())
        return {};
    return createIndex(row, column, parentNode->getPresentedChildren().at(row));
}

int ElaNavigationModel::rowCount(const QModelIndex& parentIndex) const
{
    if (parentIndex.isValid() && (parentIndex.model() != this || parentIndex.column() != 0))
        return 0;
    const auto parentNode =
        parentIndex.isValid() ? static_cast<ElaNavigationNode*>(parentIndex.internalPointer()) : _rootNode;
    return parentNode ? parentNode->getPresentedChildren().size() : 0;
}

int ElaNavigationModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant ElaNavigationModel::data(const QModelIndex& index, int role) const
{
    Q_UNUSED(index)
    Q_UNUSED(role)
    return {};
}

ElaNavigationType::NodeResult ElaNavigationModel::_addNode(const QString& title, QString& key, const QString& parentKey,
                                                           ElaIconType::IconName awesome, int keyPoints, bool expander,
                                                           bool category, std::function<bool()> presentation)
{
    if (_changing)
        return ElaNavigationType::TargetNodeInvalid;
    auto parentNode = parentKey.isEmpty() ? _rootNode : getNavigationNode(parentKey);
    if (!parentNode)
        return ElaNavigationType::TargetNodeInvalid;
    if (!parentNode->getIsExpanderNode())
        return ElaNavigationType::TargetNodeTypeError;
    if (parentNode->getDepth() > 10)
        return ElaNavigationType::TargetNodeDepthLimit;
    auto node = std::make_unique<ElaNavigationNode>(title, parentNode);
    node->setDepth(parentNode->getDepth() + 1);
    node->setAwesome(awesome);
    node->setKeyPoints(keyPoints);
    node->setIsExpanderNode(expander);
    node->setIsCategoryNode(category);
    node->setPresentation(std::move(presentation));
    node->setIsVisible(parentNode == _rootNode || (parentNode->getIsVisible() && parentNode->getIsExpanded()));
    const QPointer<ElaNavigationModel> alive(this);
    _changing = true;
    beginResetModel();
    if (!alive)
        return ElaNavigationType::TargetNodeInvalid;
    key = node->getNodeKey();
    parentNode->appendChildNode(node.get());
    _nodesMap.insert(key, node.release());
    _rebuildPresentation(_rootNode, true);
    if (!_pSelectedNode && !expander && !category) {
        auto added = _nodesMap.value(key);
        if (added->getModelIndex().isValid())
            _pSelectedNode = added;
    }
    endResetModel();
    if (alive)
        _changing = false;
    return alive ? ElaNavigationType::Success : ElaNavigationType::TargetNodeInvalid;
}

ElaNavigationType::NodeResult ElaNavigationModel::addExpanderNode(const QString& title, QString& key,
                                                                  ElaIconType::IconName awesome,
                                                                  std::function<bool()> presentation)
{
    return _addNode(title, key, {}, awesome, 0, true, false, std::move(presentation));
}

ElaNavigationType::NodeResult ElaNavigationModel::addExpanderNode(const QString& title, QString& key,
                                                                  const QString& parentKey,
                                                                  ElaIconType::IconName awesome,
                                                                  std::function<bool()> presentation)
{
    if (parentKey.isEmpty())
        return ElaNavigationType::TargetNodeInvalid;
    return _addNode(title, key, parentKey, awesome, 0, true, false, std::move(presentation));
}

ElaNavigationType::NodeResult ElaNavigationModel::addPageNode(const QString& title, QString& key,
                                                              ElaIconType::IconName awesome,
                                                              std::function<bool()> presentation)
{
    return _addNode(title, key, {}, awesome, 0, false, false, std::move(presentation));
}

ElaNavigationType::NodeResult ElaNavigationModel::addPageNode(const QString& title, QString& key,
                                                              const QString& parentKey, ElaIconType::IconName awesome,
                                                              std::function<bool()> presentation)
{
    if (parentKey.isEmpty())
        return ElaNavigationType::TargetNodeInvalid;
    return _addNode(title, key, parentKey, awesome, 0, false, false, std::move(presentation));
}

ElaNavigationType::NodeResult ElaNavigationModel::addPageNode(const QString& title, QString& key, int points,
                                                              ElaIconType::IconName awesome,
                                                              std::function<bool()> presentation)
{
    return _addNode(title, key, {}, awesome, points, false, false, std::move(presentation));
}

ElaNavigationType::NodeResult ElaNavigationModel::addPageNode(const QString& title, QString& key,
                                                              const QString& parentKey, int points,
                                                              ElaIconType::IconName awesome,
                                                              std::function<bool()> presentation)
{
    if (parentKey.isEmpty())
        return ElaNavigationType::TargetNodeInvalid;
    return _addNode(title, key, parentKey, awesome, points, false, false, std::move(presentation));
}

ElaNavigationType::NodeResult ElaNavigationModel::addCategoryNode(const QString& title, QString& key)
{
    return _addNode(title, key, {}, ElaIconType::None, 0, false, true, {});
}

ElaNavigationType::NodeResult ElaNavigationModel::addCategoryNode(const QString& title, QString& key,
                                                                  const QString& parentKey)
{
    if (parentKey.isEmpty())
        return ElaNavigationType::TargetNodeInvalid;
    return _addNode(title, key, parentKey, ElaIconType::None, 0, false, true, {});
}

void ElaNavigationModel::_removeSubtree(ElaNavigationNode* node, QStringList& pages)
{
    for (auto child : node->getChildrenNodes())
        _removeSubtree(child, pages);
    if (!node->getIsExpanderNode())
        pages.append(node->getNodeKey());
    _nodesMap.remove(node->getNodeKey());
    if (_pSelectedNode == node) _pSelectedNode = nullptr;
    if (_pSelectedExpandedNode == node) _pSelectedExpandedNode = nullptr;
    node->getParentNode()->removeChildNode(node);
    node->deleteLater();
}

QStringList ElaNavigationModel::removeNavigationNode(const QString& key)
{
    QStringList pages;
    if (_changing)
        return pages;
    auto node = getNavigationNode(key);
    if (!node)
        return pages;
    const QPointer<ElaNavigationModel> alive(this);
    _changing = true;
    beginResetModel();
    if (!alive)
        return pages;
    _removeSubtree(node, pages);
    _rebuildPresentation(_rootNode, true);
    endResetModel();
    if (alive)
        _changing = false;
    return pages;
}

ElaNavigationNode* ElaNavigationModel::getNavigationNode(const QString& key) const
{
    return _nodesMap.value(key);
}

QModelIndex ElaNavigationModel::getNavigationNodeIndex(const QString& key) const
{
    auto node = getNavigationNode(key);
    return node && node->isPresentable() ? node->getModelIndex() : QModelIndex{};
}

QList<ElaNavigationNode*> ElaNavigationModel::getRootExpanderNodes() const
{
    QList<ElaNavigationNode*> result;
    for (auto node : _rootNode->getPresentedChildren())
        if (node->getIsExpanderNode())
            result.append(node);
    return result;
}

QList<ElaNavigationNode*> ElaNavigationModel::getRootExpandedNodes() const
{
    QList<ElaNavigationNode*> result;
    for (auto node : _rootNode->getPresentedChildren())
        if (node->getIsExpanderNode() && node->getIsExpanded())
            result.append(node);
    return result;
}

QList<ElaNavigationNode*> ElaNavigationModel::getRootCategoryNodes() const
{
    QList<ElaNavigationNode*> result;
    for (auto node : _rootNode->getPresentedChildren())
        if (node->getIsCategoryNode())
            result.append(node);
    return result;
}
