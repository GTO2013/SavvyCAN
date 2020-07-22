#ifndef DBCMAINEDITOR_H
#define DBCMAINEDITOR_H

#include <QDialog>
#include <QDebug>
#include <QIcon>
#include <QTreeWidget>
#include "dbchandler.h"
#include "dbcsignaleditor.h"
#include "dbcmessageeditor.h"
#include "dbcnodeeditor.h"
#include "utility.h"

namespace Ui {
class DBCMainEditor;
}

class DBCMainEditor : public QDialog
{
    Q_OBJECT

public:
    explicit DBCMainEditor(const QVector<CANFrame> *frames, QWidget *parent = 0);
    ~DBCMainEditor();
    void setFileIdx(int idx);

public slots:
    void updatedTreeInfo(QString oldData, QString newData, int type);

private slots:
    void onTreeDoubleClicked(const QModelIndex &index);
    void onCustomMenuTree(QPoint);
    void deleteCurrentTreeItem();
    void handleSearch();
    void handleSearchForward();
    void handleSearchBackward();

private:
    Ui::DBCMainEditor *ui;
    DBCHandler *dbcHandler;
    const QVector<CANFrame> *referenceFrames;
    DBCSignalEditor *sigEditor;
    DBCMessageEditor *msgEditor;
    DBCNodeEditor *nodeEditor;
    DBCFile *dbcFile;
    int fileIdx;
    QIcon nodeIcon;
    QIcon messageIcon;
    QIcon signalIcon;
    QIcon multiplexorSignalIcon;
    QIcon multiplexedSignalIcon;
    QList<QTreeWidgetItem *> searchItems;
    int searchItemPos;

    void showEvent(QShowEvent* event);
    void closeEvent(QCloseEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);
    void readSettings();
    void writeSettings();
    void refreshTree();
};

#endif // DBCMAINEDITOR_H
