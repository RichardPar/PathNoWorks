// mainwindow.h -- the network: the nodes decnetd knows, and what to do
// with one.

#ifndef PNW_GUI_MAINWINDOW_H
#define PNW_GUI_MAINWINDOW_H

#include <QMainWindow>

class QCheckBox;
class QLabel;
class QLineEdit;
class QMenu;
class QSortFilterProxyModel;
class QStandardItemModel;
class QTableView;

namespace gui {

class NodeFilter;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow (QWidget *parent = nullptr);

    // Rows shown, after filtering.  For tests.
    int visible_nodes () const;
    QString system () const { return system_; }

    // Favourite nodes, by name (or address, for a node with no name).
    // Kept in the settings; shown first, and whether reachable or not.
    QStringList favourites () const { return favourites_; }
    bool is_favourite (const QString &node) const;

public slots:
    void refresh ();
    void open_files (const QString &node);
    void open_terminal_to (const QString &node);
    void write_mail (const QString &to = {});
    void toggle_favourite (const QString &node);

signals:
    void refreshed (bool ok, const QString &message);

private:
    QString current_node () const;
    void ask_node (const QString &what, void (MainWindow::*act) (const QString &));
    void settings ();
    void show_favourites ();            // marks in the table, and the menu
    void add_missing_favourites ();
    void ask_favourite ();

    QStandardItemModel *model_;
    NodeFilter         *filter_;
    QTableView         *table_;
    QLineEdit          *search_;
    QCheckBox          *reachable_;
    QCheckBox          *favs_only_;
    QMenu              *fav_menu_;
    QStringList         favourites_;
    QLabel             *status_;
    QString             system_;
    bool                busy_ = false;
};

}   // namespace gui

#endif
