#ifndef ITEMWIDGET_H
#define ITEMWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class ItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ItemWidget(int icon, QWidget *parent = 0);
    ~ItemWidget();

private:
    QLabel *lblIcon;
    QVBoxLayout *layout;

    QString iconName(int pos);
};

#endif // ITEMWIDGET_H
