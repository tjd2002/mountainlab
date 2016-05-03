/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
** Created: 4/30/2016
*******************************************************/

#include "taskprogressview.h"
#include "taskprogress.h"
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QtDebug>

class TaskProgressViewDelegate : public QStyledItemDelegate {
public:
	TaskProgressViewDelegate(QObject *parent = 0) : QStyledItemDelegate(parent) {}
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
		QSize sh = QStyledItemDelegate::sizeHint(option, index);
		sh.setHeight(sh.height()*2);
		return sh;
	}
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
        if (index.internalId() != 0xDEADBEEF) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
		QStyleOptionViewItem opt = option;
		opt.text = "";
		opt.displayAlignment = Qt::AlignTop|Qt::AlignLeft;
		QStyledItemDelegate::paint(painter, opt, index);
		qreal progress = index.data(Qt::UserRole).toDouble();
		if (progress < 1.0) {
			QStyleOptionProgressBar progOpt;
			progOpt.initFrom(opt.widget);
			progOpt.rect = option.rect;
			progOpt.minimum = 0;
			progOpt.maximum = 100;
			progOpt.progress = progress*100;
			progOpt.rect.setTop(progOpt.rect.center().y());
			progOpt.rect.adjust(44, 2, -4, -2);
//			progOpt.rect.setHeight(progOpt.rect.height()/2);
			if (option.widget) {
				option.widget->style()->drawControl(QStyle::CE_ProgressBar, &progOpt, painter, option.widget);
			}
			painter->save();
			QPen p = painter->pen();
			QFont f = painter->font();
			p.setColor((option.state & QStyle::State_Selected) ? Qt::white : Qt::darkGray);
			f.setPointSize(f.pointSize()-3);
			painter->setPen(p);
			painter->setFont(f);
			QRect r = option.rect;
			r.setTop(option.rect.center().y());
			r.adjust(4, 2, -4, -2);
			r.setRight(40);

			qreal duration = index.data(Qt::UserRole+1).toDateTime().msecsTo(QDateTime::currentDateTime()) / 1000.0;
			painter->drawText(r, Qt::AlignLeft|Qt::AlignVCenter, QString("%1s").arg(duration));
			painter->restore();
		} else {
			painter->save();
			QPen p = painter->pen();
			QFont f = painter->font();
			p.setColor((option.state & QStyle::State_Selected) ? Qt::white : Qt::darkGray);
			f.setPointSize(f.pointSize()-2);
			painter->setPen(p);
			painter->setFont(f);
			QRect r = option.rect;
			r.setTop(option.rect.center().y());
			r.adjust(4, 2, -4, -2);

			qreal duration = index.data(Qt::UserRole+1).toDateTime().msecsTo(index.data(Qt::UserRole+2).toDateTime()) / 1000.0;
			painter->drawText(r, Qt::AlignLeft|Qt::AlignVCenter, QString("Completed in %1s").arg(duration));
			painter->restore();
		}
	}
};

class TaskProgressModel : public QAbstractItemModel {
public:
    enum {
        ProgressRole = Qt::UserRole,
        StartTimeRole,
        EndTimeRole
    };
    TaskProgressModel(QObject* parent = 0)
        : QAbstractItemModel(parent)
    {
        m_agent = TaskProgressAgent::globalInstance();
        connect(m_agent, &TaskProgressAgent::tasksChanged, this, &TaskProgressModel::update);
        update();
    }

    QModelIndex index(int row, int column,
        const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid() && parent.internalId() != 0xDEADBEEF) return QModelIndex();
        if (parent.isValid()) {
            return createIndex(row, column, parent.row());
        }
        return createIndex(row, column, 0xDEADBEEF);
    }

    QModelIndex parent(const QModelIndex& child) const override {
        if (child.internalId() == 0xDEADBEEF)
            return QModelIndex();
        return createIndex(child.internalId(), 0, 0xDEADBEEF);
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid() && parent.internalId() != 0xDEADBEEF)
            return 0;
        if (parent.isValid()) {
            if (parent.row() < 0) return 0;
            return m_data.at(parent.row()).log_messages.size();
        }
        return m_data.size();
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 1; // 2
        return 1;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid())
            return QVariant();
        if (index.parent().isValid()) {
            const TaskInfo& task = m_data.at(index.internalId());
            const auto &logMessages = task.log_messages;
            auto logMessage = logMessages.at(index.row());
            switch (role) {
            case Qt::EditRole:
            case Qt::DisplayRole:
                return logMessage.message;
            case Qt::UserRole:
                return logMessage.time;
            default: return QVariant();
            }
        }
        const TaskInfo& task = m_data.at(index.row());
        switch (role) {
        case Qt::EditRole:
        case Qt::DisplayRole:
            return task.label;
        case Qt::ToolTipRole:
            return task.description;
        case Qt::ForegroundRole: {
            if (task.progress < 1)
                return QColor(Qt::blue);
            return QVariant();
        }
        case ProgressRole:
            return task.progress;
        case StartTimeRole:
            return task.start_time;
        case EndTimeRole:
            return task.end_time;
        }
        return QVariant();
    }

protected:
    void update()
    {
        beginResetModel();
        m_data.clear();
        m_data.append(m_agent->activeTasks());
        m_data.append(m_agent->completedTasks());
        endResetModel();
    }

private:
    QList<TaskInfo> m_data;
    TaskProgressAgent* m_agent;
};

class TaskProgressViewPrivate {
public:
    TaskProgressView* q;

    QString shortened(QString txt, int maxlen);
};

TaskProgressView::TaskProgressView()
{
    d = new TaskProgressViewPrivate;
    d->q = this;
	setItemDelegate(new TaskProgressViewDelegate(this));
    TaskProgressModel *model = new TaskProgressModel(this);
    setModel(model);
	header()->hide();
//	setRootIsDecorated(false);
}

TaskProgressView::~TaskProgressView()
{
    delete d;
}

QString TaskProgressViewPrivate::shortened(QString txt, int maxlen)
{
    if (txt.count() > maxlen) {
        return txt.mid(0, maxlen - 3) + "...";
    }
    else
        return txt;
}
