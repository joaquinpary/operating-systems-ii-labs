#include "ui_bridge.h"

void ui_init(void)
{
}

void ui_register_sos_callback(ui_event_cb_t cb)
{
    (void)cb;
}

void ui_register_delivered_callback(ui_event_cb_t cb)
{
    (void)cb;
}

void ui_update_stop(const char* stop_name, int current_position, int total_stops)
{
    (void)stop_name;
    (void)current_position;
    (void)total_stops;
}

void ui_process(void)
{
}

void ui_shutdown(void)
{
}
