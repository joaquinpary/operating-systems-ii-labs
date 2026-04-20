#include <QApplication>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "ui_bridge.h"

namespace
{

QApplication* g_app;
QWidget* g_window;
QLabel* g_stop_label;
QLabel* g_progress_label;
QPushButton* g_sos_button;
QPushButton* g_delivered_button;
ui_event_cb_t g_sos_callback;
ui_event_cb_t g_delivered_callback;

int g_argc = 1;
char g_arg0[] = "dhl_courier";
char* g_argv[] = {g_arg0, nullptr};

bool hasActiveStop(const char* stop_name, int total_stops)
{
    return stop_name != nullptr && stop_name[0] != '\0' && total_stops > 0;
}

} /* namespace */

extern "C" void ui_init(void)
{
    if (g_app != nullptr)
    {
        return;
    }

    g_app = new QApplication(g_argc, g_argv);
    g_window = new QWidget();
    g_window->setWindowTitle(QStringLiteral("DHL Courier Simulator"));
    g_window->resize(520, 320);
    g_window->setStyleSheet("QWidget { background: #f4efe5; color: #1f2933; }"
                            "QLabel#title { font-size: 16px; font-weight: 700; letter-spacing: 1px; }"
                            "QLabel#stop { font-size: 28px; font-weight: 700; padding: 12px;"
                            " background: white; border: 2px solid #d8c3a5; border-radius: 12px; }"
                            "QLabel#progress { font-size: 14px; color: #52606d; }"
                            "QPushButton { min-height: 54px; border-radius: 12px; font-size: 18px; font-weight: 700; }"
                            "QPushButton#sos { background: #b91c1c; color: white; }"
                            "QPushButton#delivered { background: #166534; color: white; }"
                            "QPushButton:disabled { background: #9aa5b1; color: #e4e7eb; }");

    auto* root_layout = new QVBoxLayout(g_window);
    root_layout->setContentsMargins(20, 20, 20, 20);
    root_layout->setSpacing(14);

    auto* title_label = new QLabel(QStringLiteral("DHL COURIER"), g_window);
    title_label->setObjectName(QStringLiteral("title"));

    g_stop_label = new QLabel(QStringLiteral("No route assigned"), g_window);
    g_stop_label->setObjectName(QStringLiteral("stop"));
    g_stop_label->setWordWrap(true);
    g_stop_label->setAlignment(Qt::AlignCenter);

    g_progress_label = new QLabel(QStringLiteral("Waiting for a route from the broker"), g_window);
    g_progress_label->setObjectName(QStringLiteral("progress"));
    g_progress_label->setAlignment(Qt::AlignCenter);

    auto* button_row = new QHBoxLayout();
    button_row->setSpacing(12);

    g_sos_button = new QPushButton(QStringLiteral("SOS"), g_window);
    g_sos_button->setObjectName(QStringLiteral("sos"));

    g_delivered_button = new QPushButton(QStringLiteral("Delivered"), g_window);
    g_delivered_button->setObjectName(QStringLiteral("delivered"));
    g_delivered_button->setEnabled(false);

    button_row->addWidget(g_sos_button);
    button_row->addWidget(g_delivered_button);

    root_layout->addWidget(title_label);
    root_layout->addStretch(1);
    root_layout->addWidget(g_stop_label);
    root_layout->addWidget(g_progress_label);
    root_layout->addStretch(1);
    root_layout->addLayout(button_row);

    QObject::connect(g_sos_button, &QPushButton::clicked, []() {
        if (g_sos_callback != nullptr)
        {
            g_sos_callback();
        }
    });

    QObject::connect(g_delivered_button, &QPushButton::clicked, []() {
        if (g_delivered_callback != nullptr)
        {
            g_delivered_callback();
        }
    });

    g_window->show();
}

extern "C" void ui_register_sos_callback(ui_event_cb_t cb)
{
    g_sos_callback = cb;
}

extern "C" void ui_register_delivered_callback(ui_event_cb_t cb)
{
    g_delivered_callback = cb;
}

extern "C" void ui_update_stop(const char* stop_name, int current_position, int total_stops)
{
    if (g_stop_label == nullptr || g_progress_label == nullptr)
    {
        return;
    }

    if (hasActiveStop(stop_name, total_stops))
    {
        g_stop_label->setText(QString::fromUtf8(stop_name));
        g_progress_label->setText(QStringLiteral("Stop %1 of %2").arg(current_position).arg(total_stops));
    }
    else if (total_stops > 0)
    {
        g_stop_label->setText(QStringLiteral("Route complete"));
        g_progress_label->setText(QStringLiteral("%1 deliveries confirmed").arg(total_stops));
    }
    else
    {
        g_stop_label->setText(QStringLiteral("No route assigned"));
        g_progress_label->setText(QStringLiteral("Waiting for a route from the broker"));
    }

    if (g_delivered_button != nullptr)
    {
        g_delivered_button->setEnabled(hasActiveStop(stop_name, total_stops));
    }
}

extern "C" void ui_process(void)
{
    if (g_app != nullptr)
    {
        g_app->processEvents(QEventLoop::AllEvents, 0);
    }
}

extern "C" void ui_shutdown(void)
{
    delete g_window;
    g_window = nullptr;
    g_stop_label = nullptr;
    g_progress_label = nullptr;
    g_sos_button = nullptr;
    g_delivered_button = nullptr;

    delete g_app;
    g_app = nullptr;
}
