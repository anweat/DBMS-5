#include "MainWindow.h"

#include "AdminPanel.h"
#include "DatabaseTreePanel.h"
#include "QtSessionAdapter.h"
#include "ResultTablePanel.h"
#include "SessionPanel.h"
#include "SqlEditorPanel.h"
#include "StatusMetaPanel.h"
#include "TableEditorPanel.h"

#include <QMenuBar>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      adapter_(new QtSessionAdapter(QStringLiteral("data"), this)),
      adminPanel_(new AdminPanel(this)),
      databaseTreePanel_(new DatabaseTreePanel(this)),
      sessionPanel_(new SessionPanel(this)),
      sqlEditorPanel_(new SqlEditorPanel(this)),
      resultTablePanel_(new ResultTablePanel(this)),
      statusMetaPanel_(new StatusMetaPanel(this)),
      tableEditorPanel_(new TableEditorPanel(this))
{
    auto *mainSplitter = new QSplitter(Qt::Horizontal, this);
    auto *leftPanel = new QWidget(mainSplitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(sessionPanel_);
    leftLayout->addWidget(databaseTreePanel_, 1);

    auto *workTabs = new QTabWidget(mainSplitter);
    workTabs->addTab(tableEditorPanel_, tr("Table Data"));
    workTabs->addTab(resultTablePanel_, tr("Query Result"));
    workTabs->addTab(sqlEditorPanel_, tr("SQL"));

    auto *sideTabs = new QTabWidget(mainSplitter);
    sideTabs->addTab(statusMetaPanel_, tr("Status"));

    auto *adminScroll = new QScrollArea(sideTabs);
    adminScroll->setObjectName(QStringLiteral("adminPanelScrollArea"));
    adminScroll->setWidget(adminPanel_);
    adminScroll->setWidgetResizable(true);
    adminScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    adminScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sideTabs->addTab(adminScroll, tr("Admin"));

    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(workTabs);
    mainSplitter->addWidget(sideTabs);
    mainSplitter->setObjectName(QStringLiteral("mainSplitter"));
    mainSplitter->setChildrenCollapsible(false);
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 6);
    mainSplitter->setStretchFactor(2, 3);

    setCentralWidget(mainSplitter);
    setWindowTitle(tr("DBMS Qt"));
    resize(1420, 820);

    sqlEditorPanel_->setExamples({
        QStringLiteral("CONNECT 'root' IDENTIFIED BY 'root'"),
        QStringLiteral("CREATE DATABASE demo"),
        QStringLiteral("USE demo"),
        QStringLiteral("CREATE TABLE users (uid INT PRIMARY KEY, region VARCHAR(20))"),
        QStringLiteral("CREATE TABLE orders (oid INT PRIMARY KEY, user_id INT, amount DOUBLE)"),
        QStringLiteral("INSERT INTO users (uid, region) VALUES (1, 'east')"),
        QStringLiteral("INSERT INTO orders (oid, user_id, amount) VALUES (100, 1, 42.5)"),
        QStringLiteral("SELECT u.region, COUNT(o.oid) AS cnt, SUM(o.amount) AS total "
                       "FROM users u JOIN orders o ON u.uid = o.user_id "
                       "GROUP BY u.region ORDER BY cnt DESC"),
        QStringLiteral("SHOW USERS")
    });

    connect(sessionPanel_, &SessionPanel::connectRequested,
            adapter_, &QtSessionAdapter::connectUser);
    connect(databaseTreePanel_, &DatabaseTreePanel::refreshRequested,
            adapter_, &QtSessionAdapter::refreshCatalog);
    connect(databaseTreePanel_, &DatabaseTreePanel::tableOpenRequested,
            adapter_, &QtSessionAdapter::loadTable);
    connect(databaseTreePanel_, &DatabaseTreePanel::sqlRequested,
            this, &MainWindow::executeSql);
    connect(tableEditorPanel_, &TableEditorPanel::sqlRequested,
            this, &MainWindow::executeSql);
    connect(adminPanel_, &AdminPanel::sqlRequested,
            this, &MainWindow::executeSql);
    connect(adminPanel_, &AdminPanel::refreshRequested,
            adapter_, &QtSessionAdapter::refreshCatalog);
    connect(sqlEditorPanel_, &SqlEditorPanel::executeRequested,
            this, &MainWindow::executeSql);
    connect(adapter_, &QtSessionAdapter::resultReady,
            this, &MainWindow::handleResult);
    connect(adapter_, &QtSessionAdapter::tableReady,
            this, &MainWindow::handleTableReady);
    connect(adapter_, &QtSessionAdapter::catalogReady,
            databaseTreePanel_, &DatabaseTreePanel::setCatalog);
    connect(adapter_, &QtSessionAdapter::sessionChanged,
            sessionPanel_, &SessionPanel::setSessionInfo);

    auto *fileMenu = menuBar()->addMenu(tr("File"));
    fileMenu->addAction(tr("Refresh Catalog"), adapter_, &QtSessionAdapter::refreshCatalog);
    fileMenu->addAction(tr("Exit"), this, &QWidget::close);
    statusBar()->showMessage(tr("Ready"));
    adapter_->refreshCatalog();
}

void MainWindow::executeSql(const QString &sql)
{
    adapter_->executeSql(sql);
}

void MainWindow::handleResult(const QueryResult &result)
{
    if (result.type == QueryResult::Type::ERROR)
    {
        statusMetaPanel_->showError(QString::fromStdString(result.message));
        statusBar()->showMessage(tr("Error"));
        return;
    }

    resultTablePanel_->renderResult(result);
    statusMetaPanel_->showResultMeta(result);
    statusBar()->showMessage(tr("OK"));
    adapter_->refreshCatalog();
}

void MainWindow::handleTableReady(const QString &database, const QString &table, const QueryResult &result)
{
    if (result.type == QueryResult::Type::ERROR)
        return;

    tableEditorPanel_->loadTable(database, table, result);
    adminPanel_->setCurrentTable(database, table);
}
