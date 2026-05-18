#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTableWidget;

class AdminPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AdminPanel(QWidget *parent = nullptr);

public slots:
    void setCurrentDatabase(const QString &database);
    void setCurrentTable(const QString &database, const QString &table);
    void setCurrentTable(const QString &database, const QString &table, const QStringList &columns);
    void setCurrentStructure(const QString &database, const QString &table, const QStringList &columns);

signals:
    void sqlRequested(const QString &sql);
    void refreshRequested();

private:
    QString targetName() const;
    QString quoteString(const QString &value) const;
    QString createTableColumnsSql() const;
    QString columnDefinitionFromRow(int row) const;
    QString columnSpecFromRow(int row) const;
    QString columnNameFromSpec(const QString &spec) const;
    QString privilegesSql() const;
    void addColumnSpecRow(const QString &name = QString(), const QString &type = QString(),
                          bool notNull = false, bool primaryKey = false);
    void clearColumnSpecRows();
    void applyColumnSpecChanges();
    void emitIfNotEmpty(const QString &sql);

    QLabel *contextLabel_;
    QTabWidget *tabs_;
    QLineEdit *databaseEdit_;
    QLineEdit *tableEdit_;
    QTableWidget *tableColumnsTable_;
    QLineEdit *deleteWhereEdit_;
    QLineEdit *indexEdit_;
    QLineEdit *indexColumnsEdit_;
    QStringList originalColumnSpecs_;

    QLineEdit *userEdit_;
    QLineEdit *passwordEdit_;
    QLineEdit *privilegeTargetDbEdit_;
    QLineEdit *privilegeTargetTableEdit_;
    QComboBox *privilegeCombo_;
    QCheckBox *privilegeAllCheck_;
    QCheckBox *privilegeSelectCheck_;
    QCheckBox *privilegeInsertCheck_;
    QCheckBox *privilegeUpdateCheck_;
    QCheckBox *privilegeDeleteCheck_;
};
