#ifndef UI_BRIDGE_H_
#define UI_BRIDGE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_event_cb_t)(void);

void ui_init(void);
void ui_register_sos_callback(ui_event_cb_t cb);
void ui_register_delivered_callback(ui_event_cb_t cb);
void ui_update_stop(const char *stop_name, int current_position, int total_stops);
void ui_process(void);
void ui_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_BRIDGE_H_ */