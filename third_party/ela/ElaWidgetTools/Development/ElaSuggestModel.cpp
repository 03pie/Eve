#include "ElaSuggestModel.h"
#include "ElaSuggestBoxPrivate.h"

ElaSuggestModel::ElaSuggestModel(QObject* parent)
    : QAbstractListModel{parent}
{
}

ElaSuggestModel::~ElaSuggestModel()
{
}

int ElaSuggestModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : _suggestionVector.count();
}

QVariant ElaSuggestModel::data(const QModelIndex& index, int role) const
{
    return QVariant();
}

void ElaSuggestModel::setSearchSuggestion(const QVector<ElaSuggestion*>& suggestionVector)
{
    if (_resetting) return;
    if (suggestionVector.count() == 0)
    {
        return;
    }
    QVector<QPointer<ElaSuggestion>> guarded;
    for (auto suggestion : suggestionVector) guarded.append(suggestion);
    QPointer<ElaSuggestModel> alive(this);
    _resetting = true;
    beginResetModel();
    if (!alive) return;
    _suggestionVector = guarded;
    endResetModel();
    if (alive) _resetting = false;
}

void ElaSuggestModel::clearSearchNode()
{
    if (_resetting) return;
    _resetting = true;
    QPointer<ElaSuggestModel> alive(this);
    beginResetModel();
    if (!alive) return;
    this->_suggestionVector.clear();
    endResetModel();
    if (alive) _resetting = false;
}

ElaSuggestion* ElaSuggestModel::getSearchSuggestion(int row)
{
    if (row < 0 || row >= _suggestionVector.count())
    {
        return nullptr;
    }
    return _suggestionVector[row].data();
}
