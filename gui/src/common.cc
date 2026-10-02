// common.cc -- what the PathNoWorks windows share.

#include "common.h"

#include "pnw/api.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

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
#ifdef Q_OS_WIN
    const QString file = name + ".exe";
#else
    const QString &file = name;
#endif
    // Installed side by side, or this program in build/gui and the tool
    // in build/tools/<dir>/.
    const QStringList candidates {
        here.filePath (file),
        here.filePath ("../tools/" + name + "/" + file),
        here.filePath ("../tools/pnw-nft/" + file),
    };
    for (const QString &c : candidates) {
        QFileInfo f (c);
        if (f.isFile () && f.isExecutable ()) return f.canonicalFilePath ();
    }
    return QStandardPaths::findExecutable (name);
}

void no_console_window (QProcess &p)
{
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier ([] (QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
    });
#else
    (void) p;
#endif
}

#ifdef Q_OS_WIN

bool open_terminal (const QString &title, const QStringList &command,
                    QString *error)
{
    // Windows Terminal is a VT terminal (with Sixel, from 1.22), and lets
    // the window be sized: 80x24, which full-screen VMS programs expect.
    QString wt = QStandardPaths::findExecutable ("wt");
    if (!wt.isEmpty ()) {
        QStringList args { "--window", "new", "--size", "80,24",
                           "new-tab", "--title", title,
                           "--suppressApplicationTitle", "--" };
        args += command;
        if (QProcess::startDetached (wt, args)) return true;
    }
    // Otherwise a console window of its own.  pnw-sethost puts it in VT
    // mode.
    QProcess p;
    p.setProgram (command.value (0));
    p.setArguments (command.mid (1));
    p.setCreateProcessArgumentsModifier ([title] (QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NEW_CONSOLE;
        static std::wstring t;
        t = title.toStdWString ();
        a->startupInfo->lpTitle = t.data ();
    });
    if (!p.startDetached ()) {
        if (error) *error = "cannot start " + command.value (0);
        return false;
    }
    return true;
}

#else

bool open_terminal (const QString &title, const QStringList &command,
                    QString *error)
{
    QString xterm = QStandardPaths::findExecutable ("xterm");
    if (xterm.isEmpty ()) {
        if (error) *error = "xterm is not installed";
        return false;
    }
    // Copy and paste as the rest of the desktop does it.  xterm on its own
    // only sets the PRIMARY selection (middle button), which Ctrl+V in
    // other programs never sees.  So: selecting sets the CLIPBOARD too,
    // Ctrl+Shift+C copies, Ctrl+Shift+V and Shift+Insert paste the
    // clipboard, and the middle button still pastes the selection.
    // Ctrl+Shift+C is taken here, so it never reaches the node as Ctrl+C.
    static const char *const clipboard =
        "XTerm*VT100.translations: #override \\n"
        " Ctrl Shift <Key>C: copy-selection(CLIPBOARD) \\n"
        " Ctrl Shift <Key>V: insert-selection(CLIPBOARD) \\n"
        " Shift <Key>Insert: insert-selection(CLIPBOARD, PRIMARY) \\n"
        " <Btn1Up>: select-end(PRIMARY, CLIPBOARD, CUT_BUFFER0) \\n"
        " <Btn3Up>: select-end(PRIMARY, CLIPBOARD, CUT_BUFFER0)";
    QStringList args {
        "-ti", "vt340",
        "-xrm", "XTerm*decTerminalID: vt340",
        "-xrm", "XTerm*numColorRegisters: 256",
        "-xrm", clipboard,
        // A readable size: xterm's own bitmap font makes 80x24 a postage
        // stamp.  Still 80x24, which full-screen VMS programs expect; the
        // window grows with the font, and resizing tells VMS the new size.
        "-fa", "Monospace", "-fs", "12",
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

#endif

bool open_sethost (const QString &node, QString *error)
{
    QString tool = find_tool ("pnw-sethost");
    if (tool.isEmpty ()) {
        if (error) *error = "pnw-sethost not found";
        return false;
    }
    // --wait: should it fail to connect, the window stays to say why.
    return open_terminal (node + " - SET HOST",
                          { tool, "--wait", "-s", api_socket (), node }, error);
}

unsigned reject_reason (const std::exception &e)
{
    if (auto *r = dynamic_cast<const pnw::Rejected *> (&e)) return r->reason ();
    return 0;
}

}   // namespace gui
