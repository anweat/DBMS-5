#include "StatusMetaPanel.h"

#include <QGroupBox>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

StatusMetaPanel::StatusMetaPanel(QWidget *parent)
    : QWidget(parent),
      summaryLabel_(new QLabel(this)),
      logView_(new QTextEdit(this))
{
    logView_->setObjectName(QStringLiteral("statusLogView"));
    logView_->setReadOnly(true);
    logView_->setLineWrapMode(QTextEdit::NoWrap);
    logView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    summaryLabel_->setWordWrap(true);

    auto *group = new QGroupBox(tr("Status"), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(summaryLabel_);
    groupLayout->addWidget(logView_, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(group);
}

void StatusMetaPanel::showResultMeta(const QueryResult &result)
{
    if (result.type == QueryResult::Type::ERROR)
    {
        showError(QString::fromStdString(result.message));
        return;
    }

    QString summary;
    if (result.type == QueryResult::Type::SELECT)
    {
        summary = tr("%1 row(s), %2 ms").arg(result.rowCount).arg(result.elapsedMs);
    }
    else if (result.type == QueryResult::Type::DML)
    {
        summary = tr("%1 row(s) affected, %2 ms").arg(result.affectedRows).arg(result.elapsedMs);
    }
    else
    {
        summary = tr("OK, %1 ms").arg(result.elapsedMs);
    }

    summaryLabel_->setStyleSheet(QString());
    summaryLabel_->setText(summary);
    const QString detail = QString::fromStdString(result.message);
    logView_->append(detail.isEmpty() ? summary : detail);
}

void StatusMetaPanel::showError(const QString &message)
{
    summaryLabel_->setText(tr("Error"));
    summaryLabel_->setStyleSheet(QStringLiteral("color: #b00020; font-weight: 600;"));
    logView_->append(QStringLiteral("[ERROR] ") + message);
}
