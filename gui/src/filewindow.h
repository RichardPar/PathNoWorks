// filewindow.h -- one node's files: browse, copy both ways, delete,
// rename, mount.
//
// Everything that talks to the node runs in the background with its own
// DAP session; the window stays responsive and does one thing at a time.
// A node that wants a login asks for one, and the operation runs again.

#ifndef PNW_GUI_FILEWINDOW_H
#define PNW_GUI_FILEWINDOW_H

#include "logindialog.h"

#include "pnw/dap.h"

#include <QMainWindow>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUrl>

#include <functional>
#include <memory>

class QAction;
class QLabel;
class QLineEdit;

namespace gui {

// The listing.  Files dropped on it go to the directory shown, or into a
// directory row they are dropped on; files dragged out of it are fetched
// first, since the far end wants local files.
class FileList : public QTreeWidget {
    Q_OBJECT
public:
    explicit FileList (QWidget *parent = nullptr);
signals:
    // into: a subdirectory of the one shown, or empty.
    void files_dropped (const QStringList &paths, const QString &into);
    void drag_wanted ();
protected:
    void dragEnterEvent (QDragEnterEvent *e) override;
    void dragMoveEvent (QDragMoveEvent *e) override;
    void dropEvent (QDropEvent *e) override;
    void startDrag (Qt::DropActions actions) override;
};

class FileWindow : public QMainWindow {
    Q_OBJECT
public:
    FileWindow (const QString &node, const Login &login = {},
                QWidget *parent = nullptr);

    // Where we are, as VMS would write it: NODE::[.SUB].
    QString location () const;
    // The names listed, directories with a trailing "/".
    QStringList names () const;
    bool busy () const noexcept { return busy_; }

    // Fetch files into a temporary directory, waiting, and return them as
    // URLs for a drag.  Empty if they could not be fetched.  The copies
    // last as long as the window.
    QList<QUrl> fetch_for_drag (const QStringList &names);

public slots:
    // Start over at a directory: "[USER]", "DUA0:[USER]", "pub/", or
    // empty for the login directory.
    void go_to (const QString &base);
    void open_dir (const QString &name);
    void up ();
    void refresh ();
    void download (const QStringList &names, const QString &local_dir);
    // Into the directory shown, or into a subdirectory of it.
    void upload (const QStringList &local_paths, const QString &into = {});
    void remove (const QStringList &names);
    void rename_file (const QString &from, const QString &to);
    void view (const QString &name);

signals:
    // An operation has finished, and the listing is up to date if it
    // could be.  message is what the status bar shows.
    void finished (bool ok, const QString &message);

private:
    // What a background operation hands back.
    struct Reply {
        std::string               message;
        std::string               base;     // set when the base was chosen
        bool                      vms = true;
        bool                      listed = false;
        std::vector<pnw::DirEntry> entries;
        QByteArray                content;  // a file read for viewing
        QString                   content_name;
    };
    using Op = std::function<void (pnw::Api &, pnw::DapSession &, Reply &)>;

    // Run op, then list the current directory unless relist is false.
    void run (const QString &what, Op op, bool relist = true);
    void show_listing (const Reply &r);
    void set_busy (bool on, const QString &what = {});

    pnw::RemoteSpec spec () const;
    std::string listing_spec () const;
    std::string file_spec (const QString &name, const QString &subdir = {}) const;
    QString dir_spec () const;          // the current directory, no wildcard

    QStringList selected (bool files_only) const;
    void ask_download ();
    void ask_upload ();
    void ask_remove ();
    void ask_rename ();
    void toggle_mount ();
    void start_drag ();
    QString mount_point () const;
    bool mounted () const;
    void update_actions ();

    QString     node_;
    Login       login_;
    QString     base_;                  // as typed: "[]", "pub/", ...
    bool        base_known_ = false;    // chosen, or found by looking
    bool        vms_ = true;
    QStringList cur_;                   // subdirectories under the base
    bool        busy_ = false;

    FileList  *list_;
    QLineEdit *path_;
    QLabel    *status_;
    QAction   *up_, *refresh_, *download_, *upload_, *delete_, *rename_,
              *mount_, *terminal_, *decw_;
    QString    last_dir_;               // where downloads went last
    std::vector<std::unique_ptr<QTemporaryDir>> drag_dirs_;
};

}   // namespace gui

#endif
