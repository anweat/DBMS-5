#pragma once

#include "CatalogTypes.h"
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

class DatabaseTreePanel : public QWidget
{
    Q_OBJECT

public:
    explicit DatabaseTreePanel(QWidget *parent = nullptr);

public slots:
    void setCatalog(const CatalogSnapshot &catalog);

signals:
    void refreshRequested();
    void tableOpenRequested(const QString &database, const QString &table);
    void databaseUseRequested(const QString &database);
    void objectDetailRequested(const QString &title, const QStringList &lines);
    void sqlRequested(const QString &sql);

private:
    void openCurrentItem(QTreeWidgetItem *item);
    void selectCurrentItem(QTreeWidgetItem *item);

    QTreeWidget *tree_;
};
