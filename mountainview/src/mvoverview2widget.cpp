#include "mvoverview2widget.h"
#include "diskreadmda.h"
#include "sstimeseriesview.h"
#include "sstimeserieswidget.h"
#include "mvcrosscorrelogramswidget2.h"
#include "mvoverview2widgetcontrolpanel.h"
#ifdef USE_LAPACK
#include "get_pca_features.h"
#else
#include "get_principal_components.h"
#endif
#include "get_sort_indices.h"
#include "mvclusterdetailwidget.h"
#include "mvclipsview.h"
#include "mvclusterwidget.h"
#include "mvfiringrateview.h"
#include "extract_clips.h"
#include "tabber.h"
#include "computationthread.h"
#include "mountainsortthread.h"
#include "mvclipswidget.h"
#include "taskprogressview.h"
#include "mvcontrolpanel.h"
#include "taskprogress.h"

#include <QHBoxLayout>
#include <QMessageBox>
#include <QSplitter>
#include <QTime>
#include <QTimer>
#include <math.h>
#include <QProgressDialog>
#include "msmisc.h"
#include "mvutils.h"
#include <QColor>
#include <QStringList>
#include <QSet>
#include <QKeyEvent>

/// TODO important: splitter between control panel and task view
/// TODO show summary stats for dataset in mountainview
/// TODO look into find nearby events... needs explanation, testing etc

class CrossCorrelogramComputer : public ComputationThread {
public:
    //inputs
    DiskReadMda firings;
    int max_dt;

    void compute();

    Mda cross_correlograms_data;
};

class MVOverview2WidgetPrivate {
public:
    MVOverview2Widget* q;
    QMap<QString, QString> m_timeseries_paths;
    QString m_current_timeseries_name;
    DiskReadMda m_timeseries;
    DiskReadMda m_firings_original;
    Mda m_firings_split;
    DiskReadMda m_firings;
    QList<Epoch> m_epochs;
    QList<int> m_original_cluster_numbers;
    QList<int> m_original_cluster_offsets;
    int m_current_k;
    QSet<int> m_selected_ks;
    float m_samplerate;
    MVEvent m_current_event;
    QString m_mscmdserver_url;

    MVControlPanel* m_control_panel_new;
    TaskProgressView* m_task_progress_view;

    QSplitter* m_splitter1, *m_splitter2;
    TabberTabWidget* m_tabs1, *m_tabs2;
    Tabber* m_tabber;
    QProgressDialog* m_progress_dialog;

    Mda m_cross_correlograms_data;
    //Mda m_templates_data;

    QList<QColor> m_channel_colors;
    QMap<QString, QColor> m_colors;

    //CrossCorrelogramComputer m_cross_correlogram_computer;

    void create_cross_correlograms_data();
    //void create_templates_data();

    void update_sizes();
    //void update_templates();
    void update_all_widgets();
    void update_cluster_details();
    void update_clips();
    void update_cluster_views();
    void update_firing_event_views();
    void do_shell_split_and_event_filter();
    void add_tab(QWidget* W, QString label);

    MVCrossCorrelogramsWidget2* open_auto_correlograms();
    MVCrossCorrelogramsWidget2* open_cross_correlograms(int k);
    MVCrossCorrelogramsWidget2* open_matrix_of_cross_correlograms();
    //void open_templates();
    MVClusterDetailWidget* open_cluster_details();
    void open_timeseries();
    void open_clips();
    void open_clusters();
    void open_firing_events();
    void find_nearby_events();

    void update_cross_correlograms();
    void update_timeseries_views();
    void move_to_timepoint(double tp);
    void update_widget(QWidget* W);

    void set_cross_correlograms_current_index(int index);
    void set_cross_correlograms_selected_indices(const QList<int>& indices);
    void set_templates_current_number(int kk);
    void set_templates_selected_numbers(const QList<int>& kks);

    void set_times_labels_for_timeseries_widget(SSTimeSeriesWidget* WW);

    QList<QWidget*> get_all_widgets();
    TabberTabWidget* tab_widget_of(QWidget* W);

    void remove_widgets_of_type(QString widget_type);

    Mda compute_centroid(Mda& clips);
    Mda compute_geometric_median(Mda& clips, int num_iterations);
    void compute_geometric_median(int M, int N, double* output, double* input, int num_iterations);

    void set_progress(QString title, QString text, float frac);
    void set_current_event(MVEvent evt);

    long cc_max_dt();

    //void start_cross_correlograms_computer();
};

QColor brighten(QColor col, int amount)
{
    int r = col.red() + amount;
    int g = col.green() + amount;
    int b = col.blue() + amount;
    if (r > 255)
        r = 255;
    if (r < 0)
        r = 0;
    if (g > 255)
        g = 255;
    if (g < 0)
        g = 0;
    if (b > 255)
        b = 255;
    if (b < 0)
        b = 0;
    return QColor(r, g, b, col.alpha());
}

MVOverview2Widget::MVOverview2Widget(QWidget* parent)
    : QWidget(parent)
{
    d = new MVOverview2WidgetPrivate;
    d->q = this;

    d->m_current_k = 0;
    d->m_samplerate = 20000;

    d->m_progress_dialog = 0;
    d->m_current_event.time = -1;
    d->m_current_event.label = -1;

    d->m_control_panel_new = new MVControlPanel;
    connect(d->m_control_panel_new, SIGNAL(userAction(QString)), this, SLOT(slot_control_panel_user_action(QString)));

    QSplitter* splitter1 = new QSplitter;
    splitter1->setOrientation(Qt::Horizontal);
    d->m_splitter1 = splitter1;

    QSplitter* splitter2 = new QSplitter;
    splitter2->setOrientation(Qt::Vertical);
    d->m_splitter2 = splitter2;

    QScrollArea* CP = new QScrollArea;
    CP->setWidget(d->m_control_panel_new);
    CP->setWidgetResizable(true);

    d->m_task_progress_view = new TaskProgressView;
    d->m_task_progress_view->setFixedHeight(200);

    QWidget* left_widget = new QWidget;
    QVBoxLayout* left_layout = new QVBoxLayout;
    left_widget->setLayout(left_layout);
    left_layout->addWidget(CP);
    left_layout->addWidget(d->m_task_progress_view);

    splitter1->addWidget(left_widget);
    splitter1->addWidget(splitter2);

    d->m_tabber = new Tabber;
    d->m_tabs2 = d->m_tabber->createTabWidget("south");
    d->m_tabs1 = d->m_tabber->createTabWidget("north");

    splitter2->addWidget(d->m_tabs1);
    splitter2->addWidget(d->m_tabs2);

    QHBoxLayout* hlayout = new QHBoxLayout;
    hlayout->addWidget(splitter1);
    this->setLayout(hlayout);

    QStringList color_strings;
    color_strings
        << "#282828"
        << "#402020"
        << "#204020"
        << "#202070";
    for (int i = 0; i < color_strings.count(); i++)
        d->m_channel_colors << QColor(brighten(color_strings[i], 80));

    d->m_colors["background"] = QColor(240, 240, 240);
    d->m_colors["frame1"] = QColor(245, 245, 245);
    d->m_colors["info_text"] = QColor(80, 80, 80);
    d->m_colors["view_background"] = QColor(245, 245, 245);
    d->m_colors["view_background_highlighted"] = QColor(210, 230, 250);
    d->m_colors["view_background_selected"] = QColor(220, 240, 250);
    d->m_colors["view_background_hovered"] = QColor(240, 245, 240);

    d->m_colors["view_frame_selected"] = QColor(50, 20, 20);
    d->m_colors["divider_line"] = QColor(255, 100, 150);

    //connect(&d->m_cross_correlogram_computer,SIGNAL(computationFinished()),this,SLOT(slot_cross_correlogram_computer_finished()));
}

MVOverview2Widget::~MVOverview2Widget()
{
    delete d;
}

void MVOverview2Widget::addTimeseriesPath(const QString& name, const QString& path)
{
    d->m_timeseries_paths[name] = path;
    QStringList choices = d->m_timeseries_paths.keys();
    qSort(choices);
    d->m_control_panel_new->setTimeseriesChoices(choices);
    if (d->m_timeseries_paths.count() == 1) {
        this->setCurrentTimeseriesName(name);
    }
}

void MVOverview2Widget::setCurrentTimeseriesName(const QString& name)
{
    d->m_current_timeseries_name = name;
    d->m_timeseries.setPath(d->m_timeseries_paths[d->m_current_timeseries_name]);

    MVViewOptions opts = d->m_control_panel_new->viewOptions();
    opts.timeseries = name;
    d->m_control_panel_new->setViewOptions(opts);

    d->update_timeseries_views();
    d->update_cluster_details();
    d->update_clips();
    d->update_cluster_views();
    d->update_firing_event_views();
}

void MVOverview2Widget::setFiringsPath(const QString& firings)
{
    d->m_firings_original.setPath(firings);
    d->do_shell_split_and_event_filter();
    d->update_cross_correlograms();
    d->update_cluster_details();
    d->update_timeseries_views();
    //d->start_cross_correlograms_computer();
}

void MVOverview2Widget::setSampleRate(float freq)
{
    d->m_samplerate = freq;
    //d->start_cross_correlograms_computer();
}

void MVOverview2Widget::setDefaultInitialization()
{
    //d->open_templates();
    d->open_cluster_details();
    d->m_tabber->switchCurrentContainer();
    d->open_auto_correlograms();
}

void MVOverview2Widget::setEpochs(const QList<Epoch>& epochs)
{
    d->m_epochs = epochs;
}

/*
QImage MVOverview2Widget::generateImage(const QMap<QString, QVariant>& params)
{
    QString type0 = params.value("type").toString();
    if (type0 == "templates") {
        MVClusterDetailWidget* X = d->open_cluster_details();
        return X->renderImage();
    }
    else if (type0 == "auto_correlograms") {
        MVCrossCorrelogramsWidget2* X = d->open_auto_correlograms();
        return X->renderImage();
    }
    else if (type0 == "cross_correlograms") {
        int k = params.value("k", 0).toInt();
        MVCrossCorrelogramsWidget2* X = d->open_cross_correlograms(k);
        return X->renderImage();
    }
    else {
        qWarning() << "Unknown type in generateImage: " << type0;
        return QImage();
    }
}
*/

int MVOverview2Widget::getMaxLabel()
{
    int ret = 0;
    for (long i = 0; i < d->m_firings.N2(); i++) {
        int label = (int)d->m_firings.value(i);
        if (label > ret)
            ret = label;
    }
    return ret;
}

void MVOverview2Widget::setMscmdServerUrl(const QString& url)
{
    d->m_mscmdserver_url = url;
}

void MVOverview2Widget::resizeEvent(QResizeEvent* evt)
{
    Q_UNUSED(evt)
    d->update_sizes();
}

void MVOverview2Widget::keyPressEvent(QKeyEvent* evt)
{
    if ((evt->key() == Qt::Key_W) && (evt->modifiers() & Qt::ControlModifier)) {
        this->close();
    }
    else
        evt->ignore();
}

/*
void MVOverview2Widget::slot_control_panel_button_clicked(QString str)
{
    if (str == "update_cross_correlograms") {
        //d->start_cross_correlograms_computer();
        d->update_cross_correlograms();
    }
    else if (str == "update_templates") {
        //	d->update_templates();
        d->update_cluster_details();
        d->update_clips();
    }
    else if (str == "update_cluster_details") {
        d->update_cluster_details();
    }
    else if ((str == "update_shell_split") || (str == "use_shell_split")) {
        d->do_shell_split_and_event_filter();
        d->remove_widgets_of_type("cross_correlograms");
        d->remove_widgets_of_type("matrix_of_cross_correlograms");
        d->remove_widgets_of_type("clips");
        d->remove_widgets_of_type("find_nearby_events");
        d->remove_widgets_of_type("clusters");
        d->remove_widgets_of_type("firing_events");
        d->update_cluster_details();
        d->update_cross_correlograms();
    }
    else if ((str == "update_event_filter") || (str == "use_event_filter")) {
        d->do_shell_split_and_event_filter();
        //d->do_event_filter();
        //d->start_cross_correlograms_computer();
        d->update_all_widgets();
    }
    else if (str == "open_auto_correlograms") {
        d->open_auto_correlograms();
    }
    else if (str == "open_matrix_of_cross_correlograms") {
        d->open_matrix_of_cross_correlograms();
    }
    //else if (str=="open_templates") {
    //    d->open_templates();
    //}
    else if (str == "open_cluster_details") {
        d->open_cluster_details();
    }
    else if (str == "open_timeseries") {
        d->open_timeseries();
    }
    else if (str == "open_clips") {
        d->open_clips();
    }
    else if (str == "open_clusters") {
        d->open_clusters();
    }
    else if (str == "open_firing_events") {
        d->open_firing_events();
    }
    else if (str == "find_nearby_events") {
        d->find_nearby_events();
    }
    else if (str == "template_method") {
        d->update_cluster_details();
    }
}
*/

/// TODO important: allow errors in task progress.. and highlight in red.
/// TODO when user applies the filter with new shell split options, we need to close many of the views because the clusters have been relabeled

void MVOverview2Widget::slot_control_panel_user_action(QString str)
{
    if ((str == "apply_shell_splitting") || (str == "apply_filter")) {
        d->do_shell_split_and_event_filter();
        d->update_all_widgets();
    }
    else if (str == "update_all_open_views") {
        d->update_all_widgets();
    }
    else if (str == "open-cluster-details") {
        d->open_cluster_details();
    }
    else if (str == "open-auto-correlograms") {
        d->open_auto_correlograms();
    }
    else if (str == "open-matrix-of-cross-correlograms") {
        d->open_matrix_of_cross_correlograms();
    }
    else if (str == "open-timeseries-data") {
        d->open_timeseries();
    }
    else if (str == "open-clips") {
        d->open_clips();
    }
    else if (str == "open-clusters") {
        d->open_clusters();
    }
    else if (str == "open-firing-events") {
        d->open_firing_events();
    }
    else if (str == "find-nearby-events") {
        d->find_nearby_events();
    }
    else {
        TaskProgress task(str);
        task.error("user action not yet implemented.");
    }
}

void MVOverview2Widget::slot_auto_correlogram_activated(int index)
{
    TabberTabWidget* TW = d->tab_widget_of((QWidget*)sender());
    d->m_tabber->setCurrentContainer(TW);
    d->m_tabber->switchCurrentContainer();
    d->open_cross_correlograms(index + 1);
}

void MVOverview2Widget::slot_details_current_k_changed()
{
    MVClusterDetailWidget* X = (MVClusterDetailWidget*)sender();
    int index = X->currentK() - 1;
    d->m_current_k = X->currentK();
    d->set_cross_correlograms_current_index(index);
}

void MVOverview2Widget::slot_details_selected_ks_changed()
{
    MVClusterDetailWidget* X = (MVClusterDetailWidget*)sender();
    QList<int> ks = X->selectedKs();
    QList<int> indices;
    foreach (int k, ks)
        indices << k - 1;
    d->m_selected_ks = ks.toSet();
    d->set_cross_correlograms_selected_indices(indices);
}

void MVOverview2Widget::slot_details_template_activated()
{
    MVClusterDetailWidget* X = (MVClusterDetailWidget*)sender();
    int k = X->currentK();
    if (k < 0)
        return;
    TabberTabWidget* TW = d->tab_widget_of((QWidget*)sender());
    d->m_tabber->setCurrentContainer(TW);
    d->m_tabber->switchCurrentContainer();
    d->open_clips();
}

void MVOverview2Widget::slot_cross_correlogram_current_index_changed()
{
    MVCrossCorrelogramsWidget2* X = (MVCrossCorrelogramsWidget2*)sender();
    d->m_current_k = X->currentLabel1();
    d->set_cross_correlograms_current_index(X->currentIndex());
    d->set_templates_current_number(X->currentLabel2());
}

void MVOverview2Widget::slot_cross_correlogram_selected_indices_changed()
{
    MVCrossCorrelogramsWidget2* X = (MVCrossCorrelogramsWidget2*)sender();
    d->m_selected_ks = X->selectedLabels1().toSet();
    d->set_cross_correlograms_selected_indices(X->selectedIndices());
    d->set_templates_selected_numbers(X->selectedLabels2());
}

void MVOverview2Widget::slot_clips_view_current_event_changed()
{
    MVClipsView* V = (MVClipsView*)sender();
    MVEvent evt = V->currentEvent();
    d->set_current_event(evt);
}

void MVOverview2Widget::slot_clips_widget_current_event_changed()
{
    MVClipsWidget* W = (MVClipsWidget*)sender();
    MVEvent evt = W->currentEvent();
    d->set_current_event(evt);
}

void MVOverview2Widget::slot_cluster_view_current_event_changed()
{
    MVClusterWidget* W = (MVClusterWidget*)sender();
    MVEvent evt = W->currentEvent();
    d->set_current_event(evt);
}

void MVOverview2WidgetPrivate::update_sizes()
{
    float W0 = q->width();
    float H0 = q->height();

    int W1 = W0 / 3;
    if (W1 < 150)
        W1 = 150;
    if (W1 > 400)
        W1 = 400;
    int W2 = W0 - W1;

    int H1 = H0 / 2;
    int H2 = H0 / 2;
    //int H3=H0-H1-H2;

    {
        QList<int> sizes;
        sizes << W1 << W2;
        m_splitter1->setSizes(sizes);
    }
    {
        QList<int> sizes;
        sizes << H1 << H2;
        m_splitter2->setSizes(sizes);
    }
}

void MVOverview2WidgetPrivate::update_all_widgets()
{
    QList<QWidget*> list = get_all_widgets();
    foreach (QWidget* W, list) {
        update_widget(W);
    }
}

void MVOverview2WidgetPrivate::update_cluster_details()
{
    QList<QWidget*> list = get_all_widgets();
    foreach (QWidget* W, list) {
        if (W->property("widget_type") == "cluster_details") {
            update_widget(W);
        }
    }
}

void MVOverview2WidgetPrivate::update_clips()
{
    QList<QWidget*> list = get_all_widgets();
    foreach (QWidget* W, list) {
        if (W->property("widget_type") == "clips") {
            update_widget(W);
        }
        if (W->property("widget_type") == "find_nearby_events") {
            update_widget(W);
        }
    }
}

void MVOverview2WidgetPrivate::update_cluster_views()
{
    QList<QWidget*> list = get_all_widgets();
    foreach (QWidget* W, list) {
        if (W->property("widget_type") == "clusters") {
            update_widget(W);
        }
    }
}

void MVOverview2WidgetPrivate::update_firing_event_views()
{
    QList<QWidget*> list = get_all_widgets();
    foreach (QWidget* W, list) {
        if (W->property("widget_type") == "firing_events") {
            update_widget(W);
        }
    }
}

double get_max(QList<double>& list)
{
    double ret = list.value(0);
    for (int i = 0; i < list.count(); i++) {
        if (list[i] > ret)
            ret = list[i];
    }
    return ret;
}

double get_min(QList<double>& list)
{
    double ret = list.value(0);
    for (int i = 0; i < list.count(); i++) {
        if (list[i] < ret)
            ret = list[i];
    }
    return ret;
}

void define_shells(QList<double>& shell_mins, QList<double>& shell_maxs, QList<double>& clip_peaks, double shell_increment, int min_shell_count)
{
    //positives
    double max_clip_peaks = get_max(clip_peaks);
    QList<double> shell_mins_pos;
    QList<double> shell_maxs_pos;
    {
        int num_bins = 1;
        while (shell_increment * num_bins <= max_clip_peaks)
            num_bins++;
        num_bins++;
        long counts[num_bins];
        for (int b = 0; b < num_bins; b++)
            counts[b] = 0;
        long tot = 0;
        for (int i = 0; i < clip_peaks.count(); i++) {
            if (clip_peaks[i] > 0) {
                int b = (int)(clip_peaks[i] / shell_increment);
                if (b < num_bins)
                    counts[b]++;
                else
                    qWarning() << "Unexpected problem" << __FILE__ << __LINE__;
                tot++;
            }
        }
        int min_b = 0;
        int max_b = 0;
        int count_in = counts[0];
        while (min_b < num_bins) {
            if ((count_in >= min_shell_count) && (tot - count_in >= min_shell_count)) {
                shell_mins_pos << min_b* shell_increment;
                shell_maxs_pos << (max_b + 1) * shell_increment;
                min_b = max_b + 1;
                max_b = max_b + 1;
                tot -= count_in;
                if (min_b < num_bins)
                    count_in = counts[min_b];
                else
                    count_in = 0;
            }
            else {
                max_b++;
                if (max_b < num_bins) {
                    count_in += counts[max_b];
                }
                else {
                    if (count_in > 0) {
                        shell_mins_pos << min_b* shell_increment;
                        shell_maxs_pos << (max_b + 1) * shell_increment;
                    }
                    break;
                }
            }
        }
    }

    //negatives
    double min_clip_peaks = get_min(clip_peaks);
    QList<double> shell_mins_neg;
    QList<double> shell_maxs_neg;
    {
        int num_bins = 1;
        while (shell_increment * num_bins <= -min_clip_peaks)
            num_bins++;
        num_bins++;
        long counts[num_bins];
        for (int b = 0; b < num_bins; b++)
            counts[b] = 0;
        long tot = 0;
        for (int i = 0; i < clip_peaks.count(); i++) {
            if (clip_peaks[i] < 0) {
                int b = (int)(-clip_peaks[i] / shell_increment);
                if (b < num_bins)
                    counts[b]++;
                else
                    qWarning() << "Unexpected problem" << __FILE__ << __LINE__;
                tot++;
            }
        }
        int min_b = 0;
        int max_b = 0;
        int count_in = counts[0];
        while (min_b < num_bins) {
            if ((count_in >= min_shell_count) && (tot - count_in >= min_shell_count)) {
                shell_mins_neg << min_b* shell_increment;
                shell_maxs_neg << (max_b + 1) * shell_increment;
                min_b = max_b + 1;
                max_b = max_b + 1;
                tot -= count_in;
                if (min_b < num_bins)
                    count_in = counts[min_b];
                else
                    count_in = 0;
            }
            else {
                max_b++;
                if (max_b < num_bins) {
                    count_in += counts[max_b];
                }
                else {
                    if (count_in > 0) {
                        shell_mins_neg << min_b* shell_increment;
                        shell_maxs_neg << (max_b + 1) * shell_increment;
                    }
                    break;
                }
            }
        }
    }

    //combine
    for (int i = shell_mins_neg.count() - 1; i >= 0; i--) {
        shell_maxs << -shell_mins_neg[i];
        shell_mins << -shell_maxs_neg[i];
    }
    for (int i = 0; i < shell_mins_pos.count(); i++) {
        shell_mins << shell_mins_pos[i];
        shell_maxs << shell_maxs_pos[i];
    }
}

void MVOverview2WidgetPrivate::do_shell_split_and_event_filter()
{
    TaskProgress task("do_shell_split_and_event_filter()");

    MountainsortThread MT;
    QString processor_name = "mv_firings_filter";

    MT.setProcessorName(processor_name);

    QMap<QString, QVariant> params;

    MVEventFilter evt_filter = m_control_panel_new->eventFilter();

    /// TODO shell_width should be changed to shell_increment in params
    /// TODO should use_shell_split etc be separated out from evt filter?
    /// /// TODO should we check whether we need to use a filter at all? (see use_event_filter below)
    params["use_shell_split"] = evt_filter.use_shell_split;
    params["shell_width"] = evt_filter.shell_increment;
    params["min_per_shell"] = evt_filter.min_per_shell;
    params["use_event_filter"] = true;
    if (evt_filter.min_detectability_score) {
        params["min_detectability_score"] = evt_filter.min_detectability_score;
    }
    if (evt_filter.max_outlier_score) {
        params["max_outlier_score"] = evt_filter.max_outlier_score;
    }
    /// TODO min_amplitude and max_outlier_score should NOT be required parameters in the processor
    params["min_amplitude"] = 0;

    params["firings"] = m_firings_original.makePath();

    QStringList debug_keys = params.keys();
    foreach (QString key, debug_keys) {
        task.log(QString("%1 = %2").arg(key).arg(params[key].toString()));
    }

    MT.setInputParameters(params);
    MT.setMscmdServerUrl(m_mscmdserver_url);

    QString firings_out = MT.makeOutputFilePath("firings_out");
    QString original_cluster_numbers_out = MT.makeOutputFilePath("original_cluster_numbers");
    MT.compute();
    m_firings.setPath(firings_out);
    task.log("m_firings path = " + firings_out);
    task.log(QString("m_firings.N1()=%1 m_firings.N2()=%2").arg(m_firings.N1()).arg(m_firings.N2()));
    m_original_cluster_numbers.clear();
    m_original_cluster_offsets.clear();
    DiskReadMda AA(original_cluster_numbers_out);
    int offset = 0;
    for (int i = 0; i < AA.totalSize(); i++) {
        if (AA.value(i) != AA.value(i - 1)) {
            offset = 0;
        }
        offset++;
        m_original_cluster_numbers << AA.value(i);
        m_original_cluster_offsets << offset;
    }

    this->set_templates_current_number(-1);
    this->set_templates_selected_numbers(QList<int>());
}

void MVOverview2WidgetPrivate::add_tab(QWidget* W, QString label)
{
    W->setFocusPolicy(Qt::StrongFocus);
    //current_tab_widget()->addTab(W,label);
    //current_tab_widget()->setCurrentIndex(current_tab_widget()->count()-1);
    m_tabber->addWidget(m_tabber->currentContainerName(), label, W);
    W->setProperty("tab_label", label); //won't be needed in future, once Tabber is fully implemented
}

MVCrossCorrelogramsWidget2* MVOverview2WidgetPrivate::open_auto_correlograms()
{
    MVCrossCorrelogramsWidget2* X = new MVCrossCorrelogramsWidget2;
    X->setProperty("widget_type", "auto_correlograms");
    add_tab(X, "Auto-Correlograms");
    QObject::connect(X, SIGNAL(indexActivated(int)), q, SLOT(slot_auto_correlogram_activated(int)));
    QObject::connect(X, SIGNAL(currentIndexChanged()), q, SLOT(slot_cross_correlogram_current_index_changed()));
    QObject::connect(X, SIGNAL(selectedIndicesChanged()), q, SLOT(slot_cross_correlogram_selected_indices_changed()));
    update_widget(X);
    return X;
}

MVCrossCorrelogramsWidget2* MVOverview2WidgetPrivate::open_cross_correlograms(int k)
{
    MVCrossCorrelogramsWidget2* X = new MVCrossCorrelogramsWidget2;
    X->setProperty("widget_type", "cross_correlograms");
    X->setProperty("kk", k);
    add_tab(X, QString("CC for %1(%2)").arg(m_original_cluster_numbers.value(k)).arg(m_original_cluster_offsets.value(k)));
    QObject::connect(X, SIGNAL(currentIndexChanged()), q, SLOT(slot_cross_correlogram_current_index_changed()));
    QObject::connect(X, SIGNAL(selectedIndicesChanged()), q, SLOT(slot_cross_correlogram_selected_indices_changed()));
    update_widget(X);
    return X;
}

QStringList int_list_to_string_list(const QList<int>& list)
{
    QStringList list2;
    for (int i = 0; i < list.count(); i++)
        list2 << QString("%1").arg(list[i]);
    return list2;
}

QList<int> string_list_to_int_list(const QList<QString>& list)
{
    QList<int> list2;
    for (int i = 0; i < list.count(); i++)
        list2 << list[i].toInt();
    return list2;
}

MVCrossCorrelogramsWidget2* MVOverview2WidgetPrivate::open_matrix_of_cross_correlograms()
{
    MVCrossCorrelogramsWidget2* X = new MVCrossCorrelogramsWidget2;
    X->setProperty("widget_type", "matrix_of_cross_correlograms");
    QList<int> ks = m_selected_ks.toList();
    qSort(ks);
    if (ks.isEmpty())
        return X;
    X->setProperty("ks", int_list_to_string_list(ks));
    add_tab(X, QString("CC Matrix"));
    update_widget(X);
    return X;
}

//void MVOverview2WidgetPrivate::open_templates()
//{
//	SSTimeSeriesView *X=new SSTimeSeriesView;
//	X->initialize();
//	X->setProperty("widget_type","templates");
//	add_tab(X,QString("Templates"));
//    QObject::connect(X,SIGNAL(currentXChanged()),q,SLOT(slot_templates_clicked()));
//	update_widget(X);
//}

MVClusterDetailWidget* MVOverview2WidgetPrivate::open_cluster_details()
{
    MVClusterDetailWidget* X = new MVClusterDetailWidget;
    X->setMscmdServerUrl(m_mscmdserver_url);
    X->setChannelColors(m_channel_colors);
    X->setTimeseries(m_timeseries);
    //X->setFirings(DiskReadMda(m_firings)); //done in update_widget
    X->setSampleRate(m_samplerate);
    QObject::connect(X, SIGNAL(signalTemplateActivated()), q, SLOT(slot_details_template_activated()));
    QObject::connect(X, SIGNAL(signalCurrentKChanged()), q, SLOT(slot_details_current_k_changed()));
    QObject::connect(X, SIGNAL(signalSelectedKsChanged()), q, SLOT(slot_details_selected_ks_changed()));
    X->setProperty("widget_type", "cluster_details");
    add_tab(X, QString("Details"));
    update_widget(X);
    return X;
}

void MVOverview2WidgetPrivate::open_timeseries()
{
    SSTimeSeriesWidget* X = new SSTimeSeriesWidget;
    SSTimeSeriesView* V = new SSTimeSeriesView;
    V->initialize();
    V->setSampleRate(m_samplerate);
    X->addView(V);
    X->setProperty("widget_type", "timeseries");
    add_tab(X, QString("Timeseries"));
    update_widget(X);
}

void MVOverview2WidgetPrivate::open_clips()
{
    QList<int> ks = m_selected_ks.toList();
    qSort(ks);
    if (ks.count() == 0) {
        QMessageBox::information(q, "Unable to open clips", "You must select at least one cluster.");
        return;
    }

    MVClipsWidget* X = new MVClipsWidget;
    X->setMscmdServerUrl(m_mscmdserver_url);
    X->setProperty("widget_type", "clips");
    X->setProperty("ks", int_list_to_string_list(ks));
    q->connect(X, SIGNAL(currentEventChanged()), q, SLOT(slot_clips_widget_current_event_changed()));
    QString tab_title = "Clips";
    if (ks.count() == 1) {
        int kk = ks[0];
        tab_title = QString("Clips %1(%2)").arg(m_original_cluster_numbers.value(kk)).arg(m_original_cluster_offsets.value(kk));
    }

    add_tab(X, tab_title);
    update_widget(X);

    /*
    MVClipsView* X = MVClipsView::newInstance();
    X->setProperty("widget_type", "clips");
    X->setProperty("ks", int_list_to_string_list(ks));
    q->connect(X, SIGNAL(currentEventChanged()), q, SLOT(slot_clips_view_current_event_changed()));
    QString tab_title = "Clips";
    if (ks.count() == 1) {
        int kk = ks[0];
        tab_title = QString("Clips %1(%2)").arg(m_original_cluster_numbers.value(kk)).arg(m_original_cluster_offsets.value(kk));
    }
    add_tab(X, tab_title);
    update_widget(X);
    X->setXRange(vec2(0, 5000));
    */
}

void MVOverview2WidgetPrivate::open_clusters()
{
    QList<int> ks = m_selected_ks.toList();
    qSort(ks);
    if (ks.count() == 0) {
        QMessageBox::information(q, "Unable to open clusters", "You must select at least one cluster.");
        return;
    }
    MVClusterWidget* X = new MVClusterWidget;
    X->setMscmdServerUrl(m_mscmdserver_url);
    X->setProperty("widget_type", "clusters");
    X->setProperty("ks", int_list_to_string_list(ks));
    q->connect(X, SIGNAL(currentEventChanged()), q, SLOT(slot_cluster_view_current_event_changed()));
    add_tab(X, QString("Clusters"));
    update_widget(X);
}

void MVOverview2WidgetPrivate::open_firing_events()
{
    QList<int> ks = m_selected_ks.toList();
    qSort(ks);
    if (ks.count() == 0) {
        QMessageBox::information(q, "Unable to open firing events", "You must select at least one cluster.");
        return;
    }
    /// TODO rename MVFiringRateView to something like MVFiringEventsView
    MVFiringRateView* X = new MVFiringRateView;
    X->setProperty("widget_type", "firing_events");
    X->setProperty("ks", int_list_to_string_list(ks));
    //q->connect(X,SIGNAL(currentEventChanged()),q,SLOT(slot_firing_events_view_current_event_changed()));
    add_tab(X, QString("Firing Events"));
    update_widget(X);
}

void MVOverview2WidgetPrivate::find_nearby_events()
{
    QList<int> ks = m_selected_ks.toList();
    qSort(ks);
    if (ks.count() < 2) {
        QMessageBox::information(q, "Problem finding nearby events", "You must select at least two clusters.");
        return;
    }

    MVClipsView* X = MVClipsView::newInstance();
    X->setProperty("widget_type", "find_nearby_events");
    X->setProperty("ks", int_list_to_string_list(ks));
    q->connect(X, SIGNAL(currentEventChanged()), q, SLOT(slot_clips_view_current_event_changed()));
    QString tab_title = "Nearby Clips";
    if (ks.count() == 2) {
        int kk1 = ks[0];
        int kk2 = ks[1];
        tab_title = QString("Clips %1(%2) : Clips %3(%4)")
                        .arg(m_original_cluster_numbers.value(kk1))
                        .arg(m_original_cluster_offsets.value(kk1))
                        .arg(m_original_cluster_numbers.value(kk2))
                        .arg(m_original_cluster_offsets.value(kk2));
    }
    add_tab(X, tab_title);
    update_widget(X);
    X->setXRange(vec2(0, 5000));
}

void MVOverview2WidgetPrivate::update_cross_correlograms()
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if ((widget_type == "auto_correlograms") || (widget_type == "cross_correlograms")) {
            update_widget(W);
        }
    }
}

void MVOverview2WidgetPrivate::update_timeseries_views()
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if (widget_type == "timeseries") {
            update_widget(W);
        }
    }
}

void MVOverview2WidgetPrivate::move_to_timepoint(double tp)
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if (widget_type == "timeseries") {
            SSTimeSeriesWidget* V = (SSTimeSeriesWidget*)W;
            V->view(0)->setCurrentTimepoint(tp);
        }
    }
}

void subtract_features_mean(Mda& F)
{
    if (F.N2() == 0)
        return;
    double mean[F.N1()];
    for (int i = 0; i < F.N1(); i++)
        mean[i] = 0;
    for (int i = 0; i < F.N2(); i++) {
        for (int j = 0; j < F.N1(); j++) {
            mean[j] += F.value(j, i);
        }
    }
    for (int i = 0; i < F.N1(); i++)
        mean[i] /= F.N2();
    for (int i = 0; i < F.N2(); i++) {
        for (int j = 0; j < F.N1(); j++) {
            F.setValue(F.value(j, i) - mean[j], j, i);
        }
    }
}

void normalize_features(Mda& F, bool individual_channels)
{
    if (individual_channels) {
        for (int m = 0; m < F.N1(); m++) {
            double sumsqr = 0;
            double sum = 0;
            int NN = F.N2();
            for (int i = 0; i < NN; i++) {
                sumsqr += F.value(m, i) * F.value(m, i);
                sum += F.value(m, i);
            }
            double norm = 1;
            if (NN >= 2)
                norm = sqrt((sumsqr - sum * sum / NN) / (NN - 1));
            for (int i = 0; i < NN; i++)
                F.setValue(F.value(m, i) / norm, m, i);
        }
    }
    else {
        double sumsqr = 0;
        double sum = 0;
        int NN = F.totalSize();
        for (int i = 0; i < NN; i++) {
            sumsqr += F.get(i) * F.get(i);
            sum += F.get(i);
        }
        double norm = 1;
        if (NN >= 2)
            norm = sqrt((sumsqr - sum * sum / NN) / (NN - 1));
        for (int i = 0; i < NN; i++)
            F.set(F.get(i) / norm, i);
    }
}

void MVOverview2WidgetPrivate::update_widget(QWidget* W)
{
    QString widget_type = W->property("widget_type").toString();
    if (widget_type == "auto_correlograms") {
        MVCrossCorrelogramsWidget2* WW = (MVCrossCorrelogramsWidget2*)W;
        WW->setSampleRate(m_samplerate);
        WW->setMaxDt(cc_max_dt());
        WW->setColors(m_colors);
        WW->setFirings(m_firings);
        //WW->setCrossCorrelogramsData(DiskReadMda(m_cross_correlograms_data));
        QStringList text_labels;
        QList<int> labels1, labels2;
        for (int i = 1; i < m_original_cluster_numbers.count(); i++) {
            labels1 << i;
            labels2 << i;
            if ((i == 1) || (m_original_cluster_numbers[i] != m_original_cluster_numbers[i - 1])) {
                text_labels << QString("Auto %1").arg(m_original_cluster_numbers[i]);
            }
            else
                text_labels << "";
        }
        //WW->setTextLabels(labels);
        WW->setLabelPairs(labels1, labels2, text_labels);
        //WW->updateWidget();
    }
    else if (widget_type == "cross_correlograms") {
        MVCrossCorrelogramsWidget2* WW = (MVCrossCorrelogramsWidget2*)W;
        int k = W->property("kk").toInt();
        WW->setColors(m_colors);
        WW->setSampleRate(m_samplerate);
        WW->setMaxDt(cc_max_dt());
        WW->setFirings(DiskReadMda(m_firings));
        //WW->setCrossCorrelogramsData(DiskReadMda(m_cross_correlograms_data));
        //WW->setBaseLabel(k);
        QStringList text_labels;
        QList<int> labels1, labels2;
        for (int i = 1; i < m_original_cluster_numbers.count(); i++) {
            labels1 << k;
            labels2 << i;
            if ((i == 1) || (m_original_cluster_numbers[i] != m_original_cluster_numbers[i - 1])) {
                text_labels << QString("Cross %1(%2)/%3").arg(m_original_cluster_numbers[k + 1]).arg(m_original_cluster_offsets.value(k + 1)).arg(m_original_cluster_numbers[i]);
            }
            else
                text_labels << "";
        }
        //WW->setTextLabels(labels);
        WW->setLabelPairs(labels1, labels2, text_labels);
        //WW->updateWidget();
    }
    else if (widget_type == "matrix_of_cross_correlograms") {
        MVCrossCorrelogramsWidget2* WW = (MVCrossCorrelogramsWidget2*)W;
        QList<int> ks = string_list_to_int_list(W->property("ks").toStringList());
        WW->setColors(m_colors);
        WW->setSampleRate(m_samplerate);
        WW->setMaxDt(cc_max_dt());
        WW->setFirings(DiskReadMda(m_firings));
        //WW->setCrossCorrelogramsData(DiskReadMda(m_cross_correlograms_data));
        //WW->setLabelNumbers(ks);
        QStringList text_labels;
        QList<int> labels1, labels2;
        //text_labels << "";
        for (int a1 = 0; a1 < ks.count(); a1++) {
            QString str1 = QString("%1(%2)").arg(m_original_cluster_numbers[ks[a1]]).arg(m_original_cluster_offsets[ks[a1]]);
            for (int a2 = 0; a2 < ks.count(); a2++) {
                QString str2 = QString("%1(%2)").arg(m_original_cluster_numbers[ks[a2]]).arg(m_original_cluster_offsets[ks[a2]]);
                text_labels << QString("%1/%2").arg(str1).arg(str2);
                labels1 << ks.value(a1);
                labels2 << ks.value(a2);
            }
        }
        //WW->setTextLabels(labels);
        WW->setLabelPairs(labels1, labels2, text_labels);
        //WW->updateWidget();
    }
    /*else if (widget_type=="templates") {
		printf("Setting templates data...\n");
		SSTimeSeriesView *WW=(SSTimeSeriesView *)W;
		Mda TD=m_templates_data;
		DiskArrayModel_New *MM=new DiskArrayModel_New;
		MM->setFromMda(TD);
		int KK=TD.N3();
		QList<long> times,labels;
		int last_k=-1;
		for (int kk=1; kk<=KK; kk++) {
			int k=m_original_cluster_numbers.value(kk);
			if (k!=last_k) {
				times << (long)(TD.N2()*(kk-1+0.5));
				labels << k;
			}
			last_k=k;
		}
		WW->setData(MM,true);
		WW->setTimesLabels(times,labels);
		WW->setMarkerLinesVisible(false);
		printf(".\n");
    }*/
    else if (widget_type == "cluster_details") {
        MVClusterDetailWidget* WW = (MVClusterDetailWidget*)W;
        //int clip_size = m_control_panel->getParameterValue("clip_size").toInt();
        TaskProgress task("Update cluster details");
        int clip_size = m_control_panel_new->viewOptions().clip_size;
        WW->setColors(m_colors);
        WW->setTimeseries(m_timeseries);
        WW->setClipSize(clip_size);
        WW->setFirings(DiskReadMda(m_firings));
        WW->setGroupNumbers(m_original_cluster_numbers);
        WW->zoomAllTheWayOut();
        task.log(QString("clip_size=%1, m_firings.N1()=%2, m_firings.N2()=%3").arg(clip_size).arg(m_firings.N1()).arg(m_firings.N2()));
    }
    else if (widget_type == "clips") {
        MVClipsWidget* WW = (MVClipsWidget*)W;
        int clip_size = m_control_panel_new->viewOptions().clip_size;
        QList<int> ks = string_list_to_int_list(WW->property("ks").toStringList());
        WW->setTimeseries(m_timeseries);
        WW->setClipSize(clip_size);
        WW->setFirings(m_firings);
        WW->setLabelsToUse(ks);
    }
    else if (widget_type == "find_nearby_events") {
        printf("Extracting clips...\n");
        MVClipsView* WW = (MVClipsView*)W;
        QList<int> ks = string_list_to_int_list(WW->property("ks").toStringList());

        QList<int> labels;
        QList<double> times;

        for (int n = 0; n < m_firings.N2(); n++) {
            times << m_firings.value(1, n);
            labels << (int)m_firings.value(2, n);
        }

        int clip_size = m_control_panel_new->viewOptions().clip_size;

        QList<long> inds;
        if (widget_type == "clips") {
            for (int ik = 0; ik < ks.count(); ik++) {
                int kk = ks[ik];
                for (int n = 0; n < labels.count(); n++) {
                    if (labels[n] == kk) {
                        inds << n;
                    }
                }
            }
        }
        else if (widget_type == "find_nearby_events") {
            long max_time = (long)(compute_max(times) + 1);
            Mda occupied(ks.count(), max_time + 1);
            for (int ik = 0; ik < ks.count(); ik++) {
                int kk = ks[ik];
                for (int n = 0; n < labels.count(); n++) {
                    if (labels[n] == kk) {
                        occupied.setValue(1, ik, (long)times[n]);
                    }
                }
            }
            int radius = clip_size / 2 - 1;
            int kk1 = ks.value(0);
            for (long n = 0; n < labels.count(); n++) {
                if (labels[n] == kk1) {
                    bool okay = true;
                    for (int ik = 1; (ik < ks.count()) && (okay); ik++) {
                        bool found = false;
                        for (int dt = -radius; dt <= radius; dt++) {
                            if (occupied.value(ik, (long)times[n] + dt)) {
                                found = true;
                            }
                        }
                        if (!found)
                            okay = false;
                    }
                    if (okay)
                        inds << n;
                }
            }
        }

        QList<double> times_kk;
        QList<int> labels_kk;
        for (long i = 0; i < inds.count(); i++) {
            long n = inds[i];
            times_kk << times[n];
            labels_kk << labels[n];
        }

        Mda clips = extract_clips(m_timeseries, times_kk, clip_size);
        printf("Setting data...\n");
        //DiskArrayModel_New *DAM=new DiskArrayModel_New;
        //DAM->setFromMda(clips);
        //WW->setData(DAM,true);
        WW->setClips(clips);
        WW->setTimes(times_kk);
        WW->setLabels(labels_kk);
        printf(".\n");
    }
    else if (widget_type == "clusters") {
        MVClusterWidget* WW = (MVClusterWidget*)W;
        int clip_size = m_control_panel_new->viewOptions().clip_size;
        QList<int> ks = string_list_to_int_list(WW->property("ks").toStringList());
        WW->setTimeseries(m_timeseries);
        WW->setClipSize(clip_size);
        WW->setFirings(m_firings);
        WW->setLabelsToUse(ks);
    }
    else if (widget_type == "firing_events") {
        MVFiringRateView* WW = (MVFiringRateView*)W;
        QList<int> ks = string_list_to_int_list(WW->property("ks").toStringList());
        QSet<int> ks_set = ks.toSet();

        QList<double> times, amplitudes;
        QList<int> labels;
        for (int i = 0; i < m_firings.N2(); i++) {
            int label = (int)m_firings.value(2, i);
            if (ks_set.contains(label)) {
                times << m_firings.value(1, i);
                amplitudes << m_firings.value(3, i);
                labels << label;
            }
        }
        Mda firings2;
        firings2.allocate(4, times.count());
        for (int i = 0; i < times.count(); i++) {
            firings2.setValue(times[i], 1, i);
            firings2.setValue(labels[i], 2, i);
            firings2.setValue(amplitudes[i], 3, i);
        }
        WW->setFirings(firings2);
        WW->setSampleRate(m_samplerate);
        WW->setEpochs(m_epochs);
    }
    else if (widget_type == "timeseries") {
        SSTimeSeriesWidget* WW = (SSTimeSeriesWidget*)W;
        DiskArrayModel_New* X = new DiskArrayModel_New;
        X->setPath(m_timeseries_paths[m_current_timeseries_name]);
        ((SSTimeSeriesView*)(WW->view()))->setData(X, true);
        set_times_labels_for_timeseries_widget(WW);
    }
}

void MVOverview2WidgetPrivate::set_cross_correlograms_current_index(int index)
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if ((widget_type == "auto_correlograms") || (widget_type == "cross_correlograms")) {
            MVCrossCorrelogramsWidget2* WW = (MVCrossCorrelogramsWidget2*)W;
            WW->setCurrentIndex(index);
        }
    }
}

void MVOverview2WidgetPrivate::set_cross_correlograms_selected_indices(const QList<int>& indices)
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if ((widget_type == "auto_correlograms") || (widget_type == "cross_correlograms")) {
            MVCrossCorrelogramsWidget2* WW = (MVCrossCorrelogramsWidget2*)W;
            WW->setSelectedIndices(indices);
        }
    }
}

void MVOverview2WidgetPrivate::set_templates_current_number(int kk)
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if (widget_type == "cluster_details") {
            MVClusterDetailWidget* WW = (MVClusterDetailWidget*)W;
            WW->setCurrentK(kk);
        }
    }
}

void MVOverview2WidgetPrivate::set_templates_selected_numbers(const QList<int>& kks)
{
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if (widget_type == "cluster_details") {
            MVClusterDetailWidget* WW = (MVClusterDetailWidget*)W;
            WW->setSelectedKs(kks);
        }
    }
}

void MVOverview2WidgetPrivate::set_times_labels_for_timeseries_widget(SSTimeSeriesWidget* WW)
{
    QList<long> times, labels;
    for (int n = 0; n < m_firings_original.N2(); n++) {
        long label0 = (long)m_firings_original.value(2, n);
        if ((m_selected_ks.isEmpty()) || (m_selected_ks.contains(label0))) {
            times << (long)m_firings_original.value(1, n);
            labels << label0;
        }
    }
    SSTimeSeriesView* V = (SSTimeSeriesView*)WW->view();
    V->setTimesLabels(times, labels);

    /*
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if (widget_type == "timeseries") {
            SSTimeSeriesWidget* WW = (SSTimeSeriesWidget*)W;
            SSTimeSeriesView* V = (SSTimeSeriesView*)WW->view();
            V->setTimesLabels(times, labels);
        }
    }
    */
}

QList<QWidget*> MVOverview2WidgetPrivate::get_all_widgets()
{
    return m_tabber->allWidgets();
    /*
	QList<QWidget *> ret;
	for (int i=0; i<m_tabs1->count(); i++) {
		ret << m_tabs1->widget(i);
	}
	for (int i=0; i<m_tabs2->count(); i++) {
		ret << m_tabs2->widget(i);
	}
	return ret;
	*/
}

TabberTabWidget* MVOverview2WidgetPrivate::tab_widget_of(QWidget* W)
{
    for (int i = 0; i < m_tabs1->count(); i++) {
        if (m_tabs1->widget(i) == W)
            return m_tabs1;
    }
    for (int i = 0; i < m_tabs2->count(); i++) {
        if (m_tabs2->widget(i) == W)
            return m_tabs2;
    }
    return m_tabs1;
}

void MVOverview2WidgetPrivate::remove_widgets_of_type(QString widget_type)
{
    QList<QWidget*> list = get_all_widgets();
    foreach (QWidget* W, list) {
        if (W->property("widget_type") == widget_type) {
            delete W;
        }
    }
}

Mda MVOverview2WidgetPrivate::compute_centroid(Mda& clips)
{
    int M = clips.N1();
    int T = clips.N2();
    int NC = clips.N3();
    Mda ret;
    ret.allocate(M, T);
    double* retptr = ret.dataPtr();
    double* clipsptr = clips.dataPtr();
    for (int i = 0; i < NC; i++) {
        int iii = i * M * T;
        int jjj = 0;
        for (int t = 0; t < T; t++) {
            for (int m = 0; m < M; m++) {
                retptr[jjj] += clipsptr[iii];
                iii++;
                jjj++;
            }
        }
    }
    if (NC) {
        for (int t = 0; t < T; t++) {
            for (int m = 0; m < M; m++) {
                double val = ret.value(m, t) / NC;
                ret.setValue(val, m, t);
            }
        }
    }
    return ret;
}

Mda MVOverview2WidgetPrivate::compute_geometric_median(Mda& clips, int num_iterations)
{
    int M = clips.N1();
    int T = clips.N2();
    int L = clips.N3();
    double* clipsptr = clips.dataPtr();
    Mda ret;
    ret.allocate(M, T);
    if (L == 0)
        return ret;

    int num_features = 6;
    Mda features;
    features.allocate(num_features, L);
    double* featuresptr = features.dataPtr();
#ifdef USE_LAPACK
    get_pca_features(M * T, L, num_features, featuresptr, clipsptr);
#else
    get_pca_features_2(M * T, L, num_features, featuresptr, clipsptr);
#endif

    Mda geomedian;
    geomedian.allocate(num_features, 1);
    double* geomedianptr = geomedian.dataPtr();
    compute_geometric_median(num_features, L, geomedianptr, featuresptr, num_iterations);

    QList<double> dists;
    {
        int kkk = 0;
        for (int j = 0; j < L; j++) {
            double sumsqr = 0;
            for (int a = 0; a < num_features; a++) {
                double diff = featuresptr[kkk] - geomedianptr[a];
                sumsqr += diff * diff;
                kkk++;
            }
            dists << sqrt(sumsqr);
        }
    }

    QList<double> dists_sorted = dists;
    qSort(dists_sorted);
    double cutoff = dists_sorted[(int)(L * 0.3)];
    QList<int> inds_to_use;
    for (int j = 0; j < L; j++) {
        if (dists[j] <= cutoff)
            inds_to_use << j;
    }

    Mda clips2;
    clips2.allocate(M, T, inds_to_use.count());
    for (int j = 0; j < inds_to_use.count(); j++) {
        int jj = inds_to_use[j];
        for (int t = 0; t < T; t++) {
            for (int m = 0; m < M; m++) {
                clips2.setValue(clips.value(m, t, jj), m, t, j);
            }
        }
    }

    ret = compute_centroid(clips2);
    return ret;
}

void MVOverview2WidgetPrivate::compute_geometric_median(int M, int N, double* output, double* input, int num_iterations)
{
    double* weights = (double*)malloc(sizeof(double) * N);
    double* dists = (double*)malloc(sizeof(double) * N);
    for (int j = 0; j < N; j++)
        weights[j] = 1;
    for (int it = 1; it <= num_iterations; it++) {
        float sumweights = 0;
        for (int j = 0; j < N; j++)
            sumweights += weights[j];
        if (sumweights)
            for (int j = 0; j < N; j++)
                weights[j] /= sumweights;
        for (int i = 0; i < M; i++)
            output[i] = 0;
        {
            //compute output
            int kkk = 0;
            for (int j = 0; j < N; j++) {
                int i = 0;
                for (int m = 0; m < M; m++) {
                    output[i] += weights[j] * input[kkk];
                    i++;
                    kkk++;
                }
            }
        }
        {
            //compute dists
            int kkk = 0;
            for (int j = 0; j < N; j++) {
                int i = 0;
                double sumsqr = 0;
                for (int m = 0; m < M; m++) {
                    double diff = output[i] - input[kkk];
                    i++;
                    kkk++;
                    sumsqr += diff * diff;
                }
                dists[j] = sqrt(sumsqr);
            }
        }
        {
            //compute weights
            for (int j = 0; j < N; j++) {
                if (dists[j])
                    weights[j] = 1 / dists[j];
                else
                    weights[j] = 0;
            }
        }
    }
    free(dists);
    free(weights);
}

void MVOverview2WidgetPrivate::set_progress(QString title, QString text, float frac)
{
    if (!m_progress_dialog) {
        m_progress_dialog = new QProgressDialog;
        m_progress_dialog->setCancelButton(0);
    }
    static QTime* timer = 0;
    if (!timer) {
        timer = new QTime;
        timer->start();
        m_progress_dialog->show();
        m_progress_dialog->repaint();
        qApp->processEvents();
    }
    if (timer->elapsed() > 500) {
        timer->restart();
        if (!m_progress_dialog->isVisible()) {
            m_progress_dialog->show();
        }
        m_progress_dialog->setLabelText(text);
        m_progress_dialog->setWindowTitle(title);
        m_progress_dialog->setValue((int)(frac * 100));
        m_progress_dialog->repaint();
        qApp->processEvents();
    }
    if (frac >= 1) {
        delete m_progress_dialog;
        m_progress_dialog = 0;
    }
}

void MVOverview2WidgetPrivate::set_current_event(MVEvent evt)
{
    if ((m_current_event.time == evt.time) && (m_current_event.label == evt.label)) {
        return;
    }
    m_current_event = evt;
    QList<QWidget*> widgets = get_all_widgets();
    foreach (QWidget* W, widgets) {
        QString widget_type = W->property("widget_type").toString();
        if (widget_type == "clips") {
            MVClipsWidget* WW = (MVClipsWidget*)W;
            WW->setCurrentEvent(evt);
        }
        else if (widget_type == "find_nearby_events") {
            MVClipsView* WW = (MVClipsView*)W;
            WW->setCurrentEvent(evt);
        }
        else if (widget_type == "clusters") {
            MVClusterWidget* WW = (MVClusterWidget*)W;
            WW->setCurrentEvent(evt);
        }
        else if (widget_type == "firing_events") {
            MVFiringRateView* WW = (MVFiringRateView*)W;
            WW->setCurrentEvent(evt);
        }
        else if (widget_type == "cluster_details") {
            MVClusterDetailWidget* WW = (MVClusterDetailWidget*)W;
            if (evt.label > 0) {
                WW->setCurrentK(evt.label);
            }
        }
        else if (widget_type == "timeseries") {
            SSTimeSeriesWidget* WW = (SSTimeSeriesWidget*)W;
            SSTimeSeriesView* VV = (SSTimeSeriesView*)WW->view(0);
            if (evt.time >= 0) {
                VV->setCurrentX(evt.time);
            }
        }
    }
}

long MVOverview2WidgetPrivate::cc_max_dt()
{
    //return (int)(m_control_panel->getParameterValue("max_dt").toInt() * m_samplerate / 1000);
    return m_control_panel_new->viewOptions().cc_max_dt;
}

typedef QList<long> IntList;
void CrossCorrelogramComputer::compute()
{
    QList<long> times, labels;
    long L = firings.N2();

    printf("Setting up times and labels...\n");
    for (int n = 0; n < L; n++) {
        times << (long)firings.value(1, n);
        labels << (long)firings.value(2, n);
    }
    int K = 0;
    for (int n = 0; n < labels.count(); n++) {
        if (labels[n] > K)
            K = labels[n];
    }

    printf("Sorting by times...\n");
    QList<long> indices = get_sort_indices(times);
    QList<long> times2, labels2;
    for (int i = 0; i < indices.count(); i++) {
        times2 << times[indices[i]];
        labels2 << labels[indices[i]];
    }
    times = times2;
    labels = labels2;

    printf("Initializing output...\n");
    QList<IntList> out;
    IntList empty_list;
    for (int k1 = 1; k1 <= K; k1++) {
        for (int k2 = 1; k2 <= K; k2++) {
            out << empty_list;
        }
    }

    printf("Setting time differences...\n");
    int i1 = 0;
    for (int i2 = 0; i2 < L; i2++) {
        if (i2 % 100 == 0) {
            //set_progress("Computing", "Creating cross correlograms data", i2 * 1.0 / L);
        }
        while ((i1 + 1 < L) && (times[i1] < times[i2] - max_dt))
            i1++;
        int k2 = labels[i2];
        int t2 = times[i2];
        if (k2 >= 1) {
            for (int jj = i1; jj < i2; jj++) {
                int k1 = labels[jj];
                int t1 = times[jj];
                if (k1 >= 1) {
                    out[(k1 - 1) + K * (k2 - 1)] << t2 - t1;
                    out[(k2 - 1) + K * (k1 - 1)] << t1 - t2;
                }
            }
        }
    }

    printf("Counting...\n");
    int ct = 0;
    for (int k1 = 1; k1 <= K; k1++) {
        for (int k2 = 1; k2 <= K; k2++) {
            ct += out[(k1 - 1) + K * (k2 - 1)].count();
        }
    }

    printf("Creating mda...\n");
    Mda ret;
    ret.allocate(3, ct);

    ct = 0;
    for (int k1 = 1; k1 <= K; k1++) {
        for (int k2 = 1; k2 <= K; k2++) {
            IntList* tmp = &out[(k1 - 1) + K * (k2 - 1)];
            for (int jj = 0; jj < tmp->count(); jj++) {
                ret.setValue(k1, 0, ct);
                ret.setValue(k2, 1, ct);
                ret.setValue((*tmp)[jj], 2, ct);
                ct++;
            }
        }
    }
    printf(".\n");

    cross_correlograms_data = ret;
}
