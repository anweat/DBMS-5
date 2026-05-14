#pragma once

#include "../types.h"
#include <QMainWindow>

class QtSessionAdapter;
class AdminPanel;
class DatabaseTreePanel;
class ObjectDetailPanel;
class ResultTablePanel;
class SessionPanel;
class SqlEditorPanel;
class StatusMetaPanel;
class TableEditorPanel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void executeSql(const QString &sql);
    void handleResult(const QueryResult &result);
    void handleTableReady(const QString &database, const QString &table, const QueryResult &result);

private:
    QtSessionAdapter *adapter_;
    AdminPanel *adminPanel_;
    DatabaseTreePanel *databaseTreePanel_;
    ObjectDetailPanel *objectDetailPanel_;
    SessionPanel *sessionPanel_;
    SqlEditorPanel *sqlEditorPanel_;
    ResultTablePanel *resultTablePanel_;
    StatusMetaPanel *statusMetaPanel_;
    TableEditorPanel *tableEditorPanel_;
};
