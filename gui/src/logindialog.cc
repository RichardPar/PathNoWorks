// logindialog.cc -- a user name and password for a node.

#include "logindialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace gui {

LoginDialog::LoginDialog (const QString &node, const QString &why,
                          const Login &start, QWidget *parent)
    : QDialog (parent)
{
    setWindowTitle ("Log in to " + node);
    auto *v = new QVBoxLayout (this);
    if (!why.isEmpty ()) {
        auto *l = new QLabel (why);
        l->setWordWrap (true);
        v->addWidget (l);
    }
    auto *form = new QFormLayout;
    user_ = new QLineEdit (start.user);
    password_ = new QLineEdit (start.password);
    password_->setEchoMode (QLineEdit::Password);
    account_ = new QLineEdit (start.account);
    proxy_ = new QCheckBox ("Without a user name, ask for proxy access as me");
    proxy_->setChecked (start.proxy);
    form->addRow ("User:", user_);
    form->addRow ("Password:", password_);
    form->addRow ("Account:", account_);
    v->addLayout (form);
    v->addWidget (proxy_);
    auto *note = new QLabel ("Leave the user empty for the node's default "
                             "DECnet account.");
    note->setEnabled (false);
    v->addWidget (note);
    auto *buttons = new QDialogButtonBox (QDialogButtonBox::Ok
                                          | QDialogButtonBox::Cancel);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget (buttons);
    user_->setFocus ();
}

Login LoginDialog::login () const
{
    return { user_->text ().trimmed (), password_->text (),
             account_->text ().trimmed (), proxy_->isChecked () };
}

}   // namespace gui
