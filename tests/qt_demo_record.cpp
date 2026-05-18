#include "ui/MainWindow.h"
#include "ui/QtSessionAdapter.h"

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
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
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    MainWindow window;
    window.setWindowTitle(QObject::tr("DBMS Qt Demo - Admin Workflow"));
    window.showMaximized();
    QTest::qWaitForWindowExposed(&window);
    pause(1000);

    auto *userEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionUserEdit"));
    auto *passwordEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionPasswordEdit"));
    auto *connectButton = mustFind<QPushButton>(window, QStringLiteral("connectButton"));
    auto *sqlEditor = mustFind<QTextEdit>(window, QStringLiteral("sqlEditor"));
    auto *executeButton = mustFind<QPushButton>(window, QStringLiteral("executeSqlButton"));
    auto *tree = mustFind<QTreeWidget>(window, QStringLiteral("databaseTree"));
    auto *adapter = window.findChild<QtSessionAdapter *>();

    auto execSql = [&](const QString &sql, int waitMs = 650) {
        typeText(sqlEditor, sql, 250);
        clickButton(executeButton, waitMs);
    };

    auto connectAs = [&](const QString &user, const QString &password) {
        typeText(userEdit, user, 250);
        typeText(passwordEdit, password, 250);
        clickButton(connectButton, 850);
    };

    auto clickTree = [&](const QString &text) {
        if (auto *item = findTreeItem(tree, text))
        {
            tree->scrollToItem(item);
            moveTo(tree->viewport());
            tree->setCurrentItem(item);
            pause(800);
        }
    };

    connectAs(QStringLiteral("root"), QStringLiteral("root"));
    execSql(QStringLiteral("DROP DATABASE IF EXISTS qt_demo_video"));
    execSql(QStringLiteral("CREATE DATABASE qt_demo_video"));
    execSql(QStringLiteral("USE qt_demo_video"));

    auto *adminDb = mustFind<QLineEdit>(window, QStringLiteral("adminDatabaseEdit"));
    auto *adminTable = mustFind<QLineEdit>(window, QStringLiteral("adminTableEdit"));
    auto *columns = mustFind<QTableWidget>(window, QStringLiteral("adminTableColumnsTable"));
    auto *addColumn = mustFind<QPushButton>(window, QStringLiteral("adminAddColumnSpecButton"));
    auto *applyColumns = mustFind<QPushButton>(window, QStringLiteral("adminApplyColumnsButton"));
    auto *createTable = mustFind<QPushButton>(window, QStringLiteral("adminCreateTableButton"));

    typeText(adminDb, QStringLiteral("qt_demo_video"), 250);
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

    execSql(QStringLiteral("INSERT INTO products (id, name, stock) VALUES (1, 'keyboard', 12)"));
    execSql(QStringLiteral("INSERT INTO products (id, name, stock) VALUES (2, 'mouse', 34)"));
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(700);
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
    typeText(indexName, QStringLiteral("idx_products_name"), 250);
    typeText(indexColumns, QStringLiteral("name"), 250);
    pause(700);
    clickButton(createIndex, 1000);
    QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection);
    pause(700);
    clickTree(QStringLiteral("idx_products_name"));

    auto *dataTable = mustFind<QTableWidget>(window, QStringLiteral("tableEditorTable"));
    auto *addRow = mustFind<QPushButton>(window, QStringLiteral("tableAddRowButton"));
    auto *saveRows = mustFind<QPushButton>(window, QStringLiteral("tableSaveEditsButton"));
    clickTree(QStringLiteral("products"));
    clickButton(addRow, 500);
    const int row = dataTable->rowCount() - 1;
    setCellText(dataTable, row, 0, QStringLiteral("3"));
    setCellText(dataTable, row, 1, QStringLiteral("monitor"));
    setCellText(dataTable, row, 2, QStringLiteral("8"));
    pause(1000);
    clickButton(saveRows, 1200);
    execSql(QStringLiteral("SELECT id, name, stock FROM products ORDER BY id"), 1200);

    auto *adminUser = mustFind<QLineEdit>(window, QStringLiteral("adminUserEdit"));
    auto *adminPassword = mustFind<QLineEdit>(window, QStringLiteral("adminPasswordEdit"));
    auto *privDb = mustFind<QLineEdit>(window, QStringLiteral("adminPrivilegeDbEdit"));
    auto *privTable = mustFind<QLineEdit>(window, QStringLiteral("adminPrivilegeTableEdit"));
    auto *privSelect = mustFind<QCheckBox>(window, QStringLiteral("adminPrivilegeSelectCheck"));
    auto *privInsert = mustFind<QCheckBox>(window, QStringLiteral("adminPrivilegeInsertCheck"));
    auto *createUser = mustFind<QPushButton>(window, QStringLiteral("adminCreateUserButton"));
    auto *grant = mustFind<QPushButton>(window, QStringLiteral("adminGrantButton"));
    typeText(adminUser, QStringLiteral("demo_reader"), 250);
    typeText(adminPassword, QStringLiteral("reader123"), 250);
    clickButton(createUser, 700);
    typeText(privDb, QStringLiteral("qt_demo_video"), 250);
    typeText(privTable, QStringLiteral("products"), 250);
    privSelect->setChecked(true);
    privInsert->setChecked(false);
    pause(700);
    clickButton(grant, 1000);

    connectAs(QStringLiteral("demo_reader"), QStringLiteral("reader123"));
    execSql(QStringLiteral("USE qt_demo_video"));
    execSql(QStringLiteral("SELECT name, stock FROM products WHERE stock > 10"), 1200);
    execSql(QStringLiteral("INSERT INTO products (id, name, stock) VALUES (9, 'blocked', 1)"), 1200);

    pause(2500);
    return 0;
}
