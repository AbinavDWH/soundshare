#include "ui.h"
#include "config.h"
#include "network.h"
#include "streaming.h"
#include "receiving.h"
#include "receiving.h"
#include "receiving.h"
#include "chat.h"

#include <string.h>

/* ---- GTK widgets ---- */
static struct {
    GtkWidget *window;
    GtkWidget *lbl_status;
    GtkWidget *lbl_device_ip;
    GtkWidget *entry_ip;
    GtkWidget *btn_stream;
    GtkWidget *btn_receive;
    GtkWidget *combo_quality;
    GtkWidget *lbl_bitrate;
    GtkWidget *lbl_total;
    GtkWidget *lbl_duration;
    GtkWidget *lbl_sample_rate;
    GtkWidget *lbl_format;
    GtkWidget *lbl_connection;
    GtkWidget *lbl_receivers;
    GtkWidget *lbl_latency;
    GtkWidget *stats_box;
    GtkWidget *chat_box;
    GtkWidget *chat_view;
    GtkTextBuffer *chat_buffer;
    GtkWidget *chat_entry;
    GtkWidget *btn_chat_send;
    GtkWidget *lbl_chat_status;
} ui;

/* ---- Thread-safe UI update helpers ---- */

typedef struct {
    char text[512];
} TextUpdate;


/* Typed idle-callback data */
typedef struct {
    GtkWidget *label;
    char       text[512];
} LabelUpdate;

static gboolean update_label_idle(gpointer data)
{
    LabelUpdate *u = (LabelUpdate *)data;
    if (GTK_IS_LABEL(u->label))
        gtk_label_set_text(GTK_LABEL(u->label), u->text);
    free(u);
    return G_SOURCE_REMOVE;
}

static void set_label_threadsafe(GtkWidget *label, const char *text)
{
    LabelUpdate *u = malloc(sizeof(*u));
    if (!u) return;
    u->label = label;
    snprintf(u->text, sizeof(u->text), "%s", text);
    g_idle_add(update_label_idle, u);
}

/* Sensitivity update */
typedef struct {
    GtkWidget *widget;
    gboolean   sensitive;
} SensUpdate;

static gboolean update_sens_idle(gpointer data)
{
    SensUpdate *u = (SensUpdate *)data;
    gtk_widget_set_sensitive(u->widget, u->sensitive);
    free(u);
    return G_SOURCE_REMOVE;
}

static void set_sensitive_threadsafe(GtkWidget *w, gboolean s)
{
    SensUpdate *u = malloc(sizeof(*u));
    u->widget = w; u->sensitive = s;
    g_idle_add(update_sens_idle, u);
}

/* Visibility update */
typedef struct {
    GtkWidget *widget;
    gboolean   visible;
} VisUpdate;

static gboolean update_vis_idle(gpointer data)
{
    VisUpdate *u = (VisUpdate *)data;
    if (u->visible) gtk_widget_show(u->widget);
    else            gtk_widget_hide(u->widget);
    free(u);
    return G_SOURCE_REMOVE;
}

static void set_visible_threadsafe(GtkWidget *w, gboolean v)
{
    VisUpdate *u = malloc(sizeof(*u));
    u->widget = w; u->visible = v;
    g_idle_add(update_vis_idle, u);
}

/* Chat append */
typedef struct {
    char sender[256];
    char message[4096];
    int  type;
} ChatUpdate;

static gboolean append_chat_idle(gpointer data)
{
    ChatUpdate *u = (ChatUpdate *)data;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ui.chat_buffer, &end);

    char line[4400];
    if (u->type == CHAT_TYPE_SYSTEM)
        snprintf(line, sizeof(line), "*** %s ***\n", u->message);
    else
        snprintf(line, sizeof(line), "%s: %s\n", u->sender, u->message);

    gtk_text_buffer_insert(ui.chat_buffer, &end, line, -1);

    /* Scroll to bottom */
    GtkTextMark *mark = gtk_text_buffer_get_mark(ui.chat_buffer, "end");
    if (!mark) {
        gtk_text_buffer_get_end_iter(ui.chat_buffer, &end);
        mark = gtk_text_buffer_create_mark(ui.chat_buffer, "end", &end, FALSE);
    }
    gtk_text_buffer_get_end_iter(ui.chat_buffer, &end);
    gtk_text_buffer_move_mark(ui.chat_buffer, mark, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(ui.chat_view), mark);

    free(u);
    return G_SOURCE_REMOVE;
}

/* ---- Public thread-safe API ---- */

void ui_update_status(const char *msg) {
    set_label_threadsafe(ui.lbl_status, msg);
}

void ui_update_latency(int64_t ms)
{
    char buf[64];
    if (ms < 0) snprintf(buf, sizeof(buf), "Waiting...");
    else        snprintf(buf, sizeof(buf), "%lld ms", (long long)ms);
    set_label_threadsafe(ui.lbl_latency, buf);
}

void ui_update_receiver_count(int count)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", count);
    set_label_threadsafe(ui.lbl_receivers, buf);
}

void ui_update_stats(int64_t kbps, int64_t total_bytes, int64_t elapsed_ms)
{
    /* Bitrate */
    char br[64];
    if (kbps > 1000)
        snprintf(br, sizeof(br), "%.1f Mbps", kbps / 1000.0);
    else
        snprintf(br, sizeof(br), "%lld kbps", (long long)kbps);
    set_label_threadsafe(ui.lbl_bitrate, br);

    /* Total */
    char tot[64];
    if (total_bytes < 1024)
        snprintf(tot, sizeof(tot), "%lld B", (long long)total_bytes);
    else if (total_bytes < 1024*1024)
        snprintf(tot, sizeof(tot), "%.1f KB", total_bytes / 1024.0);
    else if (total_bytes < 1024LL*1024*1024)
        snprintf(tot, sizeof(tot), "%.2f MB", total_bytes / (1024.0*1024.0));
    else
        snprintf(tot, sizeof(tot), "%.2f GB", total_bytes / (1024.0*1024.0*1024.0));
    set_label_threadsafe(ui.lbl_total, tot);

    /* Duration */
    char dur[32];
    int64_t sec = elapsed_ms / 1000;
    if (sec >= 3600)
        snprintf(dur, sizeof(dur), "%lld:%02lld:%02lld",
                 (long long)(sec/3600), (long long)((sec%3600)/60),
                 (long long)(sec%60));
    else
        snprintf(dur, sizeof(dur), "%02lld:%02lld",
                 (long long)(sec/60), (long long)(sec%60));
    set_label_threadsafe(ui.lbl_duration, dur);
}

void ui_show_streaming(const char *format_info)
{
    set_label_threadsafe(ui.lbl_format, format_info);
    set_visible_threadsafe(ui.stats_box, TRUE);
    set_visible_threadsafe(ui.chat_box, TRUE);
    set_label_threadsafe(ui.lbl_bitrate, "0 kbps");
    set_label_threadsafe(ui.lbl_total, "0 B");
    set_label_threadsafe(ui.lbl_duration, "00:00");
    set_label_threadsafe(ui.lbl_receivers, "0");
    set_label_threadsafe(ui.lbl_latency, "Waiting...");
    set_sensitive_threadsafe(ui.btn_receive, FALSE);
    set_sensitive_threadsafe(ui.entry_ip, FALSE);
    set_sensitive_threadsafe(ui.combo_quality, FALSE);
}

void ui_show_receiving(const char *server_ip)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "Connected to %s", server_ip);
    set_label_threadsafe(ui.lbl_connection, buf);
    set_visible_threadsafe(ui.stats_box, TRUE);
    set_visible_threadsafe(ui.chat_box, TRUE);
    set_sensitive_threadsafe(ui.btn_stream, FALSE);
    set_sensitive_threadsafe(ui.entry_ip, FALSE);
    set_sensitive_threadsafe(ui.combo_quality, FALSE);
}

void ui_reset(void)
{
    set_visible_threadsafe(ui.stats_box, FALSE);
    set_visible_threadsafe(ui.chat_box, FALSE);
    set_sensitive_threadsafe(ui.btn_stream, TRUE);
    set_sensitive_threadsafe(ui.btn_receive, TRUE);
    set_sensitive_threadsafe(ui.entry_ip, TRUE);
    set_sensitive_threadsafe(ui.combo_quality, TRUE);
    set_label_threadsafe(ui.lbl_connection, "");

    /* Restore button labels */
    LabelUpdate *u1 = malloc(sizeof(*u1));
    u1->label = ui.btn_stream;
    snprintf(u1->text, sizeof(u1->text), "Start Streaming");
    g_idle_add(update_label_idle, u1);
}

void ui_add_chat_message(const char *sender, const char *text, int type)
{
    ChatUpdate *u = malloc(sizeof(*u));
    snprintf(u->sender, sizeof(u->sender), "%s", sender ? sender : "");
    snprintf(u->message, sizeof(u->message), "%s", text ? text : "");
    u->type = type;
    g_idle_add(append_chat_idle, u);
}

/* ---- Chat callback from chat.c ---- */

static void on_chat_received(const char *sender, const char *message,
                             void *user_data)
{
    (void)user_data;
    if (sender[0] == '\0')
        ui_add_chat_message("", message, CHAT_TYPE_SYSTEM);
    else
        ui_add_chat_message(sender, message, CHAT_TYPE_RECEIVED);
}

/* ---- GTK callbacks ---- */

static void on_stream_clicked(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;

    if (atomic_load(&g_app.is_streaming)) {
        streaming_stop();
        gtk_button_set_label(GTK_BUTTON(ui.btn_stream), "Start Streaming");
        return;
    }

    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(ui.combo_quality));
    if (idx < 0) idx = 2;
    g_app.selected_preset = idx;

    gtk_button_set_label(GTK_BUTTON(ui.btn_stream), "Stop Streaming");
    streaming_start(idx);
}

static void on_receive_clicked(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;

    if (atomic_load(&g_app.is_receiving)) {
        receiving_stop();
        gtk_button_set_label(GTK_BUTTON(ui.btn_receive), "Start Receiving");
        return;
    }

    const char *ip = gtk_entry_get_text(GTK_ENTRY(ui.entry_ip));
    if (!ip || strlen(ip) == 0) {
        ui_update_status("Enter streamer IP address");
        return;
    }

    gtk_button_set_label(GTK_BUTTON(ui.btn_receive), "Stop Receiving");
    receiving_start(ip);
}

static void on_chat_send(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;

    const char *text = gtk_entry_get_text(GTK_ENTRY(ui.chat_entry));
    if (!text || strlen(text) == 0) return;

    /* Show locally */
    ui_add_chat_message("You", text, CHAT_TYPE_SENT);

    /* Send over network */
    if (atomic_load(&g_app.is_streaming)) {
        chat_server_broadcast("Host", text);
    } else if (atomic_load(&g_app.is_receiving)) {
        char ip[64];
        net_get_device_ip(ip, sizeof(ip));
        chat_client_send(ip, text);
    }

    gtk_entry_set_text(GTK_ENTRY(ui.chat_entry), "");
}

static void on_ip_copy(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    const char *ip = gtk_label_get_text(GTK_LABEL(ui.lbl_device_ip));
    GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(cb, ip, -1);
}

static void on_window_destroy(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    streaming_stop();
    receiving_stop();
    gtk_main_quit();
}

/* ---- Build UI ---- */

static GtkWidget *make_label(const char *text, const char *css_class)
{
    GtkWidget *lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    if (css_class) {
        GtkStyleContext *sc = gtk_widget_get_style_context(lbl);
        gtk_style_context_add_class(sc, css_class);
    }
    return lbl;
}

static GtkWidget *make_stat_row(const char *title, GtkWidget **value_label)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl_title = make_label(title, NULL);
    *value_label = make_label("—", NULL);

    gtk_widget_set_size_request(lbl_title, 120, -1);
    gtk_box_pack_start(GTK_BOX(hbox), lbl_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), *value_label, TRUE, TRUE, 0);

    return hbox;
}

static void build_ui(void)
{
    /* ---- Window ---- */
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "SoundShare");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 500, 700);
    g_signal_connect(ui.window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    /* ---- CSS ---- */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "window { background-color: #1a1a2e; }"
        "label  { color: #e0e0e0; font-family: monospace; }"
        ".title { font-size: 18px; font-weight: bold; color: #00d4ff; }"
        ".status { color: #aaaaaa; font-size: 12px; }"
        ".ip    { font-size: 16px; font-weight: bold; color: #00ff88; }"
        "button { background: #16213e; color: #e0e0e0; border: 1px solid #0f3460;"
        "         padding: 8px 16px; border-radius: 6px; }"
        "button:hover { background: #0f3460; }"
        ".stream-btn { background: #00cc66; color: #000000; font-weight: bold; }"
        ".receive-btn { background: #0088ff; color: #ffffff; font-weight: bold; }"
        ".stop-btn { background: #ff4444; color: #ffffff; }"
        "entry  { background: #16213e; color: #e0e0e0; border: 1px solid #0f3460;"
        "         padding: 6px; border-radius: 4px; }"
        "combobox button { background: #16213e; }"
        "textview { background-color: #0d1117; color: #c9d1d9; font-family: monospace;"
        "           font-size: 11px; }"
        ".stat-value { color: #00d4ff; font-weight: bold; }"
        ".card { background: #16213e; border: 1px solid #0f3460; border-radius: 8px;"
        "        padding: 12px; margin: 4px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* ---- Main layout ---- */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(ui.window), scroll);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 16);
    gtk_container_add(GTK_CONTAINER(scroll), vbox);

    /* Title */
    GtkWidget *title = make_label("🔊 SoundShare", "title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 4);

    /* Device IP */
    GtkWidget *ip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ip_box), make_label("Device IP:", NULL), FALSE, FALSE, 0);
    ui.lbl_device_ip = make_label("Loading...", "ip");
    gtk_box_pack_start(GTK_BOX(ip_box), ui.lbl_device_ip, TRUE, TRUE, 0);

    GtkWidget *btn_copy = gtk_button_new_with_label("📋");
    g_signal_connect(btn_copy, "clicked", G_CALLBACK(on_ip_copy), NULL);
    gtk_box_pack_end(GTK_BOX(ip_box), btn_copy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), ip_box, FALSE, FALSE, 4);

    /* Quality selector */
    ui.combo_quality = gtk_combo_box_text_new();
    for (int i = 0; i < NUM_PRESETS; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.combo_quality),
                                       QUALITY_NAMES[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.combo_quality), 2);
    gtk_box_pack_start(GTK_BOX(vbox), ui.combo_quality, FALSE, FALSE, 4);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui.btn_stream = gtk_button_new_with_label("▶ Start Streaming");
    ui.btn_receive = gtk_button_new_with_label("📥 Start Receiving");

    GtkStyleContext *sc;
    sc = gtk_widget_get_style_context(ui.btn_stream);
    gtk_style_context_add_class(sc, "stream-btn");
    sc = gtk_widget_get_style_context(ui.btn_receive);
    gtk_style_context_add_class(sc, "receive-btn");

    g_signal_connect(ui.btn_stream, "clicked", G_CALLBACK(on_stream_clicked), NULL);
    g_signal_connect(ui.btn_receive, "clicked", G_CALLBACK(on_receive_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(btn_box), ui.btn_stream, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), ui.btn_receive, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), btn_box, FALSE, FALSE, 4);

    /* IP input */
    GtkWidget *ip_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(ip_input_box),
                       make_label("Streamer IP:", NULL), FALSE, FALSE, 0);
    ui.entry_ip = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui.entry_ip), "192.168.1.x");
    gtk_box_pack_start(GTK_BOX(ip_input_box), ui.entry_ip, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), ip_input_box, FALSE, FALSE, 4);

    /* Status */
    ui.lbl_status = make_label("Ready", "status");
    gtk_box_pack_start(GTK_BOX(vbox), ui.lbl_status, FALSE, FALSE, 4);

    /* ---- Stats box ---- */
    ui.stats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    sc = gtk_widget_get_style_context(ui.stats_box);
    gtk_style_context_add_class(sc, "card");

    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_label("📊 Stream Statistics", "title"), FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Bitrate:",     &ui.lbl_bitrate),    FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Total Sent:",  &ui.lbl_total),      FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Duration:",    &ui.lbl_duration),   FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Sample Rate:", &ui.lbl_sample_rate),FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Format:",      &ui.lbl_format),     FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Connection:",  &ui.lbl_connection), FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Receivers:",   &ui.lbl_receivers),  FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_stat_row("Latency:",     &ui.lbl_latency),    FALSE, FALSE, 2);

    gtk_box_pack_start(GTK_BOX(vbox), ui.stats_box, FALSE, FALSE, 4);
    gtk_widget_hide(ui.stats_box);

    /* ---- Chat box ---- */
    ui.chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    sc = gtk_widget_get_style_context(ui.chat_box);
    gtk_style_context_add_class(sc, "card");

    gtk_box_pack_start(GTK_BOX(ui.chat_box),
                       make_label("💬 Chat", "title"), FALSE, FALSE, 4);

    ui.lbl_chat_status = make_label("Disconnected", "status");
    gtk_box_pack_start(GTK_BOX(ui.chat_box), ui.lbl_chat_status, FALSE, FALSE, 2);

    /* Chat text view */
    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(chat_scroll, -1, 200);

    ui.chat_view   = gtk_text_view_new();
    ui.chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.chat_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ui.chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui.chat_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(chat_scroll), ui.chat_view);
    gtk_box_pack_start(GTK_BOX(ui.chat_box), chat_scroll, TRUE, TRUE, 4);

    /* Chat input */
    GtkWidget *chat_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    ui.chat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui.chat_entry), "Type a message...");
    g_signal_connect(ui.chat_entry, "activate", G_CALLBACK(on_chat_send), NULL);

    ui.btn_chat_send = gtk_button_new_with_label("Send");
    g_signal_connect(ui.btn_chat_send, "clicked", G_CALLBACK(on_chat_send), NULL);

    gtk_box_pack_start(GTK_BOX(chat_input_box), ui.chat_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chat_input_box), ui.btn_chat_send, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.chat_box), chat_input_box, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(vbox), ui.chat_box, FALSE, FALSE, 4);
    gtk_widget_hide(ui.chat_box);

    /* ---- Show device IP ---- */
    char ip[64];
    if (net_get_device_ip(ip, sizeof(ip)) == 0)
        gtk_label_set_text(GTK_LABEL(ui.lbl_device_ip), ip);
    else
        gtk_label_set_text(GTK_LABEL(ui.lbl_device_ip), "Not connected");

    /* ---- Register chat callback ---- */
    chat_set_callback(on_chat_received, NULL);

    gtk_widget_show_all(ui.window);
    gtk_widget_hide(ui.stats_box);
    gtk_widget_hide(ui.chat_box);
}

/* ---- Entry point ---- */
int ui_run(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    build_ui();
    gtk_main();
    return 0;
}