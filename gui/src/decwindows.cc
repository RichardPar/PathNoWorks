// decwindows.cc -- a VMS node's DECwindows programs on this screen.

#include "decwindows.h"

#include "common.h"

#include "pnw/dap.h"
#include "pnw/nodes.h"

#include <QCoreApplication>
#include <QMenu>
#include <QProcess>

namespace gui {

const QList<DecwApp> &decw_apps ()
{
    // What DECwindows Motif installs on VMS.  DECterm waits for its window
    // to close; the rest run until they are quit.
    static const QList<DecwApp> apps {
        { "DECterm",      "PNWXDECTERM",  "CREATE/TERMINAL=DECTERM/WAIT",          "utilities-terminal" },
        { "FileView",     "PNWXFILEVIEW", "RUN SYS$SYSTEM:VUE$MASTER",             "system-file-manager" },
        { "Mail",         "PNWXMAIL",     "RUN SYS$SYSTEM:DECW$MAIL",              "mail-client" },
        { "Notepad",      "PNWXNOTEPAD",  "RUN SYS$SYSTEM:DECW$NOTEPAD",           "accessories-text-editor" },
        { "Paint",        "PNWXPAINT",    "RUN SYS$SYSTEM:DECW$PAINT",             "applications-graphics" },
        { "Calculator",   "PNWXCALC",     "RUN SYS$SYSTEM:DECW$CALC",              "accessories-calculator" },
        { "Calendar",     "PNWXCALENDAR", "RUN SYS$SYSTEM:DECW$CALENDAR",          "x-office-calendar" },
        { "Cardfiler",    "PNWXCARDFILE", "RUN SYS$SYSTEM:DECW$CARDFILER",         "x-office-address-book" },
        { "Clock",        "PNWXCLOCK",    "RUN SYS$SYSTEM:DECW$CLOCK",             "preferences-system-time" },
        { "Puzzle",       "PNWXPUZZLE",   "RUN SYS$SYSTEM:DECW$PUZZLE",            "applications-games" },
        { "Bookreader",   "PNWXBOOKS",    "RUN SYS$SYSTEM:DECW$BOOKREADER",        "help-browser" },
    };
    return apps;
}

QString decw_procedure (const DecwApp &app, const QString &display_node)
{
    return QString (
        "$ ! %1 for PathNoWorks: run by a DECnet connection to task %2,\n"
        "$ ! it shows on %3's screen.  Safe to delete.\n"
        "$ SET NOON\n"
        "$ ! Accept the connection, so the requester is not kept waiting.\n"
        "$ OPEN/READ/WRITE PNW$NET SYS$NET\n"
        "$ CLOSE PNW$NET\n"
        "$ SET DISPLAY/CREATE/NODE=%3/TRANSPORT=DECNET\n"
        "$ %4\n"
        "$ EXIT\n").arg (app.label, app.task, display_node, app.command);
}

// ------------------------------------------------------------ the bridge

namespace {

QProcess   *bridge = nullptr;
QStringList allowed;

}   // namespace

bool decw_bridge (const QString &node, QString *error)
{
    QString n = node.toUpper ();
    if (bridge && bridge->state () == QProcess::Running && allowed.contains (n))
        return true;
    // Someone else's bridge was found last time: still there?
    if (!bridge && !allowed.isEmpty () && allowed.contains (n)) return true;

    QString tool = find_tool ("pnw-x11");
    if (tool.isEmpty ()) {
        if (error) *error = "pnw-x11 not found";
        return false;
    }
    if (!allowed.contains (n)) allowed << n;

    // Start again with the wider list: one bridge serves display 0.
    if (bridge) {
        bridge->terminate ();
        bridge->waitForFinished (3000);
        delete bridge;
    }
    bridge = new QProcess (QCoreApplication::instance ());
    bridge->setProcessChannelMode (QProcess::MergedChannels);
    bridge->start (tool, { "--socket", api_socket (), "serve", "--allow", allowed.join (',') });
    if (!bridge->waitForStarted (5000)) {
        if (error) *error = "cannot start pnw-x11";
        return false;
    }
    // It says when it is serving, or why not.
    QString out;
    for (int i = 0; i < 30 && !out.contains ("XSERVING"); ++i) {
        if (bridge->waitForReadyRead (200)) out += QString::fromLocal8Bit (bridge->readAll ());
        if (bridge->state () != QProcess::Running) break;
    }
    if (out.contains ("XSERVING")) {
        // From now on its log goes nowhere, rather than piling up here.
        QObject::connect (bridge, &QProcess::readyRead, bridge, [] { bridge->readAll (); });
        return true;
    }
    out += QString::fromLocal8Bit (bridge->readAll ());
    // Another pnw-x11 serve already has the display -- one run by hand,
    // say.  Use it; it has to let the node in itself.
    if (out.contains ("already registered")) {
        delete bridge;
        bridge = nullptr;
        allowed.clear ();
        return true;
    }
    if (error) {
        *error = "pnw-x11 serve did not start: " + out.trimmed ();
        // Most likely another one already has the display.
        if (out.contains ("already", Qt::CaseInsensitive) || out.contains ("bind", Qt::CaseInsensitive))
            *error += "\nIf you run pnw-x11 serve yourself, let " + n + " in with --allow.";
    }
    return false;
}

// ------------------------------------------------------------ launching

void decw_launch (QObject *ctx, const QString &node, const Login &login,
                  const DecwApp &app,
                  std::function<void (bool, const QString &)> done)
{
    QString error;
    if (!decw_bridge (node, &error)) {
        done (false, error);
        return;
    }
    pnw::RemoteSpec spec;
    spec.node = ss (node);
    spec.user = ss (login.user);
    spec.password = ss (login.password);
    spec.account = ss (login.account);
    bool proxy = login.proxy;
    std::string socket = ss (api_socket ());
    DecwApp a = app;

    in_background (ctx, [=] () -> std::string {
        pnw::Api api (socket);
        // This node as the far end will know it: by address, since a name
        // it was only told volatilely is gone after a reboot.
        std::string self = api.system ();
        try {
            for (const pnw::NodeRow &r : pnw::known_nodes (api))
                if (r.executor) self = r.address;
        } catch (const std::exception &) {}

        // The procedure, in the login directory: the one before goes, so
        // versions do not pile up.
        std::string file = ss (a.task) + ".COM";
        std::string text = ss (decw_procedure (a, qs (self)));
        {
            pnw::DapSession s (api, spec, proxy);
            try { s.erase (file + ";*"); } catch (const std::exception &) {}
        }
        {
            pnw::DapSession s (api, spec, proxy);
            bool sent = false;
            s.put (file, true, [&] {
                if (sent) return decnet::Bytes ();
                sent = true;
                return decnet::Bytes (text.begin (), text.end ());
            });
        }

        // Connecting to the task runs it.  The procedure accepts at once
        // and carries on by itself; the link has done its job.
        pnw::ConnectOptions o;
        o.dest = spec.node;
        o.object = ss (a.task);
        o.username = spec.user;
        o.password = spec.password;
        o.account = spec.account;
        o.proxy = proxy && spec.user.empty ();
        auto link = api.connect (o);
        link->disconnect ();
        return self;
    }, [done, a, node] (Outcome<std::string> o) {
        if (!o.ok ()) {
            done (false, "Could not start " + a.label + " on " + node + ": " + o.error);
            return;
        }
        done (true, a.label + " is starting on " + node + "; its window comes here (display "
                    + qs (*o.value) + "::0)");
    });
}

QMenu *decw_menu (QWidget *parent, std::function<void (const DecwApp &)> pick)
{
    auto *m = new QMenu ("DECwindows", parent);
    m->setIcon (QIcon::fromTheme ("preferences-desktop-display"));
    m->setToolTip ("Run one of the node's DECwindows programs, here");
    for (const DecwApp &a : decw_apps ())
        m->addAction (QIcon::fromTheme (a.icon), a.label, parent, [pick, a] { pick (a); });
    return m;
}

}   // namespace gui
