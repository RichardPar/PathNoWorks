// common.cc -- what the PathNoWorks windows share.

#include "common.h"

#include "pnw/api.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace gui {

QString qs (const std::string &s) { return QString::fromStdString (s); }
std::string ss (const QString &s) { return s.toStdString (); }

namespace {
QString override_socket;
}

void override_api_socket (const QString &path) { override_socket = path; }

QString api_socket ()
{
    if (!override_socket.isEmpty ()) return override_socket;
    QSettings settings;
    QString s = settings.value ("api/socket").toString ();
    if (!s.isEmpty ()) return s;
    return qs (pnw::Api::default_path ());
}

void set_api_socket (const QString &path)
{
    QSettings settings;
    if (path.isEmpty ()) settings.remove ("api/socket");
    else settings.setValue ("api/socket", path);
}

QString find_tool (const QString &name)
{
    QDir here (QCoreApplication::applicationDirPath ());
    // Installed side by side, or this program in build/gui and the tool
    // in build/tools/<dir>/.
    const QStringList candidates {
        here.filePath (name),
        here.filePath ("../tools/" + name + "/" + name),
        here.filePath ("../tools/pnw-nft/" + name),
    };
    for (const QString &c : candidates) {
        QFileInfo f (c);
        if (f.isFile () && f.isExecutable ()) return f.canonicalFilePath ();
    }
    return QStandardPaths::findExecutable (name);
}

bool open_terminal (const QString &title, const QStringList &command,
                    QString *error)
{
    QString xterm = QStandardPaths::findExecutable ("xterm");
    if (xterm.isEmpty ()) {
        if (error) *error = "xterm is not installed";
        return false;
    }
    QStringList args {
        "-ti", "vt340",
        "-xrm", "XTerm*decTerminalID: vt340",
        "-xrm", "XTerm*numColorRegisters: 256",
        "-geometry", "80x24",
        "-T", title,
        "-e" };
    args += command;
    if (!QProcess::startDetached (xterm, args)) {
        if (error) *error = "cannot start xterm";
        return false;
    }
    return true;
}

bool open_sethost (const QString &node, QString *error)
{
    QString tool = find_tool ("pnw-sethost");
    if (tool.isEmpty ()) {
        if (error) *error = "pnw-sethost not found";
        return false;
    }
    return open_terminal (node + " - SET HOST",
                          { tool, "-s", api_socket (), node }, error);
}

unsigned reject_reason (const std::exception &e)
{
    if (auto *r = dynamic_cast<const pnw::Rejected *> (&e)) return r->reason ();
    return 0;
}

}   // namespace gui
