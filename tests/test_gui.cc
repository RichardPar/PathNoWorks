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
#include "filewindow.h"
#include "mainwindow.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
            net.show ();
            w->show ();
            QTest::qWait (300);
            net.grab ().save (shots + "/network.png");
            w->grab ().save (shots + "/files.png");
        }

        w->remove ({ "PNWGUI.TXT" });
        QVERIFY (done (*w, spy));
        QVERIFY (!w->names ().contains ("PNWGUI.TXT"));
        delete w;
    }
};

QTEST_MAIN (TestGui)
#include "test_gui.moc"
