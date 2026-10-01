// mainwindow.cc -- the network.

#include "mainwindow.h"

#include "common.h"
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
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include <map>

namespace gui {

namespace {

enum Column { c_name, c_address, c_state, c_hops, c_cost, c_circuit, c_count };

constexpr int SORT_ROLE = Qt::UserRole;
constexpr int REACHABLE_ROLE = Qt::UserRole + 1;

// "29.157" as a number that sorts: area * 1024 + node.
int address_key (const QString &a)
{
    int dot = a.indexOf ('.');
    if (dot < 0) return a.toInt ();
    return a.left (dot).toInt () * 1024 + a.mid (dot + 1).toInt ();
}

}   // namespace

// Text from the search box in the name or address, and only reachable
// nodes if asked.
class NodeFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void set (const QString &text, bool reachable_only)
    {
        text_ = text.trimmed ();
        reachable_only_ = reachable_only;
        invalidateFilter ();
    }

protected:
    bool filterAcceptsRow (int row, const QModelIndex &parent) const override
    {
        auto *m = sourceModel ();
        QModelIndex name = m->index (row, c_name, parent);
        if (reachable_only_ && !name.data (REACHABLE_ROLE).toBool ()) return false;
        if (text_.isEmpty ()) return true;
        return name.data ().toString ().contains (text_, Qt::CaseInsensitive)
            || m->index (row, c_address, parent).data ().toString ().startsWith (text_);
    }

private:
    QString text_;
    bool    reachable_only_ = true;
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
    nodeMenu->addSeparator ();
    nodeMenu->addAction (refresh);
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
    row->addWidget (search_, 1);
    row->addWidget (reachable_);
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
    table_->addActions ({ files, term, mail });
    v->addWidget (table_, 1);
    setCentralWidget (central);

    auto update = [this] { filter_->set (search_->text (), reachable_->isChecked ()); };
    connect (search_, &QLineEdit::textChanged, this, update);
    connect (reachable_, &QCheckBox::toggled, this, update);
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
