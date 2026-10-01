// maildialog.h -- write and send DECnet mail.

#ifndef PNW_GUI_MAILDIALOG_H
#define PNW_GUI_MAILDIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace gui {

class MailDialog : public QDialog {
    Q_OBJECT
public:
    explicit MailDialog (const QString &to = {}, QWidget *parent = nullptr);

signals:
    void sent (bool ok, const QString &report);

private:
    void send ();

    QLineEdit      *to_, *subject_;
    QPlainTextEdit *body_;
    QLabel         *status_;
    QPushButton    *send_;
};

}   // namespace gui

#endif
