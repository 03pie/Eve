#ifndef ELAWORKSPACE_ELAWIDGETTOOLS_DEVELOPERCOMPONENTS_ELASUGGESTMODEL_H_
#define ELAWORKSPACE_ELAWIDGETTOOLS_DEVELOPERCOMPONENTS_ELASUGGESTMODEL_H_

#include <QAbstractListModel>
#include <QPointer>

#include "ElaWidgetToolsDef.h"
class ElaSuggestion;
class ElaSuggestModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ElaSuggestModel(QObject* parent = nullptr);
    ~ElaSuggestModel();
    int rowCount(const QModelIndex& parent = QModelIndex()) const;
    QVariant data(const QModelIndex& index, int role) const;
    void setSearchSuggestion(const QVector<ElaSuggestion*>& suggestionVector);
    void clearSearchNode();
    ElaSuggestion* getSearchSuggestion(int row);

private:
    bool _resetting{false};
    QVector<QPointer<ElaSuggestion>> _suggestionVector; // 撤销建议后不保留悬空指针
};

#endif // ELAWORKSPACE_ELAWIDGETTOOLS_DEVELOPERCOMPONENTS_ELASUGGESTMODEL_H_

