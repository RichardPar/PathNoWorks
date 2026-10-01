// decwindows.h -- run a VMS node's DECwindows programs on this screen.
//
// The program runs on the node and draws here, over DECnet, through
// pnw-x11 serve (which this starts, letting the node in).  To start it, a
// short command procedure goes to the user's login directory by DAP, and
// a connection to it as a DECnet task makes VMS run it in a network job:
// it sets the display to this node and runs the program.

#ifndef PNW_GUI_DECWINDOWS_H
#define PNW_GUI_DECWINDOWS_H

#include "logindialog.h"

#include <QList>
#include <QString>

#include <functional>

class QMenu;
class QObject;
class QWidget;

namespace gui {

struct DecwApp {
    QString label;          // "Calculator"
    QString task;           // the procedure's name on the node, <= 12 chars
    QString command;        // the DCL that runs it
    QString icon;           // a theme icon name
};

// The programs on the DECwindows menus: the user's list from the
// settings, or the built-in one until they change it.  Task names are made
// from the labels, unique and at most twelve characters.
QList<DecwApp> decw_apps ();
const QList<DecwApp> &decw_default_apps ();
void decw_set_apps (const QList<DecwApp> &apps);
void decw_reset_apps ();

// The command procedure that shows app on display_node (an address).
QString decw_procedure (const DecwApp &app, const QString &display_node);

// Make sure pnw-x11 serve is running here and lets node in.  False, with
// error set, if it cannot be started.
bool decw_bridge (const QString &node, QString *error);

// Start app on node, in the background.  done gets whether it started and
// what to tell the user.
void decw_launch (QObject *ctx, const QString &node, const Login &login,
                  const DecwApp &app,
                  std::function<void (bool, const QString &)> done);

// Edit the list in a dialog; true if it was changed.
bool decw_customize (QWidget *parent);

// A menu of the programs, ending in Customize...; pick is called with the
// one chosen.
QMenu *decw_menu (QWidget *parent, std::function<void (const DecwApp &)> pick);

}   // namespace gui

#endif
