#ifndef UI_H
#define UI_H

#include "soundshare.h"
#include <gtk/gtk.h>

/* Build and run the GTK window.  Call from main(). */
int  ui_run(int argc, char **argv);

/* Thread-safe UI updates (schedule on GTK main loop) */
void ui_update_status(const char *msg);
void ui_update_stats(int64_t kbps, int64_t total_bytes, int64_t elapsed_ms);
void ui_update_latency(int64_t ms);
void ui_update_receiver_count(int count);
void ui_show_streaming(const char *format_info);
void ui_show_receiving(const char *server_ip);
void ui_reset(void);
void ui_add_chat_message(const char *sender, const char *text, int type);

/* Chat message types */
#define CHAT_TYPE_SENT     0
#define CHAT_TYPE_RECEIVED 1
#define CHAT_TYPE_SYSTEM   2

#endif /* UI_H */