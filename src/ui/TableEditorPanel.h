#pragma once

#include "../types.h"
#include <QPair>
#include <QSet>
#include <QVector>
#include <QWidget>

class QLabel;
class QTableWidget;

class TableEditorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TableEditorPanel(QWidget *parent = nullptr);

public slots:
    void loadTable(const QString &database, const QString &table, const QueryResult &result);
    void handleExecutionResult(const QueryResult &result);

signals:
    void sqlRequested(const QString &sql);
    void reloadRequested(const QString &database, const QString &table);

private:
    void addRow();
    void deleteSelectedRows();
    void saveChanges();
    QString normalizeColumnName(const QString &name) const;
    QString itemText(int row, int column) const;
    QString originalItemText(int row, int column) const;
    QString literal(const QString &value) const;
    QString keyWhereClause(int row) const;
    QString qualifiedTableName() const;
    void enqueuePendingEdit(const QVector<QPair<int, int>> &cells);
    void markCells(const QVector<QPair<int, int>> &cells, const QString &message);
    void clearCellMark(int row, int column);

    struct PendingEdit
    {
        QVector<QPair<int, int>> cells;
    };

    QLabel *titleLabel_;
    QTableWidget *table_;
    QString database_;
    QString tableName_;
    QStringList columnNames_;
    QVector<QStringList> originalRows_;
    int originalRowCount_ = 0;
    bool loading_ = false;
    bool saveHadErrors_ = false;
    QSet<QString> changedCells_;
    QVector<PendingEdit> pendingEdits_;
};
