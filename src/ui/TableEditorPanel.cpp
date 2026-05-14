#include "TableEditorPanel.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

TableEditorPanel::TableEditorPanel(QWidget *parent)
    : QWidget(parent),
      titleLabel_(new QLabel(tr("No table loaded"), this)),
      table_(new QTableWidget(this))
{
    table_->setObjectName(QStringLiteral("tableEditorTable"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (!loading_)
            changedCells_.insert(QString::number(row) + QStringLiteral(":") + QString::number(column));
    });

    auto *addButton = new QPushButton(tr("Add Row"), this);
    auto *deleteButton = new QPushButton(tr("Delete Row"), this);
    auto *saveButton = new QPushButton(tr("Save Edits"), this);
    addButton->setObjectName(QStringLiteral("tableAddRowButton"));
    deleteButton->setObjectName(QStringLiteral("tableDeleteRowButton"));
    saveButton->setObjectName(QStringLiteral("tableSaveEditsButton"));
    connect(addButton, &QPushButton::clicked, this, &TableEditorPanel::addRow);
    connect(deleteButton, &QPushButton::clicked, this, &TableEditorPanel::deleteSelectedRows);
    connect(saveButton, &QPushButton::clicked, this, &TableEditorPanel::saveChanges);

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(titleLabel_, 1);
    toolbar->addWidget(addButton);
    toolbar->addWidget(deleteButton);
    toolbar->addWidget(saveButton);

    auto *group = new QGroupBox(tr("Table Data"), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addLayout(toolbar);
    groupLayout->addWidget(table_, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(group);
}

void TableEditorPanel::loadTable(const QString &database, const QString &table, const QueryResult &result)
{
    loading_ = true;
    database_ = database;
    tableName_ = table;
    columnNames_.clear();
    changedCells_.clear();

    titleLabel_->setText(database + QStringLiteral(".") + table);
    table_->clear();
    table_->setColumnCount(static_cast<int>(result.columns.size()));
    table_->setRowCount(static_cast<int>(result.rows.size()));

    QStringList headers;
    for (const auto &column : result.columns)
    {
        QString name = normalizeColumnName(QString::fromStdString(column.name));
        columnNames_ << name;
        headers << name;
    }
    table_->setHorizontalHeaderLabels(headers);

    for (int row = 0; row < static_cast<int>(result.rows.size()); ++row)
    {
        const Row &values = result.rows[static_cast<size_t>(row)];
        for (int col = 0; col < static_cast<int>(result.columns.size()); ++col)
        {
            QString text;
            if (col < static_cast<int>(values.size()))
            {
                const FieldValue &value = values[static_cast<size_t>(col)];
                if (std::holds_alternative<std::monostate>(value))
                    text = QStringLiteral("NULL");
                else if (std::holds_alternative<int64_t>(value))
                    text = QString::number(std::get<int64_t>(value));
                else if (std::holds_alternative<double>(value))
                    text = QString::number(std::get<double>(value), 'g', 12);
                else if (std::holds_alternative<bool>(value))
                    text = std::get<bool>(value) ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
                else
                    text = QString::fromStdString(std::get<std::string>(value));
            }
            table_->setItem(row, col, new QTableWidgetItem(text));
        }
    }
    originalRowCount_ = table_->rowCount();
    table_->resizeColumnsToContents();
    loading_ = false;
}

void TableEditorPanel::addRow()
{
    if (tableName_.isEmpty())
        return;
    table_->insertRow(table_->rowCount());
}

void TableEditorPanel::deleteSelectedRows()
{
    if (tableName_.isEmpty() || columnNames_.isEmpty())
        return;

    QList<int> rows;
    for (const auto &range : table_->selectedRanges())
        for (int row = range.topRow(); row <= range.bottomRow(); ++row)
            if (!rows.contains(row))
                rows.push_back(row);
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int row : rows)
    {
        if (row < originalRowCount_)
            emit sqlRequested(QStringLiteral("DELETE FROM %1 WHERE %2").arg(qualifiedTableName(), keyWhereClause(row)));
        table_->removeRow(row);
    }
}

void TableEditorPanel::saveChanges()
{
    if (tableName_.isEmpty() || columnNames_.isEmpty())
        return;

    for (int row = originalRowCount_; row < table_->rowCount(); ++row)
    {
        QStringList cols;
        QStringList values;
        for (int col = 0; col < columnNames_.size(); ++col)
        {
            const QString value = itemText(row, col).trimmed();
            if (value.isEmpty())
                continue;
            cols << columnNames_[col];
            values << literal(value);
        }
        if (!cols.isEmpty())
            emit sqlRequested(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)")
                                  .arg(qualifiedTableName(), cols.join(QStringLiteral(", ")), values.join(QStringLiteral(", "))));
    }

    for (const QString &cell : changedCells_)
    {
        const auto parts = cell.split(QStringLiteral(":"));
        if (parts.size() != 2)
            continue;
        int row = parts[0].toInt();
        int col = parts[1].toInt();
        if (row >= originalRowCount_ || col >= columnNames_.size())
            continue;
        emit sqlRequested(QStringLiteral("UPDATE %1 SET %2 = %3 WHERE %4")
                              .arg(qualifiedTableName(), columnNames_[col], literal(itemText(row, col)), keyWhereClause(row)));
    }
}

QString TableEditorPanel::itemText(int row, int column) const
{
    auto *item = table_->item(row, column);
    return item ? item->text() : QString();
}

QString TableEditorPanel::normalizeColumnName(const QString &name) const
{
    const int dot = name.lastIndexOf('.');
    return dot >= 0 ? name.mid(dot + 1) : name;
}

QString TableEditorPanel::literal(const QString &value) const
{
    if (value.compare(QStringLiteral("NULL"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("NULL");
    bool ok = false;
    value.toDouble(&ok);
    if (ok)
        return value;
    QString escaped = value;
    escaped.replace("'", "''");
    return QStringLiteral("'") + escaped + QStringLiteral("'");
}

QString TableEditorPanel::keyWhereClause(int row) const
{
    return columnNames_.first() + QStringLiteral(" = ") + literal(itemText(row, 0));
}

QString TableEditorPanel::qualifiedTableName() const
{
    return database_.isEmpty() ? tableName_ : database_ + QStringLiteral(".") + tableName_;
}
