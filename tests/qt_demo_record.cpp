#include "ui/MainWindow.h"
#include "ui/QtSessionAdapter.h"

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTest>
#include <QTextEdit>
#include <QTreeWidget>

namespace
{
template <typename T>
T *mustFind(QObject &root, const QString &name)
{
    auto *object = root.findChild<T *>(name);
    Q_ASSERT(object);
    return object;
}

QTreeWidgetItem *findTreeItem(QTreeWidgetItem *root, const QString &text)
{
    if (!root)
        return nullptr;
    if (root->text(0).contains(text))
        return root;
    for (int i = 0; i < root->childCount(); ++i)
        if (auto *found = findTreeItem(root->child(i), text))
            return found;
    return nullptr;
}

QTreeWidgetItem *findTreeItem(QTreeWidget *tree, const QString &text)
{
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (auto *found = findTreeItem(tree->topLevelItem(i), text))
            return found;
    return nullptr;
}

void pause(int ms = 750)
{
    QTest::qWait(ms);
}

void moveTo(QWidget *widget)
{
    if (!widget)
        return;
    QCursor::setPos(widget->mapToGlobal(widget->rect().center()));
    pause(180);
}

void clickButton(QPushButton *button, int waitMs = 700)
{
    moveTo(button);
    QTest::mouseClick(button, Qt::LeftButton);
    pause(waitMs);
}

void typeText(QLineEdit *edit, const QString &text, int waitMs = 400)
{
    moveTo(edit);
    edit->clear();
    QTest::keyClicks(edit, text, Qt::NoModifier, 18);
    pause(waitMs);
}

void typeText(QTextEdit *edit, const QString &text, int waitMs = 400)
{
    moveTo(edit);
    edit->clear();
    QTest::keyClicks(edit, text, Qt::NoModifier, 8);
    pause(waitMs);
}

void setCellText(QTableWidget *table, int row, int column, const QString &text)
{
    auto *item = table->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem;
        table->setItem(row, column, item);
    }
    table->scrollToItem(item);
    table->setCurrentItem(item);
    moveTo(table->viewport());
    item->setText(text);
    pause(350);
}

void setTab(QTabWidget *tabs, const QString &text, int waitMs = 600)
{
    if (!tabs)
        return;
    for (int i = 0; i < tabs->count(); ++i)
    {
        if (tabs->tabText(i).contains(text, Qt::CaseInsensitive))
        {
            tabs->setCurrentIndex(i);
            QApplication::processEvents();
            moveTo(tabs);
            pause(waitMs);
            return;
        }
    }
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const bool presentationMode = qEnvironmentVariableIsSet("DBMS_QT_PRESENTATION_MODE");
    if (presentationMode)
    {
        app.setStyleSheet(QStringLiteral(
            "QWidget { font-size: 14pt; }"
            "QGroupBox { font-size: 15pt; font-weight: 600; margin-top: 14px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
            "QLineEdit, QTextEdit, QTableWidget, QTreeWidget { font-size: 14pt; }"
            "QHeaderView::section { font-size: 13pt; font-weight: 600; padding: 6px; }"
            "QPushButton { font-size: 13pt; min-height: 34px; padding: 5px 10px; }"
            "QTabBar::tab { font-size: 13pt; min-width: 120px; padding: 8px 14px; }"
            "QLabel { font-size: 13pt; }"));
    }

    MainWindow window;
    window.setWindowTitle(QObject::tr("DBMS Qt Demo - Admin Workflow"));
    window.showMaximized();
    QTest::qWaitForWindowExposed(&window);
    pause(1000);

    const QString screenshotDir = qEnvironmentVariable("DBMS_QT_SCREENSHOT_DIR");
    if (!screenshotDir.isEmpty())
        QDir().mkpath(screenshotDir);
    auto saveShot = [&](const QString &name) {
        if (screenshotDir.isEmpty())
            return;
        QApplication::processEvents();
        pause(450);
        window.grab().save(QDir(screenshotDir).filePath(name));
    };

    auto *userEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionUserEdit"));
    auto *passwordEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionPasswordEdit"));
    auto *connectButton = mustFind<QPushButton>(window, QStringLiteral("connectButton"));
    auto *sqlEditor = mustFind<QTextEdit>(window, QStringLiteral("sqlEditor"));
    auto *executeButton = mustFind<QPushButton>(window, QStringLiteral("executeSqlButton"));
    auto *tree = mustFind<QTreeWidget>(window, QStringLiteral("databaseTree"));
    auto *adapter = window.findChild<QtSessionAdapter *>();
    auto *adminTabs = mustFind<QTabWidget>(window, QStringLiteral("adminTabs"));
    if (presentationMode)
    {
        if (auto *splitter = window.findChild<QSplitter *>(QStringLiteral("mainSplitter")))
            splitter->setSizes({340, 980, 620});
        tree->setColumnWidth(0, 310);
        sqlEditor->setMinimumHeight(460);
    }
    QTabWidget *workTabs = nullptr;
    QTabWidget *sideTabs = nullptr;
    for (auto *tabs : window.findChildren<QTabWidget *>())
    {
        for (int i = 0; i < tabs->count(); ++i)
        {
            if (tabs->tabText(i) == QStringLiteral("Table Data"))
                workTabs = tabs;
            if (tabs->tabText(i) == QStringLiteral("Admin"))
                sideTabs = tabs;
        }
    }

    auto execSql = [&](const QString &sql, int waitMs = 650) {
        setTab(workTabs, QStringLiteral("SQL"), 350);
        typeText(sqlEditor, sql, 250);
        clickButton(executeButton, waitMs);
    };

    auto execSqlAndShowResult = [&](const QString &sql, int waitMs = 900, int resultWaitMs = 1100) {
        execSql(sql, waitMs);
        setTab(workTabs, QStringLiteral("Query Result"), resultWaitMs);
    };

    auto execSqlAndShowStatus = [&](const QString &sql, int waitMs = 900, int statusWaitMs = 1100) {
        execSql(sql, waitMs);
        setTab(sideTabs, QStringLiteral("Status"), statusWaitMs);
    };

    auto connectAs = [&](const QString &user, const QString &password) {
        typeText(userEdit, user, 250);
        typeText(passwordEdit, password, 250);
        clickButton(connectButton, 850);
    };

    auto clickTree = [&](const QString &text) {
        if (auto *item = findTreeItem(tree, text))
        {
            item->setExpanded(true);
            tree->scrollToItem(item);
            moveTo(tree->viewport());
            tree->setCurrentItem(item);
            pause(800);
        }
    };

    connectAs(QStringLiteral("root"), QStringLiteral("root"));
    adapter->executeSql(QStringLiteral("DROP DATABASE IF EXISTS qt_demo_video"));
    adapter->executeSql(QStringLiteral("DROP USER 'demo_reader'"));
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(1000);
    saveShot(QStringLiteral("01_login_tree.png"));

    setTab(sideTabs, QStringLiteral("Admin"));
    setTab(adminTabs, QStringLiteral("Object"));

    auto *adminDb = mustFind<QLineEdit>(window, QStringLiteral("adminDatabaseEdit"));
    auto *adminTable = mustFind<QLineEdit>(window, QStringLiteral("adminTableEdit"));
    auto *columns = mustFind<QTableWidget>(window, QStringLiteral("adminTableColumnsTable"));
    auto *addColumn = mustFind<QPushButton>(window, QStringLiteral("adminAddColumnSpecButton"));
    auto *applyColumns = mustFind<QPushButton>(window, QStringLiteral("adminApplyColumnsButton"));
    auto *createDb = mustFind<QPushButton>(window, QStringLiteral("adminCreateDbButton"));
    auto *useDb = mustFind<QPushButton>(window, QStringLiteral("adminUseDbButton"));
    auto *createTable = mustFind<QPushButton>(window, QStringLiteral("adminCreateTableButton"));
    auto *deleteRows = mustFind<QPushButton>(window, QStringLiteral("adminDeleteRowsButton"));
    auto *deleteWhere = mustFind<QLineEdit>(window, QStringLiteral("adminDeleteWhereEdit"));

    typeText(adminDb, QStringLiteral("qt_demo_video"), 250);
    pause(800);
    clickButton(createDb, 1200);
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(800);
    clickTree(QStringLiteral("qt_demo_video"));
    clickButton(useDb, 900);

    setTab(adminTabs, QStringLiteral("Object"));
    typeText(adminTable, QStringLiteral("products"), 250);
    columns->setRowCount(0);
    for (int i = 0; i < 3; ++i)
        clickButton(addColumn, 300);
    setCellText(columns, 0, 0, QStringLiteral("id"));
    setCellText(columns, 0, 1, QStringLiteral("INT"));
    if (auto *primary = qobject_cast<QCheckBox *>(columns->cellWidget(0, 3)))
    {
        moveTo(primary);
        primary->setChecked(true);
        pause(350);
    }
    setCellText(columns, 1, 0, QStringLiteral("name"));
    setCellText(columns, 1, 1, QStringLiteral("VARCHAR(40)"));
    if (auto *notNull = qobject_cast<QCheckBox *>(columns->cellWidget(1, 2)))
    {
        moveTo(notNull);
        notNull->setChecked(true);
        pause(350);
    }
    setCellText(columns, 2, 0, QStringLiteral("stock"));
    setCellText(columns, 2, 1, QStringLiteral("INT"));
    pause(1200);
    clickButton(createTable, 1000);
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(900);
    clickTree(QStringLiteral("Tables"));
    clickTree(QStringLiteral("products"));
    saveShot(QStringLiteral("02_admin_create_db_table.png"));

    setTab(workTabs, QStringLiteral("Table Data"));
    auto *dataTable = mustFind<QTableWidget>(window, QStringLiteral("tableEditorTable"));
    auto *addRow = mustFind<QPushButton>(window, QStringLiteral("tableAddRowButton"));
    auto *deleteRow = mustFind<QPushButton>(window, QStringLiteral("tableDeleteRowButton"));
    auto *saveRows = mustFind<QPushButton>(window, QStringLiteral("tableSaveEditsButton"));
    for (const auto &rowValues : {
             QStringList{QStringLiteral("1"), QStringLiteral("keyboard"), QStringLiteral("12")},
             QStringList{QStringLiteral("2"), QStringLiteral("mouse"), QStringLiteral("34")},
             QStringList{QStringLiteral("3"), QStringLiteral("monitor"), QStringLiteral("8")}})
    {
        clickButton(addRow, 500);
        const int row = dataTable->rowCount() - 1;
        for (int col = 0; col < rowValues.size(); ++col)
            setCellText(dataTable, row, col, rowValues[col]);
    }
    pause(1000);
    clickButton(saveRows, 1400);
    QMetaObject::invokeMethod(adapter, "loadTable", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("qt_demo_video")),
                              Q_ARG(QString, QStringLiteral("products")));
    pause(1000);

    clickButton(addRow, 400);
    int badRow = dataTable->rowCount() - 1;
    setCellText(dataTable, badRow, 0, QStringLiteral("2"));
    setCellText(dataTable, badRow, 1, QStringLiteral("duplicate"));
    setCellText(dataTable, badRow, 2, QStringLiteral("5"));
    clickButton(saveRows, 1400);
    pause(1200);
    dataTable->removeRow(badRow);
    pause(600);

    if (dataTable->rowCount() > 0)
    {
        dataTable->selectRow(2);
        moveTo(dataTable->viewport());
        pause(600);
        clickButton(deleteRow, 900);
        QMetaObject::invokeMethod(adapter, "loadTable", Qt::DirectConnection,
                                  Q_ARG(QString, QStringLiteral("qt_demo_video")),
                                  Q_ARG(QString, QStringLiteral("products")));
        pause(900);
    }
    saveShot(QStringLiteral("03_table_data_crud.png"));

    execSqlAndShowResult(QStringLiteral("SELECT id, name, stock FROM products ORDER BY id"), 1400, 1500);
    saveShot(QStringLiteral("04_query_result_products.png"));

    setTab(sideTabs, QStringLiteral("Admin"));
    setTab(adminTabs, QStringLiteral("Object"));
    clickTree(QStringLiteral("products"));
    clickButton(addColumn, 400);
    const int noteRow = columns->rowCount() - 1;
    setCellText(columns, noteRow, 0, QStringLiteral("note"));
    setCellText(columns, noteRow, 1, QStringLiteral("VARCHAR(60)"));
    pause(900);
    clickButton(applyColumns, 1200);
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(700);
    clickTree(QStringLiteral("note"));

    auto *indexName = mustFind<QLineEdit>(window, QStringLiteral("adminIndexEdit"));
    auto *indexColumns = mustFind<QLineEdit>(window, QStringLiteral("adminIndexColumnsEdit"));
    auto *createIndex = mustFind<QPushButton>(window, QStringLiteral("adminCreateIndexButton"));
    setTab(adminTabs, QStringLiteral("Structure"));
    typeText(indexName, QStringLiteral("idx_products_name"), 250);
    typeText(indexColumns, QStringLiteral("name"), 250);
    pause(700);
    clickButton(createIndex, 1000);
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(700);
    clickTree(QStringLiteral("idx_products_name"));
    saveShot(QStringLiteral("05_structure_index.png"));

    setTab(workTabs, QStringLiteral("SQL"));
    execSql(QStringLiteral("CREATE TABLE users (uid INT PRIMARY KEY AUTO_INCREMENT, name VARCHAR(40) NOT NULL, region VARCHAR(20) DEFAULT 'unknown', age INT DEFAULT 0, email VARCHAR(80) UNIQUE)"), 1000);
    execSql(QStringLiteral("CREATE TABLE orders (oid INT PRIMARY KEY AUTO_INCREMENT, user_id INT, amount DOUBLE NOT NULL, status VARCHAR(20) DEFAULT 'new', CONSTRAINT fk_orders_user FOREIGN KEY (user_id) REFERENCES users(uid))"), 1000);
    execSql(QStringLiteral("INSERT INTO users (name, region, age, email) VALUES ('Alice', 'East', 30, 'alice@example.com')"), 500);
    execSql(QStringLiteral("INSERT INTO users (name, region, age, email) VALUES ('Bob', 'West', 25, 'bob@example.com')"), 500);
    execSql(QStringLiteral("INSERT INTO orders (user_id, amount, status) VALUES (1, 199.99, 'paid')"), 500);
    execSql(QStringLiteral("INSERT INTO orders (user_id, amount, status) VALUES (2, 320.50, 'paid')"), 500);
    execSqlAndShowResult(QStringLiteral("SELECT u.name, o.amount, o.status FROM users u INNER JOIN orders o ON u.uid = o.user_id ORDER BY o.amount DESC"), 1600, 1700);
    saveShot(QStringLiteral("06_join_result.png"));

    setTab(workTabs, QStringLiteral("SQL"));
    execSql(QStringLiteral("BEGIN"), 700);
    execSql(QStringLiteral("UPDATE users SET age = 99 WHERE name = 'Bob'"), 700);
    execSql(QStringLiteral("ROLLBACK"), 900);
    execSqlAndShowResult(QStringLiteral("SELECT name, age FROM users ORDER BY uid ASC"), 1400, 1500);
    saveShot(QStringLiteral("07_tx_result.png"));

    auto *adminUser = mustFind<QLineEdit>(window, QStringLiteral("adminUserEdit"));
    auto *adminPassword = mustFind<QLineEdit>(window, QStringLiteral("adminPasswordEdit"));
    auto *privDb = mustFind<QLineEdit>(window, QStringLiteral("adminPrivilegeDbEdit"));
    auto *privTable = mustFind<QLineEdit>(window, QStringLiteral("adminPrivilegeTableEdit"));
    auto *privSelect = mustFind<QCheckBox>(window, QStringLiteral("adminPrivilegeSelectCheck"));
    auto *privInsert = mustFind<QCheckBox>(window, QStringLiteral("adminPrivilegeInsertCheck"));
    auto *createUser = mustFind<QPushButton>(window, QStringLiteral("adminCreateUserButton"));
    auto *grant = mustFind<QPushButton>(window, QStringLiteral("adminGrantButton"));
    setTab(sideTabs, QStringLiteral("Admin"));
    setTab(adminTabs, QStringLiteral("Users"));
    typeText(adminUser, QStringLiteral("demo_reader"), 250);
    typeText(adminPassword, QStringLiteral("reader123"), 250);
    clickButton(createUser, 700);
    typeText(privDb, QStringLiteral("qt_demo_video"), 250);
    typeText(privTable, QStringLiteral("products"), 250);
    privSelect->setChecked(true);
    privInsert->setChecked(false);
    pause(700);
    clickButton(grant, 1000);
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(900);
    clickTree(QStringLiteral("Users / Privileges"));
    clickTree(QStringLiteral("demo_reader"));
    saveShot(QStringLiteral("08_privileges_tree.png"));

    connectAs(QStringLiteral("demo_reader"), QStringLiteral("reader123"));
    execSql(QStringLiteral("USE qt_demo_video"));
    execSqlAndShowResult(QStringLiteral("SELECT name, stock FROM products WHERE stock > 10"), 1200, 1400);
    saveShot(QStringLiteral("09_reader_select.png"));
    execSqlAndShowStatus(QStringLiteral("INSERT INTO products (id, name, stock) VALUES (9, 'blocked', 1)"), 1200, 1400);
    saveShot(QStringLiteral("10_permission_status.png"));

    connectAs(QStringLiteral("root"), QStringLiteral("root"));
    setTab(sideTabs, QStringLiteral("Admin"));
    setTab(adminTabs, QStringLiteral("Object"));
    typeText(deleteWhere, QStringLiteral("id = 1"), 250);
    clickButton(deleteRows, 900);
    clickTree(QStringLiteral("products"));
    setTab(workTabs, QStringLiteral("Table Data"));
    QMetaObject::invokeMethod(adapter, "loadTable", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("qt_demo_video")),
                              Q_ARG(QString, QStringLiteral("products")));

    pause(2500);
    return 0;
}
