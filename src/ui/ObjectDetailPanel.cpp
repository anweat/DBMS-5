#include "ObjectDetailPanel.h"

#include <QGroupBox>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>

ObjectDetailPanel::ObjectDetailPanel(QWidget *parent)
    : QWidget(parent),
      titleLabel_(new QLabel(tr("No object selected"), this)),
      detailView_(new QTextEdit(this))
{
    titleLabel_->setObjectName(QStringLiteral("objectDetailTitle"));
    titleLabel_->setWordWrap(true);
    detailView_->setObjectName(QStringLiteral("objectDetailView"));
    detailView_->setReadOnly(true);
    detailView_->setLineWrapMode(QTextEdit::NoWrap);
    detailView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *group = new QGroupBox(tr("Object Info"), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(titleLabel_);
    groupLayout->addWidget(detailView_, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(group);
}

void ObjectDetailPanel::showObject(const QString &title, const QStringList &lines)
{
    titleLabel_->setText(title);
    detailView_->setPlainText(lines.join(QStringLiteral("\n")));
}
