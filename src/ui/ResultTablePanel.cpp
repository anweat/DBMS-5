#include "ResultTablePanel.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

ResultTablePanel::ResultTablePanel(QWidget *parent)
    : QWidget(parent),
      table_(new QTableWidget(this))
{
    table_->setObjectName(QStringLiteral("resultTable"));
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->verticalHeader()->setVisible(false);

    auto *group = new QGroupBox(tr("Result"), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(table_);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(group);
}

void ResultTablePanel::renderResult(const QueryResult &result)
{
    table_->clear();
    table_->setRowCount(0);
    table_->setColumnCount(static_cast<int>(result.columns.size()));

    QStringList headers;
    for (const auto &column : result.columns)
        headers << QString::fromStdString(column.name);
    table_->setHorizontalHeaderLabels(headers);

    table_->setRowCount(static_cast<int>(result.rows.size()));
    for (int rowIdx = 0; rowIdx < static_cast<int>(result.rows.size()); ++rowIdx)
    {
        const Row &row = result.rows[static_cast<size_t>(rowIdx)];
        for (int colIdx = 0; colIdx < static_cast<int>(result.columns.size()); ++colIdx)
        {
            FieldValue value = colIdx < static_cast<int>(row.size())
                                   ? row[static_cast<size_t>(colIdx)]
                                   : FieldValue{std::monostate{}};
            table_->setItem(rowIdx, colIdx, new QTableWidgetItem(fieldValueToString(value)));
        }
    }
    table_->resizeColumnsToContents();
}

void ResultTablePanel::clear()
{
    table_->clear();
    table_->setRowCount(0);
    table_->setColumnCount(0);
}

QString ResultTablePanel::fieldValueToString(const FieldValue &value)
{
    if (std::holds_alternative<std::monostate>(value))
        return QStringLiteral("NULL");
    if (std::holds_alternative<int64_t>(value))
        return QString::number(std::get<int64_t>(value));
    if (std::holds_alternative<double>(value))
        return QString::number(std::get<double>(value), 'g', 12);
    if (std::holds_alternative<bool>(value))
        return std::get<bool>(value) ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    return QString::fromStdString(std::get<std::string>(value));
}
