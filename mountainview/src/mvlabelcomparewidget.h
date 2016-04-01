/******************************************************
** See the accompanying README and LICENSE files
** Author(s): Jeremy Magland
*******************************************************/

#ifndef MVLABELCOMPAREWIDGET_H
#define MVLABELCOMPAREWIDGET_H

#include <QWidget>
#include <QThread>
#include <QWheelEvent>
#include "diskarraymodel_new.h"
#include "mda.h"

/** \class MVLabelCompareWidget
 *  \brief Not used right now, but may be useful, probably needs to be rewritten
 */

class MVLabelCompareWidgetPrivate;

class MVLabelCompareWidget : public QWidget {
    Q_OBJECT
public:
    friend class MVLabelCompareWidgetPrivate;
    explicit MVLabelCompareWidget(QWidget* parent = 0);
    ~MVLabelCompareWidget();

    void setElectrodeLocations(const Mda& L);
    void setTemplates1(const Mda& X);
    void setTemplates2(const Mda& X);
    void setRaw(DiskArrayModel_New* X, bool own_it);
    void setTimesLabels(const Mda& times1, const Mda& labels1, const Mda& times2, const Mda& labels2);

    void updateWidgets();
signals:

protected:
    void resizeEvent(QResizeEvent* evt);

private slots:

private:
    MVLabelCompareWidgetPrivate* d;

signals:

public slots:
};

#endif // MVLABELCOMPAREWIDGET_H