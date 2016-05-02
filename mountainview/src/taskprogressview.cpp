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

class TaskProgressViewDelegate : public QStyledItemDelegate {
public:
	TaskProgressViewDelegate(QObject *parent = 0) : QStyledItemDelegate(parent) {}
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
		QSize sh = QStyledItemDelegate::sizeHint(option, index);
		sh.setHeight(sh.height()*2);
		return sh;
	}
	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
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

class TaskProgressViewPrivate {
public:
    TaskProgressView* q;
    TaskProgressAgent* m_agent;
    QTreeWidget* m_tree;

    QString shortened(QString txt, int maxlen);
};

TaskProgressView::TaskProgressView()
{
    d = new TaskProgressViewPrivate;
    d->q = this;
    d->m_agent = TaskProgressAgent::globalInstance();
	setItemDelegate(new TaskProgressViewDelegate(this));

	header()->hide();
	setRootIsDecorated(false);
    connect(d->m_agent, SIGNAL(tasksChanged()), this, SLOT(slot_refresh()));
}

TaskProgressView::~TaskProgressView()
{
    delete d;
}

void TaskProgressView::slot_refresh()
{
    TaskProgressAgent* A = d->m_agent;

	clear();
    QStringList labels;
    labels << "Task";
	setHeaderLabels(labels);

    QList<TaskInfo> tasks1 = A->activeTasks();
    QList<TaskInfo> tasks2 = A->completedTasks();
    QList<TaskInfo> tasks;
    tasks.append(tasks1);
    tasks.append(tasks2);
    for (int i=0; i<tasks.count(); i++) {
        TaskInfo info=tasks[i];
		info.description = "";
        QTreeWidgetItem* it = new QTreeWidgetItem;
		it->setData(0, Qt::UserRole, info.progress);
		it->setData(0, Qt::UserRole+1, info.start_time);
		it->setData(0, Qt::UserRole+2, info.end_time);
        QString txt;
		QColor col;
        if (i<tasks1.count()) { //active
			//txt = QString("%1 (%2%) %3").arg(info.label).arg((int)(info.progress * 100)).arg(info.description);
			txt = info.label;
			col = QColor("blue");
        }
        else {
//            txt = QString("%1 (%2 sec) %3").arg(info.label).arg(info.start_time.msecsTo(info.end_time) / 1000.0).arg(info.description);
			txt = info.label;
			col = QColor();
        }
		if (col.isValid())
			it->setForeground(0, col);
		it->setText(0, txt);
        it->setToolTip(0, txt);
		addTopLevelItem(it);
    }
	for (int i = 0; i < columnCount(); i++) {
		resizeColumnToContents(i);
    }
}

QString TaskProgressViewPrivate::shortened(QString txt, int maxlen)
{
    if (txt.count() > maxlen) {
        return txt.mid(0, maxlen - 3) + "...";
    }
    else
        return txt;
}
