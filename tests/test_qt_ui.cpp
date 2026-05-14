#include "ui/MainWindow.h"
#include "ui/QtSessionAdapter.h"

#include <QApplication>
#include <QComboBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTest>
#include <QTreeWidget>

class QtUiSmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void sqlInputExecutesThroughMainWindow();
    void adminButtonsAndTableEditorRespectPermissions();
};

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
}

void QtUiSmokeTest::sqlInputExecutesThroughMainWindow()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *userEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionUserEdit"));
    auto *passwordEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionPasswordEdit"));
    auto *connectButton = mustFind<QPushButton>(window, QStringLiteral("connectButton"));
    auto *sqlEditor = mustFind<QTextEdit>(window, QStringLiteral("sqlEditor"));
    auto *executeButton = mustFind<QPushButton>(window, QStringLiteral("executeSqlButton"));
    auto *resultTable = mustFind<QTableWidget>(window, QStringLiteral("resultTable"));
    auto *statusLog = mustFind<QTextEdit>(window, QStringLiteral("statusLogView"));

    userEdit->clear();
    QTest::keyClicks(userEdit, "root");
    passwordEdit->clear();
    QTest::keyClicks(passwordEdit, "root");
    QTest::mouseClick(connectButton, Qt::LeftButton);

    auto execSql = [&](const QString &sql) {
        sqlEditor->setPlainText(sql);
        QVERIFY(executeButton->isEnabled());
        QTest::mouseClick(executeButton, Qt::LeftButton);
        QCoreApplication::processEvents();
    };

    execSql(QStringLiteral("DROP DATABASE IF EXISTS qt_pipe_smoke"));
    execSql(QStringLiteral("CREATE DATABASE qt_pipe_smoke"));
    execSql(QStringLiteral("USE qt_pipe_smoke"));
    execSql(QStringLiteral("CREATE TABLE items (id INT PRIMARY KEY, name VARCHAR(20))"));
    execSql(QStringLiteral("INSERT INTO items (id, name) VALUES (1, 'alpha')"));
    execSql(QStringLiteral("DROP USER 'qt_reader'"));
    execSql(QStringLiteral("SELECT name FROM items WHERE id = 1"));

    QCOMPARE(resultTable->rowCount(), 1);
    QCOMPARE(resultTable->columnCount(), 1);
    QVERIFY(resultTable->item(0, 0));
    QCOMPARE(resultTable->item(0, 0)->text(), QStringLiteral("alpha"));

    execSql(QStringLiteral("DROP DATABASE qt_pipe_smoke"));
}

void QtUiSmokeTest::adminButtonsAndTableEditorRespectPermissions()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *adapter = window.findChild<QtSessionAdapter *>();
    auto *userEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionUserEdit"));
    auto *passwordEdit = mustFind<QLineEdit>(window, QStringLiteral("sessionPasswordEdit"));
    auto *connectButton = mustFind<QPushButton>(window, QStringLiteral("connectButton"));
    auto *sqlEditor = mustFind<QTextEdit>(window, QStringLiteral("sqlEditor"));
    auto *executeButton = mustFind<QPushButton>(window, QStringLiteral("executeSqlButton"));
    auto *resultTable = mustFind<QTableWidget>(window, QStringLiteral("resultTable"));
    auto *statusLog = mustFind<QTextEdit>(window, QStringLiteral("statusLogView"));
    auto *tree = mustFind<QTreeWidget>(window, QStringLiteral("databaseTree"));
    auto *mainSplitter = mustFind<QSplitter>(window, QStringLiteral("mainSplitter"));
    QVERIFY(adapter);

    QCOMPARE(tree->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QCOMPARE(resultTable->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QVERIFY(!mainSplitter->childrenCollapsible());
    QCOMPARE(mainSplitter->sizes().size(), 3);
    QVERIFY(mainSplitter->sizes().at(1) > mainSplitter->sizes().at(0));

    auto execSql = [&](const QString &sql) {
        sqlEditor->setPlainText(sql);
        QVERIFY(executeButton->isEnabled());
        QTest::mouseClick(executeButton, Qt::LeftButton);
        QCoreApplication::processEvents();
    };
    auto connectAs = [&](const QString &user, const QString &password) {
        userEdit->clear();
        QTest::keyClicks(userEdit, user);
        passwordEdit->clear();
        QTest::keyClicks(passwordEdit, password);
        QTest::mouseClick(connectButton, Qt::LeftButton);
        QCoreApplication::processEvents();
    };

    connectAs(QStringLiteral("root"), QStringLiteral("root"));
    execSql(QStringLiteral("DROP DATABASE IF EXISTS qt_admin_pipe"));
    execSql(QStringLiteral("CREATE DATABASE qt_admin_pipe"));
    execSql(QStringLiteral("USE qt_admin_pipe"));
    execSql(QStringLiteral("CREATE TABLE items (id INT PRIMARY KEY, name VARCHAR(20))"));
    execSql(QStringLiteral("INSERT INTO items (id, name) VALUES (1, 'alpha')"));
    execSql(QStringLiteral("CREATE INDEX idx_items_name ON items (name)"));

    auto *adminDb = mustFind<QLineEdit>(window, QStringLiteral("adminDatabaseEdit"));
    auto *adminTable = mustFind<QLineEdit>(window, QStringLiteral("adminTableEdit"));
    auto *adminColumn = mustFind<QLineEdit>(window, QStringLiteral("adminColumnEdit"));
    auto *adminType = mustFind<QLineEdit>(window, QStringLiteral("adminColumnTypeEdit"));
    auto *adminDefault = mustFind<QLineEdit>(window, QStringLiteral("adminDefaultEdit"));
    auto *addColumnButton = mustFind<QPushButton>(window, QStringLiteral("adminAddColumnButton"));

    adminDb->setText(QStringLiteral("qt_admin_pipe"));
    adminTable->setText(QStringLiteral("items"));
    adminColumn->setText(QStringLiteral("note"));
    adminType->setText(QStringLiteral("VARCHAR(20)"));
    adminDefault->setText(QStringLiteral("fresh"));
    QTest::mouseClick(addColumnButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    QVERIFY(QMetaObject::invokeMethod(adapter, "refreshCatalog", Qt::DirectConnection));
    QVERIFY(findTreeItem(tree, QStringLiteral("qt_admin_pipe")));
    QVERIFY(findTreeItem(tree, QStringLiteral("items")));
    QVERIFY(findTreeItem(tree, QStringLiteral("note")));
    QVERIFY(findTreeItem(tree, QStringLiteral("idx_items_name")));
    QVERIFY(findTreeItem(tree, QStringLiteral("Users / Privileges")));
    QVERIFY(findTreeItem(tree, QStringLiteral("Indexes")));
    QVERIFY(findTreeItem(tree, QStringLiteral("Columns")));
    QVERIFY(findTreeItem(tree, QStringLiteral("items"))->isExpanded());
    QVERIFY(tree->header()->sectionSize(0) > 0);

    execSql(QStringLiteral("SELECT note FROM items WHERE id = 1"));
    QCOMPARE(resultTable->rowCount(), 1);
    QVERIFY(resultTable->item(0, 0));
    QCOMPARE(resultTable->item(0, 0)->text(), QStringLiteral("fresh"));

    auto *adminUser = mustFind<QLineEdit>(window, QStringLiteral("adminUserEdit"));
    auto *adminPassword = mustFind<QLineEdit>(window, QStringLiteral("adminPasswordEdit"));
    auto *privDb = mustFind<QLineEdit>(window, QStringLiteral("adminPrivilegeDbEdit"));
    auto *privTable = mustFind<QLineEdit>(window, QStringLiteral("adminPrivilegeTableEdit"));
    auto *privCombo = mustFind<QComboBox>(window, QStringLiteral("adminPrivilegeCombo"));
    auto *createUserButton = mustFind<QPushButton>(window, QStringLiteral("adminCreateUserButton"));
    auto *grantButton = mustFind<QPushButton>(window, QStringLiteral("adminGrantButton"));

    adminUser->setText(QStringLiteral("qt_reader"));
    adminPassword->setText(QStringLiteral("reader123"));
    QTest::mouseClick(createUserButton, Qt::LeftButton);
    privDb->setText(QStringLiteral("qt_admin_pipe"));
    privTable->setText(QStringLiteral("items"));
    privCombo->setCurrentText(QStringLiteral("SELECT"));
    QTest::mouseClick(grantButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    connectAs(QStringLiteral("qt_reader"), QStringLiteral("reader123"));
    execSql(QStringLiteral("USE qt_admin_pipe"));
    execSql(QStringLiteral("SELECT name FROM items WHERE id = 1"));
    QCOMPARE(resultTable->rowCount(), 1);
    QVERIFY(resultTable->item(0, 0));
    QCOMPARE(resultTable->item(0, 0)->text(), QStringLiteral("alpha"));
    execSql(QStringLiteral("INSERT INTO items (id, name) VALUES (99, 'blocked')"));
    execSql(QStringLiteral("SELECT name FROM items WHERE id = 99"));
    QCOMPARE(resultTable->rowCount(), 0);

    connectAs(QStringLiteral("root"), QStringLiteral("root"));
    QVERIFY(QMetaObject::invokeMethod(adapter, "loadTable", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("qt_admin_pipe")),
                                      Q_ARG(QString, QStringLiteral("items"))));
    auto *editorTable = mustFind<QTableWidget>(window, QStringLiteral("tableEditorTable"));
    auto *addRowButton = mustFind<QPushButton>(window, QStringLiteral("tableAddRowButton"));
    auto *saveEditsButton = mustFind<QPushButton>(window, QStringLiteral("tableSaveEditsButton"));

    QTest::mouseClick(addRowButton, Qt::LeftButton);
    const int newRow = editorTable->rowCount() - 1;
    QVERIFY(newRow >= 0);
    editorTable->setItem(newRow, 0, new QTableWidgetItem(QStringLiteral("2")));
    editorTable->setItem(newRow, 1, new QTableWidgetItem(QStringLiteral("beta")));
    editorTable->setItem(newRow, 2, new QTableWidgetItem(QStringLiteral("typed")));
    QTest::mouseClick(saveEditsButton, Qt::LeftButton);
    QCoreApplication::processEvents();

    execSql(QStringLiteral("SELECT name, note FROM items WHERE id = 2"));
    QVERIFY2(resultTable->rowCount() == 1, statusLog->toPlainText().toUtf8().constData());
    QVERIFY(resultTable->item(0, 0));
    QVERIFY(resultTable->item(0, 1));
    QCOMPARE(resultTable->item(0, 0)->text(), QStringLiteral("beta"));
    QCOMPARE(resultTable->item(0, 1)->text(), QStringLiteral("typed"));

    execSql(QStringLiteral("DROP DATABASE qt_admin_pipe"));
}

QTEST_MAIN(QtUiSmokeTest)
#include "test_qt_ui.moc"
