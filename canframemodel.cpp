#include "canframemodel.h"

#include <QFile>
#include "utility.h"

int CANFrameModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return filteredFrames.count();
}

int CANFrameModel::totalFrameCount()
{
    return frames.count();
}

int CANFrameModel::columnCount(const QModelIndex &index) const
{
    Q_UNUSED(index);
    return 7;
}

CANFrameModel::CANFrameModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    frames.reserve(10000000); //yes, I'm preallocating 10 million entries in this list. I don't think anyone will exceed this.
    filteredFrames.reserve(10000000);
    dbcHandler = NULL;
    interpretFrames = false;
    overwriteDups = false;
    useHexMode = true;
    timeSeconds = false;
    timeOffset = 0;
    needFilterRefresh = false;
    lastUpdateNumFrames = 0;
}

void CANFrameModel::setHexMode(bool mode)
{
    if (useHexMode != mode)
    {
        this->beginResetModel();
        useHexMode = mode;
        Utility::decimalMode = !useHexMode;
        this->endResetModel();
    }
}

void CANFrameModel::setSecondsMode(bool mode)
{
    if (timeSeconds != mode)
    {
        this->beginResetModel();
        timeSeconds = mode;
        this->endResetModel();
    }
}

void CANFrameModel::setDBCHandler(DBCHandler *handler)
{
    dbcHandler = handler;
}

void CANFrameModel::setInterpetMode(bool mode)
{
    //if the state of interpretFrames changes then we need to reset the model
    //so that QT will refresh the view properly
    if (interpretFrames != mode)
    {
        this->beginResetModel();
        interpretFrames = mode;
        this->endResetModel();
    }
}

void CANFrameModel::normalizeTiming()
{
    if (frames.count() == 0) return;    
    timeOffset = frames[0].timestamp;
    for (int i = 0; i < frames.count(); i++)
    {
        frames[i].timestamp -= timeOffset;
    }
    this->beginResetModel();
    for (int i = 0; i < filteredFrames.count(); i++)
    {
        filteredFrames[i].timestamp -= timeOffset;
    }
    this->endResetModel();
}

void CANFrameModel::setOverwriteMode(bool mode)
{
    overwriteDups = mode;
}

void CANFrameModel::setFilterState(int ID, bool state)
{
    if (!filters.contains(ID)) return;
    filters[ID] = state;
    sendRefresh();
}

void CANFrameModel::setAllFilters(bool state)
{
    QMap<int, bool>::iterator it;
    for (it = filters.begin(); it != filters.end(); ++it)
    {
        it.value() = state;
    }
    sendRefresh();
}

void CANFrameModel::recalcOverwrite()
{
    if (!overwriteDups) return; //no need to do a thing if mode is disabled

    int lastUnique = 0;
    bool found;

    beginResetModel();
    for (int i = 1; i < frames.count(); i++)
    {
        found = false;
        for (int j = 0; j <= lastUnique; j++)
        {
            if (frames[i].ID == frames[j].ID)
            {
                frames.replace(j, frames[i]);
                found = true;
                break;
            }
        }
        if (!found)
        {
            lastUnique++;
            frames.replace(lastUnique, frames[i]);
        }
    }

    while (frames.count() > lastUnique) frames.removeLast();

    filteredFrames.clear();

    for (int i = 0; i < frames.count(); i++)
    {
        if (filters[frames[i].ID])
        {
            filteredFrames.append(frames[i]);
        }
    }

    endResetModel();
}

QVariant CANFrameModel::data(const QModelIndex &index, int role) const
{
    QString tempString;
    if (!index.isValid())
        return QVariant();

    if (index.row() >= (filteredFrames.count()))
        return QVariant();

    if (role == Qt::DisplayRole) {
        CANFrame thisFrame = filteredFrames.at(index.row());
        switch (index.column())
        {
        case 0: //timestamp
            if (!timeSeconds) return QString::number(thisFrame.timestamp);
            else return QString::number((double)thisFrame.timestamp / 1000000.0, 'f', 6);
            break;
        case 1: //id            
            return Utility::formatNumber(thisFrame.ID);
            break;
        case 2: //ext
            return QString::number(thisFrame.extended);
            break;
        case 3: //direction
            if (thisFrame.isReceived) return QString(tr("Rx"));
            else return QString(tr("Tx"));
            break;
        case 4: //bus
            return QString::number(thisFrame.bus);
            break;
        case 5: //len
            return QString::number(thisFrame.len);
            break;
        case 6: //data
            for (int i = 0; i < thisFrame.len; i++)
            {
                tempString.append(Utility::formatNumber(thisFrame.data[i]));
                tempString.append(" ");
            }
            //now, if we're supposed to interpret the data and the DBC handler is loaded then use it
            if (dbcHandler != NULL && interpretFrames)
            {
                DBC_MESSAGE *msg = dbcHandler->findMessage(thisFrame);
                if (msg != NULL)
                {
                    tempString.append("\r\n");
                    tempString.append(msg->name + " " + msg->comment + "\r\n");
                    for (int j = 0; j < msg->sigHandler->getCount(); j++)
                    {
                        QString sigString;
                        if (msg->sigHandler->findSignalByIdx(j)->processAsText(thisFrame, sigString))
                        {
                            tempString.append(sigString);
                            tempString.append("\r\n");
                        }
                    }
                }
            }
            return tempString;
            break;
        default:
            return QVariant();
        }
    }
    else
        return QVariant();
}

QVariant CANFrameModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal)
    {
        switch (section)
        {
        case 0:
            return QString(tr("Timestamp"));
            break;
        case 1:
            return QString(tr("ID"));
            break;
        case 2:
            return QString(tr("Ext"));
            break;
        case 3:
            return QString(tr("Dir"));
            break;
        case 4:
            return QString(tr("Bus"));
            break;
        case 5:
            return QString(tr("Len"));
            break;
        case 6:
            return QString(tr("Data"));
            break;
        }
    }

    else
        return QString::number(section + 1);

    return QVariant();
}

void CANFrameModel::addFrame(const CANFrame &frame, bool autoRefresh = false)
{
    mutex.lock();
    CANFrame tempFrame;
    tempFrame = frame;
    tempFrame.timestamp -= timeOffset;

    //if this ID isn't found in the filters list then add it and show it by default
    if (!filters.contains(tempFrame.ID))
    {
        filters.insert(tempFrame.ID, true);
        needFilterRefresh = true;
    }

    if (!overwriteDups)
    {        
        frames.append(tempFrame);
        if (filters[tempFrame.ID])
        {
            if (autoRefresh) beginInsertRows(QModelIndex(), filteredFrames.count() + 1, filteredFrames.count() + 1);
            filteredFrames.append(tempFrame);
            if (autoRefresh) endInsertRows();
        }
    }
    else
    {
        bool found = false;
        for (int i = 0; i < frames.count(); i++)
        {
            if (frames[i].ID == tempFrame.ID)
            {                
                frames.replace(i, tempFrame);
                found = true;
                break;
            }
        }
        if (!found)
        {            
            frames.append(tempFrame);
            if (filters[tempFrame.ID])
            {
                if (autoRefresh) beginInsertRows(QModelIndex(), filteredFrames.count() + 1, filteredFrames.count() + 1);
                filteredFrames.append(tempFrame);
                if (autoRefresh) endInsertRows();
            }
        }
        else
        {
            for (int j = 0; j < filteredFrames.count(); j++)
            {
                if (filteredFrames[j].ID == tempFrame.ID)
                {
                    if (autoRefresh) beginResetModel();
                    filteredFrames.replace(j, tempFrame);
                    if (autoRefresh) endResetModel();
                }
            }
        }
    }

    mutex.unlock();
}

void CANFrameModel::sendRefresh()
{
    mutex.lock();
    qDebug() << "Sending mass refresh";
    QVector<CANFrame> tempContainer;
    beginResetModel();
    int count = frames.count();
    for (int i = 0; i < count; i++)
    {
        if (filters[frames[i].ID])
        {
            tempContainer.append(frames[i]);
        }
    }
    filteredFrames.clear();
    filteredFrames = tempContainer;
    lastUpdateNumFrames = filteredFrames.count();
    endResetModel();
    mutex.unlock();
}

void CANFrameModel::sendRefresh(int pos)
{
    beginInsertRows(QModelIndex(), pos, pos);
    endInsertRows();
}

//issue a refresh for the last num entries in the model.
//used by the serial worker to do batch updates so it doesn't
//have to send thousands of messages per second
void CANFrameModel::sendBulkRefresh(int num)
{
    //yes, num was sent to us by the serial worker but we actually have a better idea
    //of how many by tracking the number we last knew about as opposed to how many rows there
    //are now.
    num = filteredFrames.count() - lastUpdateNumFrames;
    lastUpdateNumFrames += num; //done this way to avoid asking for filteredFrames.count() again

    //qDebug() << "Bulk refresh of " << num;

    if (num == 0) return;
    if (filteredFrames.count() == 0) return;

    mutex.lock();

    if (!overwriteDups)
    {        
        if (num > filteredFrames.count()) num = filteredFrames.count();
        beginInsertRows(QModelIndex(), filteredFrames.count() - num, filteredFrames.count() - 1);
        endInsertRows();
    }
    else
    {
        sendRefresh();
    }
    mutex.unlock();
}

void CANFrameModel::clearFrames()
{
    mutex.lock();
    this->beginResetModel();
    frames.clear();
    filteredFrames.clear();
    filters.clear();
    this->endResetModel();
    lastUpdateNumFrames = 0;
    mutex.unlock();

    emit updatedFiltersList();
}

/*
 * Since the getListReference function returns readonly
 * you can't insert frames with it. Instead this function
 * allows for a mass import of frames into the model
 */
void CANFrameModel::insertFrames(const QVector<CANFrame> &newFrames)
{    
    //not resetting the model here because the serial worker automatically does a bulk refresh every 1/4 second
    //and that refresh will cause the view to update. If you do both it usually ends up thinking you have
    //double the number of frames.
    //beginResetModel();
    mutex.lock();
    int insertedFiltered = 0;
    for (int i = 0; i < newFrames.count(); i++)
    {
        frames.append(newFrames[i]);
        if (!filters.contains(newFrames[i].ID))
        {
            filters.insert(newFrames[i].ID, true);
            needFilterRefresh = true;
        }
        if (filters[newFrames[i].ID])
        {
            insertedFiltered++;
            filteredFrames.append(newFrames[i]);
        }
    }
    mutex.unlock();
    //endResetModel();
    //beginInsertRows(QModelIndex(), filteredFrames.count() + 1, filteredFrames.count() + insertedFiltered);
    //endInsertRows();
    if (needFilterRefresh) emit updatedFiltersList();
}

int CANFrameModel::getIndexFromTimeID(int ID, double timestamp)
{
    int bestIndex = -1;
    uint64_t intTimeStamp = timestamp * 1000000l;
    for (int i = 0; i < frames.count(); i++)
    {
        if ((frames[i].ID == ID))
        {
            if (frames[i].timestamp <= intTimeStamp) bestIndex = i;
            else break; //drop out of loop as soon as we pass the proper timestamp
        }
    }
    return bestIndex;
}

void CANFrameModel::loadFilterFile(QString filename)
{
    QFile *inFile = new QFile(filename);
    QByteArray line;
    int ID;

    if (!inFile->open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    filters.clear();

    while (!inFile->atEnd()) {
        line = inFile->readLine().simplified();
        if (line.length() > 2)
        {
            QList<QByteArray> tokens = line.split(',');
            ID = tokens[0].toInt(NULL, 16);
            if (tokens[1].toUpper() == "T") filters.insert(ID, true);
                else filters.insert(ID, false);
        }
    }
    inFile->close();

    sendRefresh();

    emit updatedFiltersList();
}

void CANFrameModel::saveFilterFile(QString filename)
{
    QFile *outFile = new QFile(filename);

    if (!outFile->open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QMap<int, bool>::const_iterator it;
    for (it = filters.begin(); it != filters.end(); ++it)
    {
        outFile->write(QString::number(it.key(), 16).toUtf8());
        outFile->putChar(',');
        if (it.value()) outFile->putChar('T');
            else outFile->putChar('F');
        outFile->write("\n");
    }
    outFile->close();
}

bool CANFrameModel::needsFilterRefresh()
{
    bool temp = needFilterRefresh;
    needFilterRefresh = false;
    return temp;
}

/*
 *This used to not be const correct but it is now. So, there's little harm in
 * allowing external code to peek at our frames. There's just no touching.
 * This ability to get a direct read-only reference speeds up a variety of
 * external code that needs to access frames directly and doesn't care about
 * this model's normal output mechanism.
 */
const QVector<CANFrame>* CANFrameModel::getListReference() const
{
    return &frames;
}

const QVector<CANFrame>* CANFrameModel::getFilteredListReference() const
{
    return &filteredFrames;
}

const QMap<int, bool>* CANFrameModel::getFiltersReference() const
{
    return &filters;
}
