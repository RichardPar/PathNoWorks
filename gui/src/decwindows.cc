// decwindows.cc -- a VMS node's DECwindows programs on this screen.

#include "decwindows.h"

#include "common.h"

#include "pnw/dap.h"
#include "pnw/nodes.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QMenu>
#include <QProcess>
#include <QSettings>

#ifdef Q_OS_WIN
#include "decnet/common/platform.h"

#include <QRandomGenerator>
#include <QThread>
#endif

namespace gui {

const QList<DecwApp> &decw_default_apps ()
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

namespace {

const char *const SETTING = "decwindows/apps";

// A task name for a program: PNWX and up to eight letters or digits of
// its name, unique among those taken.  VMS task names stop at twelve.
QString task_for (const QString &label, const QStringList &taken)
{
    QString base;
    for (QChar c : label.toUpper ())
        if (c.isLetterOrNumber () && c.unicode () < 128) base += c;
    base = "PNWX" + base.left (8);
    if (base == "PNWX") base = "PNWXPROGRAM";
    QString t = base;
    for (int n = 2; taken.contains (t); ++n) {
        QString suffix = QString::number (n);
        t = base.left (12 - suffix.size ()) + suffix;
    }
    return t;
}

}   // namespace

QList<DecwApp> decw_apps ()
{
    QSettings settings;
    if (!settings.contains (SETTING)) return decw_default_apps ();
    QList<DecwApp> out;
    QStringList taken;
    for (const QVariant &v : settings.value (SETTING).toList ()) {
        QVariantMap m = v.toMap ();
        DecwApp a;
        a.label = m.value ("label").toString ().trimmed ();
        a.command = m.value ("command").toString ().trimmed ();
        if (a.label.isEmpty () || a.command.isEmpty ()) continue;
        a.icon = m.value ("icon").toString ();
        // The built-in ones keep their task names and icons.
        for (const DecwApp &d : decw_default_apps ())
            if (d.label == a.label) {
                if (!taken.contains (d.task)) a.task = d.task;
                if (a.icon.isEmpty ()) a.icon = d.icon;
            }
        if (a.task.isEmpty ()) a.task = task_for (a.label, taken);
        if (a.icon.isEmpty ()) a.icon = "application-x-executable";
        taken << a.task;
        out << a;
    }
    return out;
}

void decw_set_apps (const QList<DecwApp> &apps)
{
    QVariantList list;
    for (const DecwApp &a : apps)
        list << QVariantMap { { "label", a.label }, { "command", a.command }, { "icon", a.icon } };
    QSettings ().setValue (SETTING, list);
}

void decw_reset_apps ()
{
    QSettings ().remove (SETTING);
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

// DEC's font names, if decw-font-aliases.py has made them.
QString decw_font_dir ()
{
    return QDir::home ().filePath (".local/share/fonts/decwindows");
}

#ifdef Q_OS_WIN

// ---------------------------------------------- the X server, on Windows
//
// Windows has no X server running as a matter of course, so the desktop
// starts VcXsrv when DECwindows is first wanted: display :0, each program
// in a window of its own, and a login cookie, which pnw-x11 hands on to
// the programs it lets in.  Without one VcXsrv would want -ac, letting
// anyone on the network onto the screen.

// Is an X server answering on display n here?
bool x_listening (int n)
{
    int fd = decnet::sock_open (AF_INET, SOCK_STREAM);
    if (fd < 0) return false;
    sockaddr_in a {};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    a.sin_port = htons (static_cast<std::uint16_t> (6000 + n));
    bool ok = ::connect (fd, reinterpret_cast<sockaddr *> (&a), sizeof a) == 0;
    decnet::sock_close (fd);
    return ok;
}

QString xauthority_path ()
{
    QString x = qEnvironmentVariable ("XAUTHORITY");
    return x.isEmpty () ? QDir::home ().filePath (".Xauthority") : x;
}

// Make sure the .Xauthority file has a MIT-MAGIC-COOKIE-1 for display 0,
// adding one if not.  Entries: family, then address, display number, name
// and data, each a 16-bit big-endian length and its bytes.
bool ensure_cookie (const QString &path, QString *error)
{
    QByteArray all;
    if (QFile f (path); f.open (QIODevice::ReadOnly)) all = f.readAll ();
    int at = 0;
    auto u16 = [&] () -> int {
        if (at + 2 > all.size ()) return -1;
        int v = (static_cast<unsigned char> (all[at]) << 8) | static_cast<unsigned char> (all[at + 1]);
        at += 2;
        return v;
    };
    auto field = [&] (QByteArray &out) {
        int n = u16 ();
        if (n < 0 || at + n > all.size ()) return false;
        out = all.mid (at, n);
        at += n;
        return true;
    };
    while (at < all.size ()) {
        QByteArray addr, number, name, data;
        if (u16 () < 0 || !field (addr) || !field (number) || !field (name) || !field (data))
            break;
        if (name == "MIT-MAGIC-COOKIE-1" && (number.isEmpty () || number == "0"))
            return true;
    }
    QByteArray cookie (16, '\0');
    QRandomGenerator::system ()->fillRange (reinterpret_cast<quint32 *> (cookie.data ()), 4);
    auto put = [] (QByteArray &b, const QByteArray &s) {
        b.append (static_cast<char> (s.size () >> 8)).append (static_cast<char> (s.size ()));
        b.append (s);
    };
    QByteArray entry ("\xff\xff", 2);            // FamilyWild: any host
    put (entry, QByteArray ());
    put (entry, "0");
    put (entry, "MIT-MAGIC-COOKIE-1");
    put (entry, cookie);
    QFile f (path);
    if (!f.open (QIODevice::Append)) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    f.write (entry);
    return true;
}

QString vcxsrv_dir ()
{
    QSettings reg ("HKEY_LOCAL_MACHINE\\SOFTWARE\\VcXsrv", QSettings::NativeFormat);
    for (const char *key : { "Install_Dir_64", "Install_Dir" }) {
        QString d = reg.value (key).toString ();
        if (!d.isEmpty () && QFile::exists (d + "/vcxsrv.exe")) return d;
    }
    QString d = qEnvironmentVariable ("ProgramFiles", "C:/Program Files") + "/VcXsrv";
    return QFile::exists (d + "/vcxsrv.exe") ? d : QString ();
}

// An X server on display :0, started if there is none.
bool ensure_x_server (QString *error)
{
    if (x_listening (0)) return true;           // VcXsrv or another, already up
    QString dir = vcxsrv_dir ();
    if (dir.isEmpty ()) {
        if (error) *error = "No X server is running, and VcXsrv is not installed.\n"
                            "Install VcXsrv (https://github.com/marchaesen/vcxsrv), "
                            "or start an X server on display :0.";
        return false;
    }
    QString auth = xauthority_path ();
    if (!ensure_cookie (auth, error)) return false;
    QStringList args { ":0", "-multiwindow", "-clipboard", "-wgl",
                       "-auth", QDir::toNativeSeparators (auth) };
    // VcXsrv's own font path, and DEC's names after it.  Given -fp, VcXsrv
    // takes neither the relative paths of its default ("./fonts/misc/") nor
    // "C:/..." (the colon and slash read as a font server's address): only
    // full paths with backslashes.  Any it cannot use are dropped, and with
    // them gone it has only its built-in fixed font.  No trailing
    // backslash, either: in a quoted argument it would escape the quote.
    if (QFile::exists (decw_font_dir () + "/fonts.alias")) {
        QStringList fp;
        for (const char *d : { "misc", "TTF", "OTF", "Type1", "100dpi", "75dpi",
                               "cyrillic", "Speedo", "terminus-font" })
            if (QFileInfo (dir + "/fonts/" + d).isDir ())
                fp << QDir::toNativeSeparators (dir + "/fonts/" + d);
        fp << QDir::toNativeSeparators (decw_font_dir ());
        args << "-fp" << fp.join (',');
    }
    if (!QProcess::startDetached (dir + "/vcxsrv.exe", args, dir)) {
        if (error) *error = "cannot start " + dir + "/vcxsrv.exe";
        return false;
    }
    for (int i = 0; i < 100 && !x_listening (0); ++i) QThread::msleep (100);
    if (!x_listening (0)) {
        if (error) *error = "VcXsrv did not start answering on display :0";
        return false;
    }
    return true;
}

#endif

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
#ifdef Q_OS_WIN
    if (!ensure_x_server (error)) return false;
#endif
    if (!allowed.contains (n)) allowed << n;

    // Start again with the wider list: one bridge serves display 0.
    if (bridge) {
#ifdef Q_OS_WIN
        // terminate () asks a window to close, and pnw-x11 has none.
        bridge->kill ();
#else
        bridge->terminate ();
#endif
        bridge->waitForFinished (3000);
        delete bridge;
    }
#ifndef Q_OS_WIN
    // DEC's font names, if decw-font-aliases.py has made them: the font
    // path lasts only as long as the X session, so add it each time.  (On
    // Windows the X server's own font path is set where it is started.)
    QString fonts = decw_font_dir ();
    if (QFile::exists (fonts + "/fonts.alias")) {
        QProcess q;
        q.start ("xset", { "q" });
        q.waitForFinished (3000);
        if (!QString::fromLocal8Bit (q.readAllStandardOutput ()).contains (fonts)) {
            QProcess::execute ("xset", { "+fp", fonts + "/" });
            QProcess::execute ("xset", { "fp", "rehash" });
        }
    }
#endif
    bridge = new QProcess (QCoreApplication::instance ());
    no_console_window (*bridge);
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

bool decw_customize (QWidget *parent)
{
    QDialog d (parent);
    d.setWindowTitle ("DECwindows programs");
    d.resize (640, 420);
    auto *v = new QVBoxLayout (&d);
    auto *intro = new QLabel (
        "The programs on the DECwindows menus. Each one is a name for the menu "
        "and the DCL that starts it on the VMS node, after its display has been "
        "set to this machine: <tt>RUN SYS$SYSTEM:DECW$CALC</tt>, say, or "
        "<tt>CREATE/TERMINAL=DECTERM/WAIT</tt>.");
    intro->setWordWrap (true);
    v->addWidget (intro);

    auto *row = new QHBoxLayout;
    auto *table = new QTableWidget (0, 2);
    table->setHorizontalHeaderLabels ({ "Program", "DCL command" });
    table->horizontalHeader ()->setStretchLastSection (true);
    table->verticalHeader ()->hide ();
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setSelectionMode (QAbstractItemView::SingleSelection);
    row->addWidget (table, 1);

    // Icons ride along unseen: built-in programs keep theirs.
    auto put = [table] (int r, const DecwApp &a) {
        auto *name = new QTableWidgetItem (QIcon::fromTheme (a.icon), a.label);
        name->setData (Qt::UserRole, a.icon);
        table->setItem (r, 0, name);
        table->setItem (r, 1, new QTableWidgetItem (a.command));
    };
    auto fill = [&] (const QList<DecwApp> &apps) {
        table->setRowCount (0);
        for (const DecwApp &a : apps) {
            int r = table->rowCount ();
            table->insertRow (r);
            put (r, a);
        }
        table->resizeColumnToContents (0);
    };
    fill (decw_apps ());

    auto *buttons = new QVBoxLayout;
    auto *add = new QPushButton ("Add");
    auto *remove = new QPushButton ("Remove");
    auto *upb = new QPushButton ("Move up");
    auto *downb = new QPushButton ("Move down");
    auto *defaults = new QPushButton ("Restore defaults");
    for (QPushButton *b : { add, remove, upb, downb }) buttons->addWidget (b);
    buttons->addStretch (1);
    buttons->addWidget (defaults);
    row->addLayout (buttons);
    v->addLayout (row, 1);

    auto take = [table] (int r) {
        DecwApp a;
        a.label = table->item (r, 0) ? table->item (r, 0)->text ().trimmed () : QString ();
        a.icon = table->item (r, 0) ? table->item (r, 0)->data (Qt::UserRole).toString () : QString ();
        a.command = table->item (r, 1) ? table->item (r, 1)->text ().trimmed () : QString ();
        return a;
    };
    auto swap = [&] (int a, int b) {
        if (a < 0 || b < 0 || a >= table->rowCount () || b >= table->rowCount ()) return;
        DecwApp x = take (a), y = take (b);
        put (a, y);
        put (b, x);
        table->selectRow (b);
    };
    QObject::connect (add, &QPushButton::clicked, &d, [&] {
        int r = table->rowCount ();
        table->insertRow (r);
        put (r, { "New program", {}, "RUN SYS$SYSTEM:", "application-x-executable" });
        table->selectRow (r);
        table->editItem (table->item (r, 0));
    });
    QObject::connect (remove, &QPushButton::clicked, &d, [&] {
        int r = table->currentRow ();
        if (r >= 0) table->removeRow (r);
    });
    QObject::connect (upb, &QPushButton::clicked, &d, [&] { int r = table->currentRow (); swap (r, r - 1); });
    QObject::connect (downb, &QPushButton::clicked, &d, [&] { int r = table->currentRow (); swap (r, r + 1); });
    QObject::connect (defaults, &QPushButton::clicked, &d, [&] { fill (decw_default_apps ()); });

    auto *box = new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect (box, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect (box, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    v->addWidget (box);

    if (d.exec () != QDialog::Accepted) return false;
    QList<DecwApp> apps;
    for (int r = 0; r < table->rowCount (); ++r) {
        DecwApp a = take (r);
        if (!a.label.isEmpty () && !a.command.isEmpty ()) apps << a;
    }
    decw_set_apps (apps);
    return true;
}

QMenu *decw_menu (QWidget *parent, std::function<void (const DecwApp &)> pick)
{
    auto *m = new QMenu ("DECwindows", parent);
    m->setIcon (QIcon::fromTheme ("preferences-desktop-display"));
    m->setToolTip ("Run one of the node's DECwindows programs, here");
    // Built each time it opens, so changes to the list show at once.
    auto build = [m, parent, pick] {
        m->clear ();
        for (const DecwApp &a : decw_apps ())
            m->addAction (QIcon::fromTheme (a.icon), a.label, parent, [pick, a] { pick (a); });
        m->addSeparator ();
        m->addAction (QIcon::fromTheme ("configure"), "Customise...", parent,
                      [parent] { decw_customize (parent); });
    };
    build ();
    QObject::connect (m, &QMenu::aboutToShow, m, build);
    return m;
}

}   // namespace gui
