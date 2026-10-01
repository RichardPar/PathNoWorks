// common.h -- what the PathNoWorks windows share.
//
// The API socket and tools, and a way to run a blocking DECnet operation
// off the GUI thread.  Every operation opens its own API connection:
// pnw::Api is not shared between threads, and connections are cheap.

#ifndef PNW_GUI_COMMON_H
#define PNW_GUI_COMMON_H

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QtConcurrent/QtConcurrentRun>

#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>

namespace gui {

// decnetd's API socket: the setting, else $DECNETAPI, else PyDECnet's
// default.
QString api_socket ();
void set_api_socket (const QString &path);
// For this run only, whatever the setting says.
void override_api_socket (const QString &path);

// One of the PathNoWorks tools: next to this program, in the build tree,
// or on PATH.  Empty if none is found.
QString find_tool (const QString &name);

// Open a terminal window running a command.  xterm as a VT340, so DEC
// graphics -- Sixel, ReGIS -- work, as pnw-sethost's guide describes.  On
// Windows: Windows Terminal if it is installed, else a console window.
bool open_terminal (const QString &title, const QStringList &command,
                    QString *error = nullptr);

// Run a console tool from here without a console window of its own, which
// Windows would otherwise open for it.  Nothing elsewhere.
void no_console_window (QProcess &p);

// Log in to node over CTERM in a terminal window.
bool open_sethost (const QString &node, QString *error = nullptr);

QString qs (const std::string &s);
std::string ss (const QString &s);

// The result of a background operation: its value, or why it failed.
template <typename T>
struct Outcome {
    std::optional<T> value;
    QString          error;
    unsigned         reject = 0;     // a session reject reason, if that
    bool ok () const { return value.has_value (); }
};

unsigned reject_reason (const std::exception &e);

// Run work on a pool thread; done gets the Outcome on ctx's thread.  If ctx
// goes first, done is not called.
template <typename Work, typename Done>
void in_background (QObject *ctx, Work work, Done done)
{
    using T = std::invoke_result_t<Work>;
    auto *watch = new QFutureWatcher<Outcome<T>> (ctx);
    QObject::connect (watch, &QFutureWatcherBase::finished, ctx,
                      [watch, done] () mutable {
                          done (watch->result ());
                          watch->deleteLater ();
                      });
    watch->setFuture (QtConcurrent::run ([work] () mutable -> Outcome<T> {
        try {
            return { work (), {}, 0 };
        } catch (const std::exception &e) {
            return { std::nullopt, QString::fromStdString (e.what ()),
                     reject_reason (e) };
        }
    }));
}

}   // namespace gui

#endif  // PNW_GUI_COMMON_H
