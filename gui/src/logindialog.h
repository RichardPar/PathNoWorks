// logindialog.h -- a user name and password for a node.

#ifndef PNW_GUI_LOGINDIALOG_H
#define PNW_GUI_LOGINDIALOG_H

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace gui {

struct Login {
    QString user, password, account;
    bool    proxy = false;          // no user: ask for proxy access
};

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    LoginDialog (const QString &node, const QString &why, const Login &start,
                 QWidget *parent = nullptr);
    Login login () const;

private:
    QLineEdit *user_, *password_, *account_;
    QCheckBox *proxy_;
};

}   // namespace gui

#endif
