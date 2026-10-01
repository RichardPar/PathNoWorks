// filewindow.cc -- one node's files.

#include "filewindow.h"

#include "common.h"
#include "decwindows.h"

#include "pnw/names.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>

namespace gui {

namespace {

// Session control's "access control rejected".
constexpr unsigned ACCESS_REJECTED = 34;

// Largest file shown in the viewer.
constexpr qsizetype VIEW_LIMIT = 4 * 1024 * 1024;

// DAP operating system types whose FALs use Unix names.
bool unix_os (unsigned ostype) { return ostype == 0 || ostype == 18 || ostype == 19; }

// A directory listing that came back in Unix form: names end in "/" for
// directories and have no versions.
bool looks_unix (const std::vector<pnw::DirEntry> &entries)
{
    for (const pnw::DirEntry &e : entries) {
        if (!e.name.empty () && e.name.back () == '/') return true;
        if (e.name.find (';') != std::string::npos) return false;
    }
    return false;
}

std::string join_local (const QStringList &cur)
{
    return "/" + ss (cur.join ('/'));
}

QString size_text (const pnw::DirEntry &e)
{
    auto b = e.blocks ();
    return b ? QString::number (*b) : QString ();
}

}   // namespace

// ----------------------------------------------------------------- FileList

FileList::FileList (QWidget *parent) : QTreeWidget (parent)
{
    setAcceptDrops (true);
    setDragEnabled (true);
    setDragDropMode (QAbstractItemView::DragDrop);
    setDefaultDropAction (Qt::CopyAction);
    setDropIndicatorShown (true);
}

void FileList::dragEnterEvent (QDragEnterEvent *e)
{
    // Our own files dragged back onto us would only be copied over
    // themselves.
    if (e->source () != this && e->mimeData ()->hasUrls ()) {
        e->setDropAction (Qt::CopyAction);
        e->accept ();
    }
}

void FileList::dragMoveEvent (QDragMoveEvent *e)
{
    if (e->source () != this && e->mimeData ()->hasUrls ()) {
        e->setDropAction (Qt::CopyAction);
        e->accept ();
    }
}

void FileList::dropEvent (QDropEvent *e)
{
    if (e->source () == this) return;
    QStringList paths;
    for (const QUrl &u : e->mimeData ()->urls ())
        if (u.isLocalFile () && QFileInfo (u.toLocalFile ()).isFile ())
            paths << u.toLocalFile ();
    if (paths.isEmpty ()) return;
    // Onto a directory row: into that directory.
    QString into;
    if (QTreeWidgetItem *it = itemAt (e->position ().toPoint ());
        it && it->data (0, Qt::UserRole + 1).toBool ())
        into = it->data (0, Qt::UserRole).toString ();
    e->setDropAction (Qt::CopyAction);
    e->accept ();
    emit files_dropped (paths, into);
}

void FileList::startDrag (Qt::DropActions)
{
    // The window fetches the files and starts the drag itself.
    emit drag_wanted ();
}

// --------------------------------------------------------------- FileWindow

FileWindow::FileWindow (const QString &node, const Login &login,
                        QWidget *parent)
    : QMainWindow (parent), node_ (node.toUpper ()), login_ (login)
{
    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (node_ + ":: - PathNoWorks");
    resize (820, 560);
    last_dir_ = QStandardPaths::writableLocation (QStandardPaths::DownloadLocation);

    auto *tb = addToolBar ("Files");
    tb->setMovable (false);
    tb->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    up_ = tb->addAction (QIcon::fromTheme ("go-up"), "Up", this, &FileWindow::up);
    up_->setShortcut (QKeySequence ("Alt+Up"));
    refresh_ = tb->addAction (QIcon::fromTheme ("view-refresh"), "Refresh",
                              this, &FileWindow::refresh);
    refresh_->setShortcut (QKeySequence::Refresh);
    path_ = new QLineEdit;
    path_->setMinimumWidth (260);
    path_->setSizePolicy (QSizePolicy::Expanding, QSizePolicy::Fixed);
    path_->setPlaceholderText ("[] for the login directory, DUA0:[USER], pub/ ...");
    path_->setToolTip ("The directory: type one and press Enter");
    tb->addWidget (path_);
    connect (path_, &QLineEdit::returnPressed, this, [this] { go_to (path_->text ()); });

    // The actions on a row of their own, under the path.
    addToolBarBreak ();
    auto *ops = addToolBar ("Actions");
    ops->setMovable (false);
    ops->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    download_ = ops->addAction (QIcon::fromTheme ("document-save"), "Copy here...",
                                this, &FileWindow::ask_download);
    upload_ = ops->addAction (QIcon::fromTheme ("document-open"), "Copy there...",
                              this, &FileWindow::ask_upload);
    rename_ = ops->addAction (QIcon::fromTheme ("edit-rename"), "Rename...",
                              this, &FileWindow::ask_rename);
    rename_->setShortcut (QKeySequence ("F2"));
    delete_ = ops->addAction (QIcon::fromTheme ("edit-delete"), "Delete",
                              this, &FileWindow::ask_remove);
    delete_->setShortcut (QKeySequence::Delete);
    ops->addSeparator ();
    mount_ = ops->addAction (QIcon::fromTheme ("drive-harddisk"), "Mount",
                             this, &FileWindow::toggle_mount);
    terminal_ = ops->addAction (QIcon::fromTheme ("utilities-terminal"), "Terminal",
                                this, [this] {
        QString err;
        if (!open_sethost (node_, &err))
            QMessageBox::warning (this, "Terminal", err);
    });
    QMenu *decw = decw_menu (this, [this] (const DecwApp &a) {
        status_->setText ("Starting " + a.label + " on " + node_ + "...");
        decw_launch (this, node_, login_, a, [this] (bool ok, const QString &msg) {
            status_->setText (ok ? msg.toHtmlEscaped ()
                                 : "<span style='color:#c0392b'>" + msg.toHtmlEscaped () + "</span>");
            if (!ok) QMessageBox::warning (this, "DECwindows", msg);
        });
    });
    decw_ = ops->addAction (decw->icon (), "DECwindows");
    decw_->setMenu (decw);
    decw_->setToolTip ("Run one of " + node_ + "'s DECwindows programs, on this screen");
    if (auto *b = qobject_cast<QToolButton *> (ops->widgetForAction (decw_)))
        b->setPopupMode (QToolButton::InstantPopup);

    list_ = new FileList;
    list_->setColumnCount (5);
    list_->setHeaderLabels ({ "Name", "Blocks", "Date", "Owner", "Protection" });
    list_->setRootIsDecorated (false);
    list_->setSelectionMode (QAbstractItemView::ExtendedSelection);
    list_->setSortingEnabled (true);
    list_->sortByColumn (0, Qt::AscendingOrder);
    list_->header ()->setStretchLastSection (true);
    list_->header ()->setSectionResizeMode (QHeaderView::Interactive);
    setCentralWidget (list_);
    connect (list_, &QTreeWidget::itemActivated, this, [this] (QTreeWidgetItem *it) {
        QString name = it->data (0, Qt::UserRole).toString ();
        if (it->data (0, Qt::UserRole + 1).toBool ()) open_dir (name);
        else view (name);
    });
    connect (list_, &QTreeWidget::itemSelectionChanged, this, &FileWindow::update_actions);
    connect (list_, &FileList::files_dropped, this, &FileWindow::upload);
    connect (list_, &FileList::drag_wanted, this, &FileWindow::start_drag);

    status_ = new QLabel;
    statusBar ()->addWidget (status_, 1);

    update_actions ();
    // Look once the window is up, so a login prompt has a parent to sit on.
    QMetaObject::invokeMethod (this, [this] { go_to ({}); }, Qt::QueuedConnection);
}

QString FileWindow::location () const { return node_ + "::" + dir_spec (); }

QStringList FileWindow::names () const
{
    QStringList out;
    for (int i = 0; i < list_->topLevelItemCount (); ++i) {
        auto *it = list_->topLevelItem (i);
        QString n = it->data (0, Qt::UserRole).toString ();
        out << (it->data (0, Qt::UserRole + 1).toBool () ? n + "/" : n);
    }
    return out;
}

pnw::RemoteSpec FileWindow::spec () const
{
    pnw::RemoteSpec s;
    s.node = ss (node_);
    s.user = ss (login_.user);
    s.password = ss (login_.password);
    s.account = ss (login_.account);
    return s;
}

std::string FileWindow::listing_spec () const
{
    return pnw::RemoteTree (ss (base_)).listing (join_local (cur_));
}

std::string FileWindow::file_spec (const QString &name, const QString &subdir) const
{
    QStringList p = cur_;
    if (!subdir.isEmpty ()) p << subdir;
    p << name;
    return pnw::RemoteTree (ss (base_)).spec (join_local (p));
}

QString FileWindow::dir_spec () const
{
    std::string l = listing_spec ();
    for (const char *wild : { "*.*;0", "*" }) {
        std::string w = wild;
        if (l.size () >= w.size () && l.compare (l.size () - w.size (), w.size (), w) == 0) {
            l.erase (l.size () - w.size ());
            break;
        }
    }
    // The login directory, on a VMS node.
    if (l.empty () && vms_) return "[]";
    return qs (l);
}

void FileWindow::set_busy (bool on, const QString &what)
{
    busy_ = on;
    if (on) {
        status_->setText (what + "...");
        QApplication::setOverrideCursor (Qt::BusyCursor);
    } else {
        QApplication::restoreOverrideCursor ();
    }
    update_actions ();
}

void FileWindow::update_actions ()
{
    bool files = !selected (true).isEmpty ();
    for (QAction *a : { up_, refresh_, upload_, mount_ }) a->setEnabled (!busy_);
    download_->setEnabled (!busy_ && files);
    delete_->setEnabled (!busy_ && files);
    rename_->setEnabled (!busy_ && selected (true).size () == 1);
    mount_->setText (mounted () ? "Unmount" : "Mount");
    // DECwindows is VMS's; a Unix FAL has none.
    decw_->setEnabled (!busy_ && (vms_ || !base_known_));
    path_->setEnabled (!busy_);
}

QStringList FileWindow::selected (bool files_only) const
{
    QStringList out;
    for (QTreeWidgetItem *it : list_->selectedItems ()) {
        if (files_only && it->data (0, Qt::UserRole + 1).toBool ()) continue;
        out << it->data (0, Qt::UserRole).toString ();
    }
    return out;
}

// ---------------------------------------------------------------- running

void FileWindow::run (const QString &what, Op op, bool relist)
{
    if (busy_) return;
    set_busy (true, what);

    pnw::RemoteSpec spec = this->spec ();
    bool proxy = login_.proxy;
    std::string socket = ss (api_socket ());
    std::string base = ss (base_);
    bool known = base_known_;
    std::string local = join_local (cur_);

    in_background (this, [=] () -> Reply {
        Reply r;
        pnw::Api api (socket);
        if (op) {
            pnw::DapSession s (api, spec, proxy);
            op (api, s, r);
        }
        if (!relist) return r;
        if (known) {
            pnw::DapSession s (api, spec, proxy);
            r.entries = s.directory (pnw::RemoteTree (base).listing (local));
            r.listed = true;
            return r;
        }
        // The first look: VMS or RSX names, or Unix ones?  The FAL's
        // operating system says which to try first, and whether the other
        // works decides.  dnfal says VMS, for VMS's sake, and lists Unix
        // names.
        pnw::DapSession first (api, spec, proxy);
        bool vms_first = !unix_os (first.remote_config ().ostype);
        std::vector<std::string> tries;
        if (vms_first) tries = { "[]", "" };
        else           tries = { "", "[]" };
        std::string error;
        for (std::size_t i = 0; i < tries.size (); ++i) {
            try {
                std::unique_ptr<pnw::DapSession> s;
                pnw::DapSession *use = &first;
                if (i > 0) { s = std::make_unique<pnw::DapSession> (api, spec, proxy); use = s.get (); }
                auto entries = use->directory (pnw::RemoteTree (tries[i]).listing ("/"));
                bool unix_names = looks_unix (entries);
                if (tries[i] == "[]" && unix_names) {
                    tries.push_back ("");
                    continue;
                }
                r.entries = std::move (entries);
                r.base = tries[i];
                r.vms = tries[i] == "[]";
                r.listed = true;
                return r;
            } catch (const pnw::DapError &e) {
                // A session that failed may not take another request, so
                // the next try has its own.
                if (error.empty ()) error = e.what ();
            }
        }
        throw pnw::ApiError (error.empty () ? "cannot list the directory" : error);
    }, [this, what, op, relist] (Outcome<Reply> o) {
        set_busy (false);
        if (!o.ok ()) {
            if (o.reject == ACCESS_REJECTED) {
                LoginDialog d (node_, o.error + ". " + node_
                               + " wants a user name and password.", login_, this);
                if (d.exec () == QDialog::Accepted) {
                    login_ = d.login ();
                    run (what, op, relist);
                    return;
                }
            }
            status_->setText ("<span style='color:#c0392b'>" + o.error.toHtmlEscaped ()
                              + "</span>");
            emit finished (false, o.error);
            return;
        }
        Reply &r = *o.value;
        if (!base_known_ && r.listed) {
            base_ = qs (r.base);
            vms_ = r.vms;
            base_known_ = true;
        }
        if (r.listed) show_listing (r);
        if (!r.content_name.isEmpty ()) {
            // A file to show.
            QByteArray &c = r.content;
            bool binary = c.left (4096).contains ('\0');
            auto *dlg = new QDialog (this);
            dlg->setAttribute (Qt::WA_DeleteOnClose);
            dlg->setWindowTitle (node_ + "::" + r.content_name);
            dlg->resize (760, 560);
            auto *v = new QVBoxLayout (dlg);
            auto *text = new QPlainTextEdit;
            text->setReadOnly (true);
            text->setFont (QFontDatabase::systemFont (QFontDatabase::FixedFont));
            if (binary)
                text->setPlainText (QString ("%1 bytes of binary data. Use Copy here "
                                             "to save it.").arg (c.size ()));
            else
                text->setPlainText (QString::fromLatin1 (c));
            v->addWidget (text);
            dlg->show ();
        }
        QString msg = qs (r.message);
        if (msg.isEmpty () && r.listed)
            msg = QString ("%1: %2 items").arg (location ()).arg (list_->topLevelItemCount ());
        status_->setText (msg.toHtmlEscaped ());
        update_actions ();
        emit finished (true, msg);
    });
}

void FileWindow::show_listing (const Reply &r)
{
    list_->setSortingEnabled (false);
    list_->clear ();
    path_->setText (dir_spec ());
    setWindowTitle (location () + " - PathNoWorks");
    for (const pnw::DirEntry &e : r.entries) {
        auto ln = pnw::local_name (e.name, vms_);
        if (!ln) continue;
        auto *it = new QTreeWidgetItem;
        QString name = qs (ln->name);
        it->setText (0, ln->directory ? name + "/" : name);
        it->setIcon (0, QIcon::fromTheme (ln->directory ? "folder" : "text-x-generic"));
        it->setData (0, Qt::UserRole, name);
        it->setData (0, Qt::UserRole + 1, ln->directory);
        if (!ln->directory) {
            it->setText (1, size_text (e));
            it->setTextAlignment (1, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (e.dates)
            it->setText (2, qs (!e.dates->rdt.empty () ? e.dates->rdt : e.dates->cdt));
        if (e.protection) {
            std::string own = e.protection->owner;
            if (!own.empty () && own[0] != '[') own = "[" + own + "]";
            it->setText (3, qs (own));
            it->setText (4, qs (e.protection->vms ()));
        }
        list_->addTopLevelItem (it);
    }
    list_->setSortingEnabled (true);
    for (int c = 0; c < 4; ++c) list_->resizeColumnToContents (c);
    list_->setColumnWidth (0, std::max (list_->columnWidth (0) + 24, 220));
    up_->setEnabled (!cur_.isEmpty () || vms_ || base_.contains ('/'));
}

// --------------------------------------------------------------- browsing

void FileWindow::go_to (const QString &base)
{
    QString b = base.trimmed ();
    if (b.isEmpty ()) {
        // The login directory.  The first time, which syntax it has is
        // found by looking.
        base_ = base_known_ && vms_ ? "[]" : "";
    } else {
        base_ = b;
        base_known_ = true;
        vms_ = pnw::RemoteTree (ss (b)).vms ();
    }
    cur_.clear ();
    refresh ();
}

void FileWindow::open_dir (const QString &name)
{
    cur_ << name;
    refresh ();
}

void FileWindow::up ()
{
    if (!cur_.isEmpty ()) {
        cur_.removeLast ();
    } else if (vms_) {
        // [A.B] -> [A]; [A] -> [000000]; [] -> [-]; [-] -> [-.-].
        QString d = dir_spec ();
        int lb = d.indexOf ('['), rb = d.lastIndexOf (']');
        if (lb < 0 || rb < lb) return;
        QString dev = d.left (lb);
        QString inside = d.mid (lb + 1, rb - lb - 1);
        bool only_up = !inside.isEmpty ()
            && QString (inside).remove ('-').remove ('.').isEmpty ();
        if (inside.isEmpty ())             inside = "-";
        else if (only_up)                  inside += ".-";
        else if (inside.contains ('.'))    inside = inside.left (inside.lastIndexOf ('.'));
        else if (inside != "000000")       inside = "000000";
        else                               return;
        // "[.SUB]" up is the login directory.
        base_ = dev + "[" + inside + "]";
    } else {
        QString b = base_;
        if (b.endsWith ('/')) b.chop (1);
        int slash = b.lastIndexOf ('/');
        if (b.isEmpty ()) return;
        base_ = slash < 0 ? QString () : b.left (slash + 1);
    }
    refresh ();
}

void FileWindow::refresh ()
{
    run ("Listing " + node_ + "::", {}, true);
}

void FileWindow::view (const QString &name)
{
    std::string path = file_spec (name);
    run ("Reading " + name, [path, name] (pnw::Api &, pnw::DapSession &s, Reply &r) {
        r.content_name = name;
        s.get (path, pnw::Transfer::automatic, [&r] (decnet::ByteView b) {
            if (r.content.size () < VIEW_LIMIT)
                r.content.append (reinterpret_cast<const char *> (b.data ()),
                                  static_cast<qsizetype> (b.size ()));
        });
        if (r.content.size () >= VIEW_LIMIT)
            r.message = "Showing the first 4 MB of " + ss (name);
    }, false);
}

// ------------------------------------------------------------- operations

void FileWindow::download (const QStringList &names, const QString &local_dir)
{
    std::vector<std::pair<std::string, std::string>> files;
    for (const QString &n : names)
        files.emplace_back (file_spec (n), ss (QDir (local_dir).filePath (n)));
    last_dir_ = local_dir;
    QString where = local_dir;
    run (QString ("Copying %1 file(s)").arg (files.size ()),
         [files, where] (pnw::Api &, pnw::DapSession &s, Reply &r) {
        std::uint64_t total = 0;
        for (const auto &[remote, local] : files) {
            std::string tmp = local + ".pnw-partial";
            std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw pnw::ApiError ("cannot create " + tmp + ": " + std::strerror (errno));
            try {
                s.get (remote, pnw::Transfer::automatic, [&] (decnet::ByteView b) {
                    out.write (reinterpret_cast<const char *> (b.data ()),
                               static_cast<std::streamsize> (b.size ()));
                    total += b.size ();
                });
                out.close ();
                if (!out) throw pnw::ApiError ("error writing " + tmp);
            } catch (...) {
                std::remove (tmp.c_str ());
                throw;
            }
            if (std::rename (tmp.c_str (), local.c_str ()) != 0)
                throw pnw::ApiError ("cannot rename to " + local + ": " + std::strerror (errno));
        }
        r.message = "Copied " + std::to_string (files.size ()) + " file(s), "
                  + std::to_string (total) + " bytes, to " + ss (where);
    }, false);
}

void FileWindow::upload (const QStringList &local_paths, const QString &into)
{
    std::vector<std::pair<std::string, std::string>> files;
    for (const QString &p : local_paths)
        files.emplace_back (ss (p), file_spec (QFileInfo (p).fileName (), into));
    run (QString ("Copying %1 file(s) to %2").arg (files.size ())
             .arg (into.isEmpty () ? node_ : node_ + " " + into + "/"),
         [files] (pnw::Api &, pnw::DapSession &s, Reply &r) {
        std::uint64_t total = 0;
        for (const auto &[local, remote] : files) {
            std::ifstream in (local, std::ios::binary);
            if (!in) throw pnw::ApiError ("cannot read " + local + ": " + std::strerror (errno));
            char sample[4096];
            in.read (sample, sizeof sample);
            bool text = pnw::looks_like_text (decnet::ByteView (
                reinterpret_cast<const std::uint8_t *> (sample),
                static_cast<std::size_t> (in.gcount ())));
            in.clear ();
            in.seekg (0, std::ios::end);
            auto size = static_cast<std::uint64_t> (in.tellg ());
            in.seekg (0);
            s.put (remote, text, [&] {
                decnet::Bytes b (8192);
                in.read (reinterpret_cast<char *> (b.data ()),
                         static_cast<std::streamsize> (b.size ()));
                b.resize (static_cast<std::size_t> (in.gcount ()));
                total += b.size ();
                return b;
            }, size);
            if (in.bad ()) throw pnw::ApiError ("error reading " + local);
        }
        r.message = "Copied " + std::to_string (files.size ()) + " file(s), "
                  + std::to_string (total) + " bytes";
    });
}

void FileWindow::remove (const QStringList &names)
{
    std::vector<std::string> specs;
    for (const QString &n : names) specs.push_back (file_spec (n));
    run (QString ("Deleting %1 file(s)").arg (specs.size ()),
         [specs] (pnw::Api &, pnw::DapSession &s, Reply &r) {
        for (const std::string &p : specs) s.erase (p);
        r.message = "Deleted " + std::to_string (specs.size ()) + " file(s)";
    });
}

void FileWindow::rename_file (const QString &from, const QString &to)
{
    std::string a = file_spec (from), b = file_spec (to);
    run ("Renaming " + from, [a, b] (pnw::Api &, pnw::DapSession &s, Reply &r) {
        s.rename (a, b);
        r.message = "Renamed " + a + " to " + b;
    });
}

// -------------------------------------------------------------- the asking

void FileWindow::ask_download ()
{
    QStringList names = selected (true);
    if (names.isEmpty ()) return;
    QString dir = QFileDialog::getExistingDirectory (this, "Copy to", last_dir_);
    if (!dir.isEmpty ()) download (names, dir);
}

void FileWindow::ask_upload ()
{
    QStringList files = QFileDialog::getOpenFileNames (this, "Copy to " + location (),
                                                       QDir::homePath ());
    if (!files.isEmpty ()) upload (files);
}

void FileWindow::ask_remove ()
{
    QStringList names = selected (true);
    if (names.isEmpty ()) return;
    QString what = names.size () == 1 ? names[0] : QString ("%1 files").arg (names.size ());
    if (QMessageBox::question (this, "Delete", "Delete " + what + " from " + location () + "?")
        == QMessageBox::Yes)
        remove (names);
}

void FileWindow::ask_rename ()
{
    QStringList names = selected (true);
    if (names.size () != 1) return;
    bool ok = false;
    QString to = QInputDialog::getText (this, "Rename", "New name for " + names[0] + ":",
                                        QLineEdit::Normal, names[0], &ok).trimmed ();
    if (ok && !to.isEmpty () && to != names[0]) rename_file (names[0], to);
}

// ----------------------------------------------------------------- dragging

QList<QUrl> FileWindow::fetch_for_drag (const QStringList &names)
{
    if (busy_ || names.isEmpty ()) return {};
    auto dir = std::make_unique<QTemporaryDir> (
        QDir::temp ().filePath ("pathnoworks-drag-XXXXXX"));
    if (!dir->isValid ()) return {};

    // Wait for the copy, with the window still drawing itself, and a
    // progress dialog if it takes more than a moment.
    QEventLoop loop;
    bool ok = false;
    auto c = connect (this, &FileWindow::finished, &loop, [&] (bool good, const QString &) {
        ok = good;
        loop.quit ();
    });
    QProgressDialog progress (QString ("Fetching %1 file(s) from %2...")
                                  .arg (names.size ()).arg (node_),
                              QString (), 0, 0, this);
    progress.setWindowModality (Qt::WindowModal);
    progress.setMinimumDuration (400);
    progress.setValue (0);
    QString keep = last_dir_;
    download (names, dir->path ());
    last_dir_ = keep;
    if (busy_) loop.exec ();
    disconnect (c);
    if (!ok) return {};

    QList<QUrl> urls;
    for (const QString &n : names) urls << QUrl::fromLocalFile (dir->filePath (n));
    drag_dirs_.push_back (std::move (dir));
    return urls;
}

void FileWindow::start_drag ()
{
    QStringList names = selected (true);
    QList<QUrl> urls = fetch_for_drag (names);
    if (urls.isEmpty ()) return;
    auto *mime = new QMimeData;
    mime->setUrls (urls);
    auto *drag = new QDrag (list_);
    drag->setMimeData (mime);
    drag->setPixmap (QIcon::fromTheme ("text-x-generic").pixmap (32));
    drag->exec (Qt::CopyAction);
    status_->setText (QString ("Dragged %1 file(s) out").arg (names.size ()));
}

// ----------------------------------------------------------------- mounting

QString FileWindow::mount_point () const
{
    return QDir::home ().filePath ("DECnet/" + node_);
}

bool FileWindow::mounted () const
{
    QFile f ("/proc/self/mounts");
    if (!f.open (QIODevice::ReadOnly)) return false;
    QString mp = mount_point ();
    for (const QByteArray &line : f.readAll ().split ('\n')) {
        QList<QByteArray> parts = line.split (' ');
        if (parts.size () > 1 && QString::fromUtf8 (parts[1]) == mp) return true;
    }
    return false;
}

void FileWindow::toggle_mount ()
{
    QString mp = mount_point ();
    if (mounted ()) {
        int rc = QProcess::execute ("fusermount3", { "-u", mp });
        status_->setText (rc == 0 ? "Unmounted " + mp : "Could not unmount " + mp
                                    + ": is something using it?");
        update_actions ();
        return;
    }
    QString tool = find_tool ("pnw-fs");
    if (tool.isEmpty ()) {
        QMessageBox::warning (this, "Mount", "pnw-fs is not built: it needs libfuse3-dev.");
        return;
    }
    QDir ().mkpath (mp);
    QString spec = node_;
    if (!login_.user.isEmpty ()) {
        spec += "\"" + login_.user;
        if (!login_.password.isEmpty ()) spec += " " + login_.password;
        spec += "\"";
    }
    spec += "::" + (dir_spec () == "[]" ? QString () : dir_spec ());
    QStringList args { "-s", api_socket (), "--rw" };
    if (login_.proxy && login_.user.isEmpty ()) args << "--proxy";
    args << spec << mp;
    QProcess p;
    p.start (tool, args);
    p.waitForFinished (30000);
    if (p.exitStatus () != QProcess::NormalExit || p.exitCode () != 0) {
        QMessageBox::warning (this, "Mount", "pnw-fs could not mount " + location () + ":\n"
                              + QString::fromLocal8Bit (p.readAllStandardError ()));
        return;
    }
    status_->setText ("Mounted " + location () + " on " + mp);
    update_actions ();
    QDesktopServices::openUrl (QUrl::fromLocalFile (mp));
}

}   // namespace gui
