#include "SqlEditorPanel.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

SqlEditorPanel::SqlEditorPanel(QWidget *parent)
    : QWidget(parent),
      editor_(new QTextEdit(this)),
      examples_(new QComboBox(this)),
      executeButton_(new QPushButton(tr("Execute"), this))
{
    editor_->setObjectName(QStringLiteral("sqlEditor"));
    examples_->setObjectName(QStringLiteral("sqlExamples"));
    executeButton_->setObjectName(QStringLiteral("executeSqlButton"));
    editor_->setAcceptRichText(false);
    editor_->setLineWrapMode(QTextEdit::NoWrap);
    editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    editor_->setPlaceholderText(tr("Write SQL here"));
    editor_->setMinimumHeight(120);

    executeButton_->setEnabled(false);
    connect(editor_, &QTextEdit::textChanged, this, [this]() {
        executeButton_->setEnabled(!editor_->toPlainText().trimmed().isEmpty());
    });
    connect(executeButton_, &QPushButton::clicked, this, [this]() {
        emit executeRequested(editor_->toPlainText().trimmed());
    });
    connect(examples_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        if (!text.isEmpty())
            editor_->setPlainText(text);
    });

    auto *toolbar = new QHBoxLayout;
    toolbar->addWidget(examples_, 1);
    toolbar->addWidget(executeButton_);

    auto *group = new QGroupBox(tr("SQL"), this);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->addLayout(toolbar);
    groupLayout->addWidget(editor_);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(group);
}

void SqlEditorPanel::setExamples(const QStringList &examples)
{
    examples_->clear();
    examples_->addItem(QString());
    examples_->addItems(examples);
}
