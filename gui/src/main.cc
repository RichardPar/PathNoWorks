// pathnoworks -- the PathNoWorks desktop: DECnet nodes, their files,
// terminals and mail, through decnetd.
//
//     pathnoworks                   # the network
//     pathnoworks --files VAXXY     # straight to a node's files
//     pathnoworks --socket PATH     # another decnetd (also the setting)

#include "filewindow.h"
#include "mainwindow.h"
#include "common.h"

#include <QApplication>
#include <QCommandLineParser>

int main (int argc, char **argv)
{
    QApplication app (argc, argv);
    QApplication::setApplicationName ("PathNoWorks");
    QApplication::setOrganizationName ("PathNoWorks");
    QApplication::setApplicationVersion ("0.1");
    QApplication::setWindowIcon (QIcon::fromTheme ("network-workgroup"));

    QCommandLineParser p;
    p.setApplicationDescription ("Pathworks-style DECnet desktop");
    p.addHelpOption ();
    p.addVersionOption ();
    QCommandLineOption socket ("socket", "decnetd API socket, for this run", "path");
    QCommandLineOption files ("files", "open a node's files", "node");
    p.addOption (socket);
    p.addOption (files);
    p.process (app);

    if (p.isSet (socket)) gui::override_api_socket (p.value (socket));

    gui::MainWindow w;
    if (p.isSet (files)) w.open_files (p.value (files));
    else w.show ();
    return app.exec ();
}
