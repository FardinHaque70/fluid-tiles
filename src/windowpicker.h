// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class QPlainTextEdit;

class WindowPicker : public QDialog {
    Q_OBJECT
public:
    enum Property { Application, Title, Role };
    WindowPicker(Property property,QPlainTextEdit *target,QWidget *parent);
private:
    Property m_property;
    QComboBox *m_values;
    QLabel *m_status;
    QPushButton *m_add;
    int m_request=0;
    void inspect(const QString &uuid);
};
