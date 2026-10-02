// The desktop's windows, driven against a real decnetd and dnfal.  Run by
// gui_smoke.sh, which starts them and sets:
//
//   PNW_TEST_SOCKET   the API socket
//   PNW_TEST_NODE     the node with dnfal
//   PNW_TEST_ROOT     dnfal's root directory, to check what happened
//
// with QT_QPA_PLATFORM=offscreen, so no display is needed.
//
// vms_node runs only when PNW_TEST_VMS is set, to NODE"user password",
// against a real VMS node through PNW_TEST_SOCKET.  PNW_TEST_SHOTS names a
// directory to save pictures of the windows in.

#include "common.h"
#include "decwindows.h"
#include "filewindow.h"
#include "maildialog.h"
#include "mainwindow.h"

#include <QDir>
#include <QProcess>
#include <QFile>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QApplication>
#include <QDialog>

#include <optional>

namespace {

QString env (const char *name) { return qEnvironmentVariable (name); }

QByteArray read_all (const QString &path)
{
    QFile f (path);
    return f.open (QIODevice::ReadOnly) ? f.readAll () : QByteArray ();
}

}   // namespace

class TestGui : public QObject {
    Q_OBJECT

private:
    // Wait for the window's next finished signal; true if it was ok.
    static bool done (gui::FileWindow &w, QSignalSpy &spy)
    {
        if (spy.isEmpty () && !spy.wait (20000)) return false;
        bool ok = spy.takeFirst ().at (0).toBool ();
        return ok;
    }

private slots:
    void initTestCase ()
    {
        QVERIFY2 (!env ("PNW_TEST_SOCKET").isEmpty (), "run this from gui_smoke.sh");
        gui::override_api_socket (env ("PNW_TEST_SOCKET"));
        // Settings of our own, so favourites tried here are not yours.
        QCoreApplication::setOrganizationName ("PathNoWorksTest");
        QCoreApplication::setApplicationName ("test_gui");
        QSettings ().remove ("nodes/favourites");
    }

    void the_network ()
    {
        gui::MainWindow w;
        QSignalSpy spy (&w, &gui::MainWindow::refreshed);
        QVERIFY (spy.wait (20000));
        QVERIFY (spy.at (0).at (0).toBool ());
        QCOMPARE (w.system (), QString ("NODEB"));
        // Both nodes, reachable.
        QCOMPARE (w.visible_nodes (), 2);
    }

    void favourites ()
    {
        gui::MainWindow w;
        QSignalSpy spy (&w, &gui::MainWindow::refreshed);
        QVERIFY (spy.wait (20000));
        QCOMPARE (w.visible_nodes (), 2);
        auto *table = w.findChild<QTableView *> ();
        QVERIFY (table);
        auto first = [table] { return table->model ()->index (0, 0).data ().toString (); };
        QCOMPARE (first (), QString ("NODEA"));     // by address

        // A favourite comes first, whatever the sort.
        w.toggle_favourite ("nodeb");
        QVERIFY (w.is_favourite ("NODEB"));
        QCOMPARE (first (), QString ("NODEB"));
        table->sortByColumn (1, Qt::DescendingOrder);
        QCOMPARE (first (), QString ("NODEB"));
        table->sortByColumn (1, Qt::AscendingOrder);

        // One the node list does not have still shows, reachable only or not.
        w.toggle_favourite ("NOSUCH");
        QCOMPARE (w.visible_nodes (), 3);

        // Kept for next time.
        gui::MainWindow again;
        QSignalSpy spy2 (&again, &gui::MainWindow::refreshed);
        QVERIFY (spy2.wait (20000));
        QCOMPARE (again.favourites (), (QStringList { "NODEB", "NOSUCH" }));
        QCOMPARE (again.visible_nodes (), 3);

        again.toggle_favourite ("NOSUCH");
        QCOMPARE (again.visible_nodes (), 2);
        QCOMPARE (QSettings ().value ("nodes/favourites").toStringList (),
                  QStringList { "NODEB" });
    }

    void decwindows_list ()
    {
        gui::decw_reset_apps ();
        QCOMPARE (gui::decw_apps ().size (), gui::decw_default_apps ().size ());

        // A list of one's own: a built-in one kept, a new one, a name that
        // clashes with it, and a long one.
        gui::decw_set_apps ({
            { "Calculator", {}, "RUN SYS$SYSTEM:DECW$CALC", {} },
            { "CALC", {}, "RUN DUA0:[TOOLS]MYCALC", {} },
            { "Monitor everything, please!", {}, "CREATE/TERMINAL=DECTERM/WAIT", {} },
            { "", {}, "RUN NOTHING", {} },                  // no name: dropped
            { "Nothing to run", {}, "", {} },               // no command: dropped
        });
        QList<gui::DecwApp> apps = gui::decw_apps ();
        QCOMPARE (apps.size (), 3);
        QCOMPARE (apps[0].label, QString ("Calculator"));
        QCOMPARE (apps[0].task, QString ("PNWXCALC"));       // the built-in one's
        QCOMPARE (apps[0].icon, QString ("accessories-calculator"));
        QCOMPARE (apps[1].task, QString ("PNWXCALC2"));      // unique
        QCOMPARE (apps[1].command, QString ("RUN DUA0:[TOOLS]MYCALC"));
        QCOMPARE (apps[2].task, QString ("PNWXMONITORE"));   // twelve at most
        for (const gui::DecwApp &a : apps) {
            QVERIFY (a.task.size () <= 12);
            QVERIFY (QRegularExpression ("^[A-Z0-9]+$").match (a.task).hasMatch ());
        }
        QVERIFY (gui::decw_procedure (apps[1], "29.151").contains ("$ RUN DUA0:[TOOLS]MYCALC\n"));
        QVERIFY (gui::decw_procedure (apps[1], "29.151").contains ("/NODE=29.151/TRANSPORT=DECNET"));

        gui::decw_reset_apps ();
        QCOMPARE (gui::decw_apps ().size (), gui::decw_default_apps ().size ());

        // A picture of the Customise dialog, if asked.
        QString shots = env ("PNW_TEST_SHOTS");
        if (!shots.isEmpty ()) {
            QTimer::singleShot (500, [shots] {
                if (QWidget *w = QApplication::activeModalWidget ()) {
                    w->grab ().save (shots + "/decwindows-customize.png");
                    static_cast<QDialog *> (w)->reject ();
                }
            });
            QVERIFY (!gui::decw_customize (nullptr));
        }
    }

    void drag_select ()
    {
        // Dragging a file out must not also select every row the mouse
        // passes over while the files are fetched.
        gui::FileList list;
        list.setSelectionMode (QAbstractItemView::ExtendedSelection);
        for (int i = 0; i < 6; ++i)
            list.addTopLevelItem (new QTreeWidgetItem (QStringList { QString ("F%1.TXT").arg (i) }));
        list.resize (300, 300);
        list.show ();
        QVERIFY (QTest::qWaitForWindowExposed (&list));
        QWidget *vp = list.viewport ();
        auto at = [&] (int row) { return list.visualItemRect (list.topLevelItem (row)).center (); };
        auto send = [&] (QEvent::Type type, QPoint p, Qt::MouseButton b, Qt::MouseButtons bs) {
            QMouseEvent e (type, p, vp->mapToGlobal (p), b, bs, Qt::NoModifier);
            QApplication::sendEvent (vp, &e);
        };
        int drags = 0;
        connect (&list, &gui::FileList::drag_wanted, &list, [&] {
            if (drags++) return;
            // Fetching: the button is still down and the mouse wanders.
            for (int row = 1; row < 6; ++row)
                send (QEvent::MouseMove, at (row), Qt::NoButton, Qt::LeftButton);
        });
        send (QEvent::MouseButtonPress, at (0), Qt::LeftButton, Qt::LeftButton);
        send (QEvent::MouseMove, at (0) + QPoint (0, 2), Qt::NoButton, Qt::LeftButton);
        send (QEvent::MouseMove, at (0) + QPoint (40, 4), Qt::NoButton, Qt::LeftButton);
        send (QEvent::MouseButtonRelease, at (5), Qt::LeftButton, Qt::NoButton);
        QCOMPARE (drags, 1);
        QCOMPARE (list.selectedItems ().size (), 1);
        QCOMPARE (list.selectedItems ()[0]->text (0), QString ("F0.TXT"));

        // And the list still selects as usual afterwards.
        send (QEvent::MouseMove, at (2), Qt::NoButton, Qt::NoButton);
        send (QEvent::MouseButtonPress, at (2), Qt::LeftButton, Qt::LeftButton);
        send (QEvent::MouseButtonRelease, at (2), Qt::LeftButton, Qt::NoButton);
        QCOMPARE (list.selectedItems ().size (), 1);
        QCOMPARE (list.selectedItems ()[0]->text (0), QString ("F2.TXT"));
    }

    void files ()
    {
        const QString root = env ("PNW_TEST_ROOT");
        auto *w = new gui::FileWindow (env ("PNW_TEST_NODE"));
        QSignalSpy spy (w, &gui::FileWindow::finished);

        // The first look finds dnfal's Unix names.
        QVERIFY (done (*w, spy));
        QVERIFY2 (w->names ().contains ("hello.txt"), qPrintable (w->names ().join (' ')));
        QVERIFY (w->names ().contains ("sub/"));

        w->open_dir ("sub");
        QVERIFY (done (*w, spy));
        QCOMPARE (w->names (), QStringList { "inner.txt" });
        w->up ();
        QVERIFY (done (*w, spy));
        QVERIFY (w->names ().contains ("hello.txt"));

        // Copy here.
        QTemporaryDir tmp;
        w->download ({ "hello.txt" }, tmp.path ());
        QVERIFY (done (*w, spy));
        QCOMPARE (read_all (tmp.filePath ("hello.txt")), read_all (root + "/hello.txt"));

        // Copy there, text and binary.
        QFile t (tmp.filePath ("up.txt"));
        QVERIFY (t.open (QIODevice::WriteOnly));
        t.write ("uploaded\nfrom the desktop\n");
        t.close ();
        QByteArray bin;
        for (int i = 0; i < 2000; ++i) bin.append (char (i * 7));
        QFile b (tmp.filePath ("up.bin"));
        QVERIFY (b.open (QIODevice::WriteOnly));
        b.write (bin);
        b.close ();
        w->upload ({ t.fileName (), b.fileName () });
        QVERIFY (done (*w, spy));
        QVERIFY (w->names ().contains ("up.txt"));
        QCOMPARE (read_all (root + "/up.txt"), QByteArray ("uploaded\nfrom the desktop\n"));
        QCOMPARE (read_all (root + "/up.bin"), bin);

        w->rename_file ("up.txt", "renamed.txt");
        QVERIFY (done (*w, spy));
        QVERIFY (w->names ().contains ("renamed.txt"));
        QVERIFY (!w->names ().contains ("up.txt"));

        w->remove ({ "renamed.txt", "up.bin" });
        QVERIFY (done (*w, spy));
        QVERIFY (!w->names ().contains ("renamed.txt"));
        QVERIFY (!QFile::exists (root + "/up.bin"));

        w->view ("hello.txt");
        QVERIFY (done (*w, spy));

        // Dragging out: the files are fetched first, as local files.
        QList<QUrl> urls = w->fetch_for_drag ({ "hello.txt" });
        QCOMPARE (urls.size (), 1);
        QVERIFY (spy.isEmpty () || spy.takeFirst ().at (0).toBool ());
        QCOMPARE (read_all (urls[0].toLocalFile ()), read_all (root + "/hello.txt"));

        // Dropping on a directory row: into that directory.
        w->upload ({ t.fileName () }, "sub");
        QVERIFY (done (*w, spy));
        QCOMPARE (read_all (root + "/sub/up.txt"), QByteArray ("uploaded\nfrom the desktop\n"));

        // A directory that is not there: an error, said so.
        w->go_to ("nosuch/");
        QVERIFY (!done (*w, spy));
        delete w;
    }

    void vms_node ()
    {
        QString target = env ("PNW_TEST_VMS");
        if (target.isEmpty ()) QSKIP ("PNW_TEST_VMS not set");
        // NODE"user password"
        QRegularExpression re ("^([^\"]+)\"(\\S+) (\\S+)\"$");
        auto m = re.match (target);
        QVERIFY2 (m.hasMatch (), "PNW_TEST_VMS is NODE\"user password\"");
        gui::Login login { m.captured (2), m.captured (3), {}, false };

        gui::MainWindow net;
        QSignalSpy nspy (&net, &gui::MainWindow::refreshed);
        QVERIFY (nspy.wait (30000));
        QVERIFY2 (nspy.at (0).at (0).toBool (), qPrintable (nspy.at (0).at (1).toString ()));

        auto *w = new gui::FileWindow (m.captured (1), login);
        QSignalSpy spy (w, &gui::FileWindow::finished);
        QVERIFY (done (*w, spy));
        // VMS names, in the login directory.
        QCOMPARE (w->location (), m.captured (1).toUpper () + "::[]");
        // Anything, in VMS's upper case names.
        QVERIFY (!w->names ().isEmpty ());
        QCOMPARE (w->names ().first (), w->names ().first ().toUpper ());

        QTemporaryDir tmp;
        QFile t (tmp.filePath ("pnwgui.txt"));
        QVERIFY (t.open (QIODevice::WriteOnly));
        t.write ("From the PathNoWorks desktop.\nSecond line.\n");
        t.close ();
        w->upload ({ t.fileName () });
        QVERIFY (done (*w, spy));
        QVERIFY (w->names ().contains ("PNWGUI.TXT"));
        QTemporaryDir back;
        w->download ({ "PNWGUI.TXT" }, back.path ());
        QVERIFY (done (*w, spy));
        QCOMPARE (read_all (back.filePath ("PNWGUI.TXT")),
                  QByteArray ("From the PathNoWorks desktop.\nSecond line.\n"));

        QString shots = env ("PNW_TEST_SHOTS");
        if (!shots.isEmpty ()) {
            net.resize (760, 420);
            // A couple of favourites, as someone would have.
            net.toggle_favourite ("VAXXY");
            net.toggle_favourite ("BAJI");
            net.show ();
            // A fresh listing, so the status bar shows it.
            w->refresh ();
            QVERIFY (done (*w, spy));
            w->show ();
            QTest::qWait (300);
            net.grab ().save (shots + "/network.png");
            net.toggle_favourite ("VAXXY");
            net.toggle_favourite ("BAJI");
            w->grab ().save (shots + "/files.png");

            // The mail window, filled in as someone might.
            gui::MailDialog mail ("VAXXY::SYSTEM, BAJI::RICHARD");
            auto edits = mail.findChildren<QLineEdit *> ();
            if (edits.size () > 1) edits[1]->setText ("Greetings from Linux");
            if (auto *body = mail.findChild<QPlainTextEdit *> ())
                body->setPlainText ("Hello from PathNoWorks.\n\n"
                                    "This came over DECnet, Mail-11 and all.\n");
            mail.show ();
            if (auto *body = mail.findChild<QPlainTextEdit *> ()) body->setFocus ();
            QTest::qWait (300);
            mail.grab ().save (shots + "/mail.png");
        }

        w->remove ({ "PNWGUI.TXT" });
        QVERIFY (done (*w, spy));
        QVERIFY (!w->names ().contains ("PNWGUI.TXT"));
        delete w;
    }

    // A DECwindows program on the VMS node, its window on this X display:
    // what the DECwindows menus do.  Needs PNW_TEST_VMS and a real DISPLAY.
    void vms_decwindows ()
    {
        QString target = env ("PNW_TEST_VMS");
        if (target.isEmpty () || env ("DISPLAY").isEmpty ()
            || env ("QT_QPA_PLATFORM") == "offscreen")
            QSKIP ("needs PNW_TEST_VMS and a real X display");
        QRegularExpression re ("^([^\"]+)\"(\\S+) (\\S+)\"$");
        auto m = re.match (target);
        QVERIFY (m.hasMatch ());
        gui::Login login { m.captured (2), m.captured (3), {}, false };
        // Calculator unless PNW_TEST_DECW_APP names another.
        QString want = env ("PNW_TEST_DECW_APP").isEmpty () ? "Calculator" : env ("PNW_TEST_DECW_APP");
        QString image = want == "DECterm" ? "DECW$TERMINAL"
                      : want == "FileView" ? "VUE$MASTER"
                      : "DECW$" + want.toUpper ().left (want == "Calculator" ? 4 : 99);
        std::optional<gui::DecwApp> calc;
        for (const gui::DecwApp &a : gui::decw_apps ())
            if (a.label == want) calc = a;
        QVERIFY (calc);

        bool done = false, ok = false;
        QString msg;
        QObject ctx;
        gui::decw_launch (&ctx, m.captured (1), login, *calc, [&] (bool good, const QString &text) {
            done = true; ok = good; msg = text;
        });
        QTRY_VERIFY_WITH_TIMEOUT (done, 60000);
        QVERIFY2 (ok, qPrintable (msg));

        // Its window, on this display.
        auto has_window = [image] {
            QProcess p;
            p.start ("xwininfo", { "-root", "-tree" });
            p.waitForFinished (5000);
            return QString::fromLocal8Bit (p.readAllStandardOutput ()).contains (image);
        };
        QTRY_VERIFY_WITH_TIMEOUT (has_window (), 60000);

        // Pictures of the more interesting ones, if asked.
        QString shots = env ("PNW_TEST_SHOTS");
        if (shots.isEmpty ()) return;
        for (const gui::DecwApp &a : gui::decw_apps ()) {
            if (!QStringList { "DECterm", "FileView", "Paint", "Puzzle" }.contains (a.label)) continue;
            bool d = false;
            gui::decw_launch (&ctx, m.captured (1), login, a, [&] (bool, const QString &) { d = true; });
            QTRY_VERIFY_WITH_TIMEOUT (d, 60000);
        }
        QTest::qWait (45000);
        QProcess tree;
        tree.start ("xwininfo", { "-root", "-tree" });
        tree.waitForFinished (5000);
        QFile list (shots + "/windows.txt");
        if (list.open (QIODevice::WriteOnly)) list.write (tree.readAllStandardOutput ());
        for (const QString &line : QString::fromLocal8Bit (QFile (shots + "/windows.txt").exists ()
                 ? [&] { QFile f (shots + "/windows.txt"); f.open (QIODevice::ReadOnly); return f.readAll (); } ()
                 : QByteArray ()).split ('\n')) {
            // Top-level application windows of a useful size.
            QRegularExpression w ("^\\s+(0x[0-9a-f]+) \"([^\"]+)\": \\(\"[^\"]*\" \"(DECW\\$[A-Z_]+|VUE\\$[A-Z_]+)\"\\)\\s+(\\d+)x(\\d+)");
            auto wm = w.match (line);
            if (!wm.hasMatch () || wm.captured (4).toInt () < 60 || wm.captured (5).toInt () < 40) continue;
            QString name = wm.captured (3).toLower ().replace ('$', '-') + "-" + wm.captured (1);
            QProcess::execute ("sh", { "-c", "xwd -silent -id " + wm.captured (1) + " > '" + shots + "/" + name + ".xwd'" });
        }
    }
};

QTEST_MAIN (TestGui)
#include "test_gui.moc"
