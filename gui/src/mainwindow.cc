// mainwindow.cc -- the network.

#include "mainwindow.h"

#include "common.h"
#include "decwindows.h"
#include "logindialog.h"
#include "filewindow.h"
#include "maildialog.h"

#include "pnw/nodes.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <map>

namespace gui {

namespace {

enum Column { c_name, c_address, c_state, c_hops, c_cost, c_circuit, c_count };

constexpr int SORT_ROLE = Qt::UserRole;
constexpr int REACHABLE_ROLE = Qt::UserRole + 1;
constexpr int FAV_ROLE = Qt::UserRole + 2;

QIcon star_icon ()
{
    return QIcon::fromTheme ("starred", QIcon::fromTheme ("emblem-favorite"));
}

// "29.157" as a number that sorts: area * 1024 + node.
int address_key (const QString &a)
{
    int dot = a.indexOf ('.');
    if (dot < 0) return a.toInt ();
    return a.left (dot).toInt () * 1024 + a.mid (dot + 1).toInt ();
}

}   // namespace

// Text from the search box in the name or address; only reachable nodes,
// or only favourites, if asked.  Favourites always pass the reachable test,
// and always sort first.
class NodeFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void set (const QString &text, bool reachable_only, bool favourites_only)
    {
        text_ = text.trimmed ();
        reachable_only_ = reachable_only;
        favourites_only_ = favourites_only;
        invalidate ();
    }

protected:
    bool lessThan (const QModelIndex &a, const QModelIndex &b) const override
    {
        bool fa = a.siblingAtColumn (c_name).data (FAV_ROLE).toBool ();
        bool fb = b.siblingAtColumn (c_name).data (FAV_ROLE).toBool ();
        // Qt reverses the comparison for a descending sort; undo that for
        // favourites, so they stay on top either way.
        if (fa != fb) return sortOrder () == Qt::AscendingOrder ? fa : fb;
        return QSortFilterProxyModel::lessThan (a, b);
    }

    bool filterAcceptsRow (int row, const QModelIndex &parent) const override
    {
        auto *m = sourceModel ();
        QModelIndex name = m->index (row, c_name, parent);
        bool fav = name.data (FAV_ROLE).toBool ();
        if (favourites_only_ && !fav) return false;
        if (reachable_only_ && !fav && !name.data (REACHABLE_ROLE).toBool ()) return false;
        if (text_.isEmpty ()) return true;
        return name.data ().toString ().contains (text_, Qt::CaseInsensitive)
            || m->index (row, c_address, parent).data ().toString ().startsWith (text_);
    }

private:
    QString text_;
    bool    reachable_only_ = true;
    bool    favourites_only_ = false;
};

MainWindow::MainWindow (QWidget *parent) : QMainWindow (parent)
{
    setWindowTitle ("PathNoWorks");
    resize (760, 560);

    model_ = new QStandardItemModel (0, c_count, this);
    model_->setHorizontalHeaderLabels ({ "Node", "Address", "State", "Hops", "Cost", "Circuit" });
    filter_ = new NodeFilter (this);
    filter_->setSourceModel (model_);
    filter_->setSortRole (SORT_ROLE);

    auto *tb = addToolBar ("Network");
    tb->setMovable (false);
    tb->setToolButtonStyle (Qt::ToolButtonTextBesideIcon);
    QAction *refresh = tb->addAction (QIcon::fromTheme ("view-refresh"), "Refresh",
                                      this, &MainWindow::refresh);
    refresh->setShortcut (QKeySequence::Refresh);
    tb->addSeparator ();
    QAction *files = tb->addAction (QIcon::fromTheme ("folder-remote"), "Files", this, [this] {
        ask_node ("Files on node:", &MainWindow::open_files);
    });
    files->setShortcut (QKeySequence ("Ctrl+O"));
    QAction *term = tb->addAction (QIcon::fromTheme ("utilities-terminal"), "Terminal", this, [this] {
        ask_node ("Log in to node:", &MainWindow::open_terminal_to);
    });
    term->setShortcut (QKeySequence ("Ctrl+T"));
    QAction *mail = tb->addAction (QIcon::fromTheme ("mail-message-new"), "Mail", this, [this] {
        QString n = current_node ();
        write_mail (n.isEmpty () ? QString () : n + "::");
    });
    mail->setShortcut (QKeySequence ("Ctrl+M"));

    auto *fileMenu = menuBar ()->addMenu ("&PathNoWorks");
    fileMenu->addAction ("&Settings...", this, &MainWindow::settings);
    fileMenu->addSeparator ();
    fileMenu->addAction ("&Quit", QKeySequence::Quit, qApp, &QApplication::quit);
    auto *nodeMenu = menuBar ()->addMenu ("&Node");
    nodeMenu->addAction (files);
    nodeMenu->addAction (term);
    nodeMenu->addAction (mail);
    QAction *fav = new QAction (star_icon (), "Favourite", this);
    fav->setShortcut (QKeySequence ("Ctrl+D"));
    fav->setToolTip ("Add the selected node to the favourites, or take it off");
    connect (fav, &QAction::triggered, this, [this] {
        QString n = current_node ();
        if (!n.isEmpty ()) toggle_favourite (n);
    });
    QMenu *decw = decw_menu (this, [this] (const DecwApp &a) {
        QString n = current_node ();
        if (n.isEmpty ()) {
            QMessageBox::information (this, "DECwindows", "Pick a node first.");
            return;
        }
        // The last login used on this node, for this session, to start from.
        LoginDialog d (n, "A login on " + n + " to run " + a.label
                          + " as. It needs DECwindows installed there.",
                       logins_.value (n.toUpper ()), this);
        if (d.exec () != QDialog::Accepted) return;
        logins_[n.toUpper ()] = d.login ();
        status_->setText ("Starting " + a.label + " on " + n + "...");
        decw_launch (this, n, d.login (), a, [this] (bool ok, const QString &msg) {
            status_->setText (msg.toHtmlEscaped ());
            if (!ok) QMessageBox::warning (this, "DECwindows", msg);
        });
    });
    nodeMenu->addMenu (decw);

    // And on the toolbar, next to Mail: a button that drops the list down.
    QAction *decwTool = tb->addAction (decw->icon (), "DECwindows");
    decwTool->setMenu (decw);
    decwTool->setToolTip ("Run one of the selected node's DECwindows programs, on this screen");
    if (auto *b = qobject_cast<QToolButton *> (tb->widgetForAction (decwTool)))
        b->setPopupMode (QToolButton::InstantPopup);
    nodeMenu->addAction (fav);
    nodeMenu->addSeparator ();
    nodeMenu->addAction (refresh);
    fav_menu_ = menuBar ()->addMenu ("F&avourites");
    auto *helpMenu = menuBar ()->addMenu ("&Help");
    helpMenu->addAction ("&About PathNoWorks", this, [this] {
        QMessageBox::about (this, "PathNoWorks",
            "<b>PathNoWorks</b><p>Pathworks-style DECnet for Linux: files, "
            "terminals and mail on VMS, RSX and HECnet nodes, through cppdecnet's "
            "decnetd.</p>");
    });

    auto *central = new QWidget;
    auto *v = new QVBoxLayout (central);
    auto *row = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText ("Find a node by name or address");
    search_->setClearButtonEnabled (true);
    reachable_ = new QCheckBox ("Reachable only");
    reachable_->setChecked (true);
    favs_only_ = new QCheckBox ("Favourites only");
    row->addWidget (search_, 1);
    row->addWidget (reachable_);
    row->addWidget (favs_only_);
    v->addLayout (row);
    table_ = new QTableView;
    table_->setModel (filter_);
    table_->setSortingEnabled (true);
    table_->sortByColumn (c_address, Qt::AscendingOrder);
    table_->setSelectionBehavior (QAbstractItemView::SelectRows);
    table_->setSelectionMode (QAbstractItemView::SingleSelection);
    table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
    table_->verticalHeader ()->hide ();
    table_->horizontalHeader ()->setStretchLastSection (true);
    table_->setContextMenuPolicy (Qt::ActionsContextMenu);
    auto *sep = new QAction (this);
    sep->setSeparator (true);
    table_->addActions ({ files, term, mail, decw->menuAction (), sep, fav });
    v->addWidget (table_, 1);
    setCentralWidget (central);

    auto update = [this] {
        filter_->set (search_->text (), reachable_->isChecked (), favs_only_->isChecked ());
    };
    connect (search_, &QLineEdit::textChanged, this, update);
    connect (reachable_, &QCheckBox::toggled, this, update);
    connect (favs_only_, &QCheckBox::toggled, this, update);

    favourites_ = QSettings ().value ("nodes/favourites").toStringList ();
    show_favourites ();
    connect (table_, &QTableView::activated, this, [this] {
        QString n = current_node ();
        if (!n.isEmpty ()) open_files (n);
    });

    status_ = new QLabel;
    statusBar ()->addWidget (status_, 1);

    QMetaObject::invokeMethod (this, &MainWindow::refresh, Qt::QueuedConnection);
}

int MainWindow::visible_nodes () const { return filter_->rowCount (); }

QString MainWindow::current_node () const
{
    QModelIndex i = table_->currentIndex ();
    if (!i.isValid ()) return {};
    QString name = filter_->index (i.row (), c_name).data ().toString ();
    return name.isEmpty () ? filter_->index (i.row (), c_address).data ().toString () : name;
}

void MainWindow::ask_node (const QString &what, void (MainWindow::*act) (const QString &))
{
    QString n = current_node ();
    bool ok = false;
    n = QInputDialog::getText (this, "PathNoWorks", what, QLineEdit::Normal, n, &ok).trimmed ();
    if (ok && !n.isEmpty ()) (this->*act) (n);
}

void MainWindow::refresh ()
{
    if (busy_) return;
    busy_ = true;
    QString socket = api_socket ();
    status_->setText ("Asking decnetd at " + socket + "...");
    struct Known {
        std::string system, via;
        std::vector<pnw::NodeRow> rows;
    };
    in_background (this, [socket] {
        pnw::Api api (ss (socket));
        Known k { api.system (), {}, pnw::known_nodes (api) };
        // An endnode knows one thing about reachability: its router.  Ask
        // the router instead, as TELL router SHOW KNOWN NODES would.
        const pnw::NodeRow *router = nullptr;
        int others = 0;
        for (const pnw::NodeRow &r : k.rows) {
            if (r.executor || !r.reachable ()) continue;
            ++others;
            if (!r.circuit.empty ()) router = &r;
        }
        if (others == 1 && router) {
            std::string via = router->name.empty () ? router->address : router->name;
            try {
                auto rows = pnw::known_nodes (api, via);
                // Names the router lacks, from our own list.
                std::map<std::string, std::string> names;
                for (const pnw::NodeRow &r : k.rows)
                    if (!r.name.empty ()) names[r.address] = r.name;
                for (pnw::NodeRow &r : rows) {
                    if (r.name.empty () && names.count (r.address))
                        r.name = names[r.address];
                    // The router itself is reachable: it is how we got here.
                    if (r.executor) r.state = "reachable, our router";
                    r.executor = r.name == k.system;
                    // The router's circuits are not ours.
                    r.circuit.clear ();
                }
                k.rows = std::move (rows);
                k.via = via;
            } catch (const std::exception &) {
                // Keep our own view.
            }
        }
        return k;
    }, [this, socket] (Outcome<Known> o) {
        busy_ = false;
        if (!o.ok ()) {
            status_->setText ("<span style='color:#c0392b'>" + o.error.toHtmlEscaped ()
                              + "</span>");
            emit refreshed (false, o.error);
            return;
        }
        system_ = qs (o.value->system);
        const auto &rows = o.value->rows;
        QString via = qs (o.value->via);
        model_->removeRows (0, model_->rowCount ());
        int reachable = 0;
        for (const pnw::NodeRow &r : rows) {
            QList<QStandardItem *> items;
            for (int c = 0; c < c_count; ++c) items << new QStandardItem;
            items[c_name]->setText (qs (r.name));
            items[c_name]->setData (qs (r.name), SORT_ROLE);
            items[c_name]->setData (r.reachable (), REACHABLE_ROLE);
            if (r.executor) {
                QFont f = items[c_name]->font ();
                f.setBold (true);
                items[c_name]->setFont (f);
                items[c_name]->setToolTip ("This node: the decnetd PathNoWorks uses");
            }
            items[c_name]->setIcon (QIcon::fromTheme (r.reachable () ? "network-server"
                                                                     : "network-offline"));
            QString a = qs (r.address);
            items[c_address]->setText (a);
            items[c_address]->setData (address_key (a), SORT_ROLE);
            items[c_state]->setText (r.executor ? "this node" : qs (r.state));
            items[c_state]->setData (items[c_state]->text (), SORT_ROLE);
            if (r.hops) { items[c_hops]->setText (QString::number (*r.hops)); items[c_hops]->setData (*r.hops, SORT_ROLE); }
            if (r.cost) { items[c_cost]->setText (QString::number (*r.cost)); items[c_cost]->setData (*r.cost, SORT_ROLE); }
            items[c_circuit]->setText (qs (r.circuit));
            items[c_circuit]->setData (qs (r.circuit), SORT_ROLE);
            model_->appendRow (items);
            if (r.reachable ()) ++reachable;
        }
        add_missing_favourites ();
        show_favourites ();
        // A router's circuits are not ours to show.
        table_->setColumnHidden (c_circuit, !via.isEmpty ());
        table_->resizeColumnsToContents ();
        QString msg = QString ("%1 through decnetd at %2: %3 nodes known, %4 reachable")
                          .arg (system_, socket).arg (rows.size ()).arg (reachable);
        if (!via.isEmpty ()) msg += ", as router " + via + " sees them";
        status_->setText (msg.toHtmlEscaped ());
        setWindowTitle ("PathNoWorks - " + system_);
        emit refreshed (true, msg);
    });
}

// ------------------------------------------------------------- favourites

bool MainWindow::is_favourite (const QString &node) const
{
    return favourites_.contains (node.trimmed ().toUpper ());
}

void MainWindow::toggle_favourite (const QString &node)
{
    QString n = node.trimmed ().toUpper ();
    if (n.isEmpty ()) return;
    if (!favourites_.removeAll (n)) favourites_ << n;
    favourites_.sort ();
    QSettings ().setValue ("nodes/favourites", favourites_);
    add_missing_favourites ();
    show_favourites ();
    status_->setText (is_favourite (n) ? n + " is a favourite" : n + " is no longer a favourite");
}

// A favourite the node list does not have -- a name decnetd does not know,
// or a node not in the router's view -- still gets a row.
void MainWindow::add_missing_favourites ()
{
    for (const QString &f : favourites_) {
        bool found = false;
        for (int r = 0; r < model_->rowCount () && !found; ++r)
            found = model_->item (r, c_name)->text ().toUpper () == f
                 || model_->item (r, c_address)->text () == f;
        if (found) continue;
        QList<QStandardItem *> items;
        for (int c = 0; c < c_count; ++c) items << new QStandardItem;
        bool address = !f.isEmpty () && f[0].isDigit ();
        items[address ? c_address : c_name]->setText (f);
        items[c_name]->setData (address ? QString () : f, SORT_ROLE);
        items[c_address]->setData (address ? address_key (f) : 0, SORT_ROLE);
        items[c_state]->setText ("not in the node list");
        items[c_state]->setData (items[c_state]->text (), SORT_ROLE);
        model_->appendRow (items);
    }
}

void MainWindow::show_favourites ()
{
    for (int r = 0; r < model_->rowCount (); ++r) {
        QStandardItem *name = model_->item (r, c_name);
        QString key = name->text ().isEmpty () ? model_->item (r, c_address)->text ()
                                               : name->text ();
        bool fav = is_favourite (key) || is_favourite (model_->item (r, c_address)->text ());
        name->setData (fav, FAV_ROLE);
        if (fav) name->setIcon (star_icon ());
        else name->setIcon (QIcon::fromTheme (name->data (REACHABLE_ROLE).toBool ()
                                              ? "network-server" : "network-offline"));
    }
    filter_->invalidate ();

    // The menu: each favourite opens its files; Ctrl+D and Add for the rest.
    fav_menu_->clear ();
    for (const QString &f : favourites_) {
        QMenu *m = fav_menu_->addMenu (star_icon (), f);
        m->addAction (QIcon::fromTheme ("folder-remote"), "Files", this, [this, f] { open_files (f); });
        m->addAction (QIcon::fromTheme ("utilities-terminal"), "Terminal", this,
                      [this, f] { open_terminal_to (f); });
        m->addAction (QIcon::fromTheme ("mail-message-new"), "Mail", this,
                      [this, f] { write_mail (f + "::"); });
        m->addSeparator ();
        m->addAction ("Remove from favourites", this, [this, f] { toggle_favourite (f); });
    }
    if (!favourites_.isEmpty ()) fav_menu_->addSeparator ();
    fav_menu_->addAction ("Add a node...", this, &MainWindow::ask_favourite);
}

void MainWindow::ask_favourite ()
{
    bool ok = false;
    QString n = QInputDialog::getText (this, "Favourites", "Node name or address:",
                                       QLineEdit::Normal, current_node (), &ok).trimmed ();
    if (ok && !n.isEmpty () && !is_favourite (n)) toggle_favourite (n);
}

// ------------------------------------------------------------- actions

void MainWindow::open_files (const QString &node)
{
    auto *w = new FileWindow (node);
    w->show ();
}

void MainWindow::open_terminal_to (const QString &node)
{
    QString err;
    if (!open_sethost (node, &err)) QMessageBox::warning (this, "Terminal", err);
}

void MainWindow::write_mail (const QString &to)
{
    auto *d = new MailDialog (to, this);
    d->setAttribute (Qt::WA_DeleteOnClose);
    connect (d, &MailDialog::sent, this, [this] (bool ok, const QString &report) {
        if (ok) status_->setText (report.toHtmlEscaped ().replace ('\n', "; "));
    });
    d->show ();
}

void MainWindow::settings ()
{
    bool ok = false;
    QString s = QInputDialog::getText (this, "Settings",
        "decnetd's API socket (empty for $DECNETAPI or /tmp/decnetapi.sock):",
        QLineEdit::Normal, api_socket (), &ok).trimmed ();
    if (!ok) return;
    set_api_socket (s);
    refresh ();
}

}   // namespace gui
