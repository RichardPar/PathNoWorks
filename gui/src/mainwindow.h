// mainwindow.h -- the network: the nodes decnetd knows, and what to do
// with one.

#ifndef PNW_GUI_MAINWINDOW_H
#define PNW_GUI_MAINWINDOW_H

#include <QMainWindow>

class QCheckBox;
class QLabel;
class QLineEdit;
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

public slots:
    void refresh ();
    void open_files (const QString &node);
    void open_terminal_to (const QString &node);
    void write_mail (const QString &to = {});

signals:
    void refreshed (bool ok, const QString &message);

private:
    QString current_node () const;
    void ask_node (const QString &what, void (MainWindow::*act) (const QString &));
    void settings ();

    QStandardItemModel *model_;
    NodeFilter         *filter_;
    QTableView         *table_;
    QLineEdit          *search_;
    QCheckBox          *reachable_;
    QLabel             *status_;
    QString             system_;
    bool                busy_ = false;
};

}   // namespace gui

#endif
