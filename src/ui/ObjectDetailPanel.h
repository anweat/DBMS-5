#pragma once

#include <QWidget>

class QLabel;
class QTextEdit;

class ObjectDetailPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectDetailPanel(QWidget *parent = nullptr);

public slots:
    void showObject(const QString &title, const QStringList &lines);

private:
    QLabel *titleLabel_;
    QTextEdit *detailView_;
};
