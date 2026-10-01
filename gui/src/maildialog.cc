// maildialog.cc -- write and send DECnet mail.

#include "maildialog.h"

#include "common.h"

#include "pnw/mail11.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <map>

#include <pwd.h>
#include <unistd.h>

namespace gui {

namespace {

std::string login_name ()
{
    std::string n = "USER";
    if (const passwd *pw = ::getpwuid (::geteuid ())) n = pw->pw_name;
    for (char &c : n) c = static_cast<char> (std::toupper (static_cast<unsigned char> (c)));
    return n;
}

// The recipients by node, each node with the login to give it, if any:
// NODE::USER or NODE"user password"::USER, separated by commas.
struct Batch {
    pnw::MailLogin           login;
    std::vector<std::string> users;
};

std::map<std::string, Batch> parse_to (const QString &to, QString *error)
{
    std::map<std::string, Batch> out;
    for (QString r : to.split (',', Qt::SkipEmptyParts)) {
        r = r.trimmed ();
        int sep = r.indexOf ("::");
        if (sep <= 0 || sep + 2 >= r.size ()) {
            *error = r + " is not NODE::USER";
            return {};
        }
        QString node = r.left (sep);
        pnw::MailLogin login;
        if (int q = node.indexOf ('"'); q >= 0) {
            QStringList words = node.mid (q + 1, node.lastIndexOf ('"') - q - 1)
                                    .split (' ', Qt::SkipEmptyParts);
            if (words.size () > 0) login.user = ss (words[0]);
            if (words.size () > 1) login.password = ss (words[1]);
            node = node.left (q);
        }
        Batch &b = out[ss (node.toUpper ())];
        if (!login.user.empty ()) b.login = login;
        b.users.push_back (ss (r.mid (sep + 2).toUpper ()));
    }
    if (out.empty ()) *error = "Nobody to send to";
    return out;
}

}   // namespace

MailDialog::MailDialog (const QString &to, QWidget *parent) : QDialog (parent)
{
    setWindowTitle ("DECnet Mail");
    resize (640, 480);
    auto *v = new QVBoxLayout (this);
    auto *form = new QFormLayout;
    to_ = new QLineEdit (to);
    to_->setPlaceholderText ("NODE::USER, NODE::USER ... or NODE\"user password\"::USER");
    subject_ = new QLineEdit;
    form->addRow ("To:", to_);
    form->addRow ("Subject:", subject_);
    v->addLayout (form);
    body_ = new QPlainTextEdit;
    body_->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
    v->addWidget (body_, 1);
    status_ = new QLabel (QString ("From %1").arg (qs (login_name ())));
    status_->setWordWrap (true);
    v->addWidget (status_);
    auto *buttons = new QDialogButtonBox (QDialogButtonBox::Cancel);
    send_ = buttons->addButton ("Send", QDialogButtonBox::AcceptRole);
    connect (send_, &QPushButton::clicked, this, &MailDialog::send);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget (buttons);
    (to.isEmpty () ? to_ : subject_)->setFocus ();
}

void MailDialog::send ()
{
    QString error;
    auto batches = parse_to (to_->text (), &error);
    if (batches.empty ()) {
        status_->setText (error);
        return;
    }
    std::vector<std::string> body;
    for (const QString &line : body_->toPlainText ().split ('\n'))
        body.push_back (ss (line));
    std::string subject = ss (subject_->text ());
    std::string socket = ss (api_socket ());
    std::string from = login_name ();

    send_->setEnabled (false);
    status_->setText ("Sending...");
    in_background (this, [=] () -> std::vector<std::string> {
        std::vector<std::string> report;
        for (const auto &[node, b] : batches) {
            std::string line;
            for (const std::string &u : b.users) line += (line.empty () ? "" : ",") + node + "::" + u;
            try {
                pnw::Api api (socket);
                for (const pnw::MailResult &r : pnw::mail11_send (api, node, b.login, from, b.users,
                                                                  line, subject, body))
                    report.push_back ((r.ok ? "Sent to " : "NOT sent to ") + node + "::" + r.user
                                      + (r.ok ? "" : ": " + r.error));
            } catch (const std::exception &e) {
                for (const std::string &u : b.users)
                    report.push_back ("NOT sent to " + node + "::" + u + ": " + e.what ());
            }
        }
        return report;
    }, [this] (Outcome<std::vector<std::string>> o) {
        send_->setEnabled (true);
        if (!o.ok ()) {
            status_->setText (o.error);
            emit sent (false, o.error);
            return;
        }
        QStringList lines;
        bool all = true;
        for (const std::string &l : *o.value) {
            lines << qs (l);
            if (l.rfind ("NOT", 0) == 0) all = false;
        }
        status_->setText (lines.join ('\n'));
        emit sent (all, lines.join ('\n'));
        if (all) accept ();
    });
}

}   // namespace gui
