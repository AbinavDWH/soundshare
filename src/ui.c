#include "ui.h"
#include "config.h"
#include "network.h"
#include "streaming.h"
#include "receiving.h"
#include "chat.h"

#include <string.h>

/* ---- GTK widgets ---- */
static struct {
    GtkWidget *window;
    GtkWidget *lbl_status;
    GtkWidget *status_icon;
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
    GtkWidget *lbl_preset_info;
    GtkWidget *header_bar;
} ui;

/* ---- Thread-safe helpers ---- */

typedef struct {
    GtkWidget *label;
    char       text[512];
} LabelUpdate;

static gboolean update_label_idle(gpointer data)
{
    LabelUpdate *u = (LabelUpdate *)data;
    if (GTK_IS_LABEL(u->label))
        gtk_label_set_markup(GTK_LABEL(u->label), u->text);
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

static void set_label_markup_threadsafe(GtkWidget *label, const char *markup)
{
    LabelUpdate *u = malloc(sizeof(*u));
    if (!u) return;
    u->label = label;
    snprintf(u->text, sizeof(u->text), "%s", markup);
    g_idle_add(update_label_idle, u);
}

typedef struct {
    GtkWidget *button;
    char       text[256];
} ButtonUpdate;

static gboolean update_button_idle(gpointer data)
{
    ButtonUpdate *u = (ButtonUpdate *)data;
    if (GTK_IS_BUTTON(u->button))
        gtk_button_set_label(GTK_BUTTON(u->button), u->text);
    free(u);
    return G_SOURCE_REMOVE;
}

static void set_button_label_threadsafe(GtkWidget *button, const char *text)
{
    ButtonUpdate *u = malloc(sizeof(*u));
    if (!u) return;
    u->button = button;
    snprintf(u->text, sizeof(u->text), "%s", text);
    g_idle_add(update_button_idle, u);
}

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
    if (!u) return;
    u->widget = w; u->sensitive = s;
    g_idle_add(update_sens_idle, u);
}

typedef struct {
    GtkWidget *widget;
    gboolean   visible;
} VisUpdate;

static gboolean update_vis_idle(gpointer data)
{
    VisUpdate *u = (VisUpdate *)data;
    if (u->visible) gtk_widget_show_all(u->widget);
    else            gtk_widget_hide(u->widget);
    free(u);
    return G_SOURCE_REMOVE;
}

static void set_visible_threadsafe(GtkWidget *w, gboolean v)
{
    VisUpdate *u = malloc(sizeof(*u));
    if (!u) return;
    u->widget = w; u->visible = v;
    g_idle_add(update_vis_idle, u);
}

/* Style class update */
typedef struct {
    GtkWidget *widget;
    char       add_class[64];
    char       remove_class[64];
} StyleUpdate;

static gboolean update_style_idle(gpointer data)
{
    StyleUpdate *u = (StyleUpdate *)data;
    GtkStyleContext *sc = gtk_widget_get_style_context(u->widget);
    if (u->remove_class[0])
        gtk_style_context_remove_class(sc, u->remove_class);
    if (u->add_class[0])
        gtk_style_context_add_class(sc, u->add_class);
    free(u);
    return G_SOURCE_REMOVE;
}

static void swap_style_class(GtkWidget *w, const char *add, const char *remove)
{
    StyleUpdate *u = malloc(sizeof(*u));
    if (!u) return;
    u->widget = w;
    snprintf(u->add_class, sizeof(u->add_class), "%s", add ? add : "");
    snprintf(u->remove_class, sizeof(u->remove_class), "%s", remove ? remove : "");
    g_idle_add(update_style_idle, u);
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
        snprintf(line, sizeof(line), "  %s\n", u->message);
    else if (u->type == CHAT_TYPE_SENT)
        snprintf(line, sizeof(line), "You:  %s\n", u->message);
    else
        snprintf(line, sizeof(line), "%s:  %s\n", u->sender, u->message);

    /* Insert with tag */
    GtkTextTag *tag = NULL;
    if (u->type == CHAT_TYPE_SYSTEM)
        tag = gtk_text_tag_table_lookup(
            gtk_text_buffer_get_tag_table(ui.chat_buffer), "system");
    else if (u->type == CHAT_TYPE_SENT)
        tag = gtk_text_tag_table_lookup(
            gtk_text_buffer_get_tag_table(ui.chat_buffer), "sent");
    else
        tag = gtk_text_tag_table_lookup(
            gtk_text_buffer_get_tag_table(ui.chat_buffer), "received");

    if (tag) {
        GtkTextIter start_mark;
        int offset = gtk_text_iter_get_offset(&end);
        gtk_text_buffer_insert(ui.chat_buffer, &end, line, -1);
        gtk_text_buffer_get_iter_at_offset(ui.chat_buffer, &start_mark, offset);
        gtk_text_buffer_get_end_iter(ui.chat_buffer, &end);
        gtk_text_buffer_apply_tag(ui.chat_buffer, tag, &start_mark, &end);
    } else {
        gtk_text_buffer_insert(ui.chat_buffer, &end, line, -1);
    }

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

void ui_update_status(const char *msg)
{
    set_label_threadsafe(ui.lbl_status, msg);
}

void ui_update_latency(int64_t ms)
{
    char buf[64];
    if (ms < 0)
        snprintf(buf, sizeof(buf), "<span color='#888'>Measuring...</span>");
    else if (ms < 20)
        snprintf(buf, sizeof(buf), "<span color='#00ff88'>%lld ms</span>", (long long)ms);
    else if (ms < 50)
        snprintf(buf, sizeof(buf), "<span color='#ffcc00'>%lld ms</span>", (long long)ms);
    else
        snprintf(buf, sizeof(buf), "<span color='#ff4444'>%lld ms</span>", (long long)ms);
    set_label_markup_threadsafe(ui.lbl_latency, buf);
}

void ui_update_receiver_count(int count)
{
    char buf[64];
    if (count == 0)
        snprintf(buf, sizeof(buf), "<span color='#888'>0</span>");
    else
        snprintf(buf, sizeof(buf), "<span color='#00ff88'>%d</span>", count);
    set_label_markup_threadsafe(ui.lbl_receivers, buf);
}

void ui_update_format_info(const char *sample_rate, const char *format)
{
    if (sample_rate) {
        char buf[128];
        snprintf(buf, sizeof(buf), "<span color='#00d4ff'>%s</span>", sample_rate);
        set_label_markup_threadsafe(ui.lbl_sample_rate, buf);
    }
    if (format) {
        char buf[300];
        snprintf(buf, sizeof(buf), "<span color='#b0b0b0'>%s</span>", format);
        set_label_markup_threadsafe(ui.lbl_format, buf);
    }
}

void ui_update_stats(int64_t kbps, int64_t total_bytes, int64_t elapsed_ms)
{
    /* Bitrate */
    char br[128];
    if (kbps > 1000)
        snprintf(br, sizeof(br),
                 "<span color='#00d4ff' font_weight='bold'>%.1f Mbps</span>",
                 kbps / 1000.0);
    else
        snprintf(br, sizeof(br),
                 "<span color='#00d4ff' font_weight='bold'>%lld kbps</span>",
                 (long long)kbps);
    set_label_markup_threadsafe(ui.lbl_bitrate, br);

    /* Total */
    char tot[128];
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
    set_label_threadsafe(ui.lbl_format, format_info ? format_info : "");
    set_visible_threadsafe(ui.stats_box, TRUE);
    set_visible_threadsafe(ui.chat_box, TRUE);
    set_label_threadsafe(ui.lbl_bitrate, "0 kbps");
    set_label_threadsafe(ui.lbl_total, "0 B");
    set_label_threadsafe(ui.lbl_duration, "00:00");
    set_label_markup_threadsafe(ui.lbl_receivers, "<span color='#888'>0</span>");
    set_label_markup_threadsafe(ui.lbl_latency,
                                "<span color='#888'>Measuring...</span>");
    set_label_markup_threadsafe(ui.lbl_connection,
                                "<span color='#ffcc00'>Waiting for receivers...</span>");
    set_sensitive_threadsafe(ui.btn_receive, FALSE);
    set_sensitive_threadsafe(ui.entry_ip, FALSE);
    set_sensitive_threadsafe(ui.combo_quality, FALSE);
    set_button_label_threadsafe(ui.btn_stream, "  Stop Streaming");
    swap_style_class(ui.btn_stream, "stop-btn", "stream-btn");
}

void ui_show_receiving(const char *server_ip)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "<span color='#00ff88'>Connected to %s</span>", server_ip);
    set_label_markup_threadsafe(ui.lbl_connection, buf);
    set_visible_threadsafe(ui.stats_box, TRUE);
    set_visible_threadsafe(ui.chat_box, TRUE);
    set_label_threadsafe(ui.lbl_bitrate, "0 kbps");
    set_label_threadsafe(ui.lbl_total, "0 B");
    set_label_threadsafe(ui.lbl_duration, "00:00");
    set_label_threadsafe(ui.lbl_receivers, "--");
    set_label_markup_threadsafe(ui.lbl_latency,
                                "<span color='#888'>Measuring...</span>");
    set_sensitive_threadsafe(ui.btn_stream, FALSE);
    set_sensitive_threadsafe(ui.entry_ip, FALSE);
    set_sensitive_threadsafe(ui.combo_quality, FALSE);
    set_button_label_threadsafe(ui.btn_receive, "  Stop Receiving");
    swap_style_class(ui.btn_receive, "stop-btn", "receive-btn");
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
    set_label_threadsafe(ui.lbl_sample_rate, "");
    set_label_threadsafe(ui.lbl_format, "");
    set_button_label_threadsafe(ui.btn_stream, "  Start Streaming");
    set_button_label_threadsafe(ui.btn_receive, "  Start Receiving");
    swap_style_class(ui.btn_stream, "stream-btn", "stop-btn");
    swap_style_class(ui.btn_receive, "receive-btn", "stop-btn");
}

void ui_add_chat_message(const char *sender, const char *text, int type)
{
    ChatUpdate *u = malloc(sizeof(*u));
    if (!u) return;
    snprintf(u->sender, sizeof(u->sender), "%s", sender ? sender : "");
    snprintf(u->message, sizeof(u->message), "%s", text ? text : "");
    u->type = type;
    g_idle_add(append_chat_idle, u);
}

/* ---- Chat callback ---- */

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
        return;
    }
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(ui.combo_quality));
    if (idx < 0) idx = 2;
    g_app.selected_preset = idx;
    streaming_start(idx);
}

static void on_receive_clicked(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    if (atomic_load(&g_app.is_receiving)) {
        receiving_stop();
        return;
    }
    const char *ip = gtk_entry_get_text(GTK_ENTRY(ui.entry_ip));
    if (!ip || strlen(ip) == 0) {
        ui_update_status("Enter streamer IP address");
        return;
    }
    receiving_start(ip);
}

static void on_chat_send(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(ui.chat_entry));
    if (!text || strlen(text) == 0) return;
    ui_add_chat_message("You", text, CHAT_TYPE_SENT);
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
    ui_update_status("IP address copied!");
}

static void on_quality_changed(GtkComboBox *combo, gpointer data)
{
    (void)data;
    int idx = gtk_combo_box_get_active(combo);
    if (idx < 0 || idx >= NUM_PRESETS) return;

    AudioConfig cfg;
    config_load_preset(&cfg, idx);

    char info[256], sr[64], fmt[256];
    config_sample_rate_string(&cfg, sr, sizeof(sr));
    config_format_string(&cfg, fmt, sizeof(fmt));
    snprintf(info, sizeof(info), "%s  |  %s  |  Buffer: %.1f ms",
             sr, cfg.channels == 1 ? "Mono" : "Stereo",
             config_buffer_latency_ms(&cfg));
    gtk_label_set_text(GTK_LABEL(ui.lbl_preset_info), info);
}

static void on_window_destroy(GtkWidget *w, gpointer data)
{
    (void)w; (void)data;
    streaming_stop();
    receiving_stop();
    gtk_main_quit();
}

/* ---- UI Builder helpers ---- */

static GtkWidget *make_label(const char *text, const char *css_class)
{
    GtkWidget *lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0);
    gtk_label_set_use_markup(GTK_LABEL(lbl), TRUE);
    if (css_class) {
        GtkStyleContext *sc = gtk_widget_get_style_context(lbl);
        gtk_style_context_add_class(sc, css_class);
    }
    return lbl;
}

static GtkWidget *make_card(void)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkStyleContext *sc = gtk_widget_get_style_context(frame);
    gtk_style_context_add_class(sc, "card");
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
    return frame;
}

static GtkWidget *make_stat_row(const char *icon, const char *title,
                                GtkWidget **value_label)
{
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(hbox, 4);
    gtk_widget_set_margin_end(hbox, 4);
    gtk_widget_set_margin_top(hbox, 3);
    gtk_widget_set_margin_bottom(hbox, 3);

    /* Icon + title */
    char title_markup[256];
    snprintf(title_markup, sizeof(title_markup),
             "<span color='#888888'>%s  %s</span>", icon, title);
    GtkWidget *lbl_title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl_title), title_markup);
    gtk_label_set_xalign(GTK_LABEL(lbl_title), 0);
    gtk_widget_set_size_request(lbl_title, 140, -1);

    /* Value */
    *value_label = gtk_label_new("--");
    gtk_label_set_xalign(GTK_LABEL(*value_label), 1);
    gtk_label_set_use_markup(GTK_LABEL(*value_label), TRUE);
    GtkStyleContext *sc = gtk_widget_get_style_context(*value_label);
    gtk_style_context_add_class(sc, "stat-value");

    gtk_box_pack_start(GTK_BOX(hbox), lbl_title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), *value_label, TRUE, TRUE, 0);

    return hbox;
}

static GtkWidget *make_separator(void)
{
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkStyleContext *sc = gtk_widget_get_style_context(sep);
    gtk_style_context_add_class(sc, "dim-separator");
    return sep;
}

/* ---- CSS Theme ---- */

static const char *CSS_THEME =
    /* Window */
    "window {"
    "  background-color: #0f0f1a;"
    "}"

    /* Labels */
    "label {"
    "  color: #d0d0d0;"
    "  font-family: 'Inter', 'Segoe UI', 'Noto Sans', sans-serif;"
    "  font-size: 12px;"
    "}"
    ".app-title {"
    "  font-size: 28px;"
    "  font-weight: 800;"
    "  color: #ffffff;"
    "  letter-spacing: 2px;"
    "}"
    ".app-subtitle {"
    "  font-size: 11px;"
    "  color: #555577;"
    "  letter-spacing: 1px;"
    "}"
    ".section-title {"
    "  font-size: 13px;"
    "  font-weight: 700;"
    "  color: #8888aa;"
    "  letter-spacing: 1px;"
    "}"
    ".ip-label {"
    "  font-size: 22px;"
    "  font-weight: 700;"
    "  color: #00e88f;"
    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
    "}"
    ".ip-label-dim {"
    "  font-size: 22px;"
    "  font-weight: 700;"
    "  color: #ff5555;"
    "}"
    ".status-text {"
    "  color: #666688;"
    "  font-size: 11px;"
    "}"
    ".stat-value {"
    "  color: #e0e0e0;"
    "  font-family: 'JetBrains Mono', 'Fira Code', monospace;"
    "  font-size: 13px;"
    "  font-weight: 600;"
    "}"
    ".preset-info {"
    "  color: #555577;"
    "  font-size: 10px;"
    "  font-family: monospace;"
    "}"

    /* Cards */
    ".card {"
    "  background-color: #16162a;"
    "  border-radius: 12px;"
    "  border: 1px solid #1e1e3a;"
    "  padding: 14px;"
    "}"

    /* Buttons */
    "button {"
    "  border-radius: 10px;"
    "  padding: 12px 20px;"
    "  font-weight: 700;"
    "  font-size: 13px;"
    "  border: none;"
    "  transition: all 200ms ease;"
    "}"
    "button:hover {"
    "  opacity: 0.9;"
    "}"
    ".stream-btn {"
    "  background: linear-gradient(135deg, #00cc66, #00aa55);"
    "  background-color: #00cc66;"
    "  color: #000000;"
    "  font-size: 14px;"
    "}"
    ".stream-btn:hover {"
    "  background-color: #00bb55;"
    "}"
    ".receive-btn {"
    "  background: linear-gradient(135deg, #0088ff, #0066dd);"
    "  background-color: #0088ff;"
    "  color: #ffffff;"
    "  font-size: 14px;"
    "}"
    ".receive-btn:hover {"
    "  background-color: #0077ee;"
    "}"
    ".stop-btn {"
    "  background-color: #ee3344;"
    "  color: #ffffff;"
    "  font-size: 14px;"
    "}"
    ".stop-btn:hover {"
    "  background-color: #dd2233;"
    "}"
    ".copy-btn {"
    "  background-color: #1e1e3a;"
    "  color: #8888aa;"
    "  padding: 6px 14px;"
    "  font-size: 11px;"
    "  border: 1px solid #2a2a4a;"
    "}"
    ".copy-btn:hover {"
    "  background-color: #2a2a4a;"
    "  color: #aaaacc;"
    "}"
    ".chat-send-btn {"
    "  background-color: #0088ff;"
    "  color: #ffffff;"
    "  padding: 8px 18px;"
    "  border-radius: 8px;"
    "}"

    /* Entry */
    "entry {"
    "  background-color: #1a1a30;"
    "  color: #d0d0d0;"
    "  border: 1px solid #2a2a4a;"
    "  border-radius: 8px;"
    "  padding: 10px 14px;"
    "  font-family: 'JetBrains Mono', monospace;"
    "  font-size: 13px;"
    "}"
    "entry:focus {"
    "  border-color: #0088ff;"
    "}"

    /* ComboBox */
    "combobox button {"
    "  background-color: #1a1a30;"
    "  color: #d0d0d0;"
    "  border: 1px solid #2a2a4a;"
    "  border-radius: 8px;"
    "  padding: 8px 12px;"
    "}"
    "combobox button:hover {"
    "  border-color: #3a3a5a;"
    "}"

    /* TextView (chat) */
    "textview, textview text {"
    "  background-color: #0d0d1a;"
    "  color: #b0b0c0;"
    "  font-family: 'JetBrains Mono', monospace;"
    "  font-size: 11px;"
    "}"

    /* Separator */
    ".dim-separator {"
    "  background-color: #1e1e3a;"
    "  min-height: 1px;"
    "}"

    /* Scrollbar */
    "scrollbar {"
    "  background-color: transparent;"
    "}"
    "scrollbar slider {"
    "  background-color: #2a2a4a;"
    "  border-radius: 4px;"
    "  min-width: 6px;"
    "}"
    "scrollbar slider:hover {"
    "  background-color: #3a3a5a;"
    "}"
;

/* ---- Build UI ---- */

static void build_ui(void)
{
    /* Window */
    ui.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(ui.window), "SoundShare");
    gtk_window_set_default_size(GTK_WINDOW(ui.window), 480, 780);
    gtk_window_set_position(GTK_WINDOW(ui.window), GTK_WIN_POS_CENTER);
    g_signal_connect(ui.window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    /* CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, CSS_THEME, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* Main scroll */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(ui.window), scroll);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);
    gtk_container_add(GTK_CONTAINER(scroll), vbox);

    /* ======== HEADER ======== */
    GtkWidget *title = make_label("SOUNDSHARE", "app-title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label("STREAM SYSTEM AUDIO OVER NETWORK", "app-subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), make_separator(), FALSE, FALSE, 8);

    /* ======== IP CARD ======== */
    GtkWidget *ip_card = make_card();
    GtkWidget *ip_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(ip_inner), 2);
    gtk_container_add(GTK_CONTAINER(ip_card), ip_inner);

    gtk_box_pack_start(GTK_BOX(ip_inner),
                       make_label("YOUR DEVICE", "section-title"), FALSE, FALSE, 0);

    GtkWidget *ip_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    ui.lbl_device_ip = make_label("...", "ip-label");
    gtk_box_pack_start(GTK_BOX(ip_row), ui.lbl_device_ip, TRUE, TRUE, 0);
    GtkWidget *btn_copy = gtk_button_new_with_label("COPY");
    GtkStyleContext *sc = gtk_widget_get_style_context(btn_copy);
    gtk_style_context_add_class(sc, "copy-btn");
    g_signal_connect(btn_copy, "clicked", G_CALLBACK(on_ip_copy), NULL);
    gtk_box_pack_end(GTK_BOX(ip_row), btn_copy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ip_inner), ip_row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), ip_card, FALSE, FALSE, 0);

    /* ======== QUALITY CARD ======== */
    GtkWidget *quality_card = make_card();
    GtkWidget *quality_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(quality_inner), 2);
    gtk_container_add(GTK_CONTAINER(quality_card), quality_inner);

    gtk_box_pack_start(GTK_BOX(quality_inner),
                       make_label("AUDIO QUALITY", "section-title"), FALSE, FALSE, 0);

    ui.combo_quality = gtk_combo_box_text_new();
    for (int i = 0; i < NUM_PRESETS; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui.combo_quality),
                                       QUALITY_NAMES[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(ui.combo_quality), 2);
    g_signal_connect(ui.combo_quality, "changed", G_CALLBACK(on_quality_changed), NULL);
    gtk_box_pack_start(GTK_BOX(quality_inner), ui.combo_quality, FALSE, FALSE, 0);

    ui.lbl_preset_info = make_label("48.0 kHz  |  Stereo  |  Buffer: 5.0 ms",
                                     "preset-info");
    gtk_box_pack_start(GTK_BOX(quality_inner), ui.lbl_preset_info, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), quality_card, FALSE, FALSE, 0);

    /* ======== CONNECT CARD ======== */
    GtkWidget *connect_card = make_card();
    GtkWidget *connect_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(connect_inner), 2);
    gtk_container_add(GTK_CONTAINER(connect_card), connect_inner);

    gtk_box_pack_start(GTK_BOX(connect_inner),
                       make_label("CONNECT", "section-title"), FALSE, FALSE, 0);

    ui.entry_ip = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui.entry_ip), "Enter streamer IP...");
    gtk_box_pack_start(GTK_BOX(connect_inner), ui.entry_ip, FALSE, FALSE, 0);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    ui.btn_stream = gtk_button_new_with_label("  Start Streaming");
    ui.btn_receive = gtk_button_new_with_label("  Start Receiving");

    sc = gtk_widget_get_style_context(ui.btn_stream);
    gtk_style_context_add_class(sc, "stream-btn");
    sc = gtk_widget_get_style_context(ui.btn_receive);
    gtk_style_context_add_class(sc, "receive-btn");

    g_signal_connect(ui.btn_stream, "clicked", G_CALLBACK(on_stream_clicked), NULL);
    g_signal_connect(ui.btn_receive, "clicked", G_CALLBACK(on_receive_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(btn_box), ui.btn_stream, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), ui.btn_receive, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(connect_inner), btn_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), connect_card, FALSE, FALSE, 0);

    /* Status */
    ui.lbl_status = make_label("Ready", "status-text");
    gtk_label_set_xalign(GTK_LABEL(ui.lbl_status), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), ui.lbl_status, FALSE, FALSE, 2);

    /* ======== STATS CARD ======== */
    GtkWidget *stats_card = make_card();
    ui.stats_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(ui.stats_box), 2);
    gtk_container_add(GTK_CONTAINER(stats_card), ui.stats_box);

    gtk_box_pack_start(GTK_BOX(ui.stats_box),
                       make_label("STREAM STATUS", "section-title"), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(ui.stats_box), make_separator(), FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xe2\x9a\xa1", "Sample Rate", &ui.lbl_sample_rate),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xf0\x9f\x8e\xb5", "Format", &ui.lbl_format),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box), make_separator(), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xf0\x9f\x93\xa1", "Bitrate", &ui.lbl_bitrate),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xf0\x9f\x93\xa6", "Data Sent", &ui.lbl_total),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xe2\x8f\xb1", "Duration", &ui.lbl_duration),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box), make_separator(), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xf0\x9f\x94\x97", "Connection", &ui.lbl_connection),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xf0\x9f\x91\xa5", "Receivers", &ui.lbl_receivers),
        FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.stats_box),
        make_stat_row("\xf0\x9f\x93\xb6", "Latency", &ui.lbl_latency),
        FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), stats_card, FALSE, FALSE, 0);

    /* ======== CHAT CARD ======== */
    GtkWidget *chat_card = make_card();
    ui.chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(ui.chat_box), 2);
    gtk_container_add(GTK_CONTAINER(chat_card), ui.chat_box);

    GtkWidget *chat_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(chat_header),
                       make_label("CHAT", "section-title"), FALSE, FALSE, 0);
    ui.lbl_chat_status = make_label("Offline", "status-text");
    gtk_label_set_xalign(GTK_LABEL(ui.lbl_chat_status), 1);
    gtk_box_pack_end(GTK_BOX(chat_header), ui.lbl_chat_status, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(ui.chat_box), chat_header, FALSE, FALSE, 0);

    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(chat_scroll, -1, 180);

    ui.chat_view = gtk_text_view_new();
    ui.chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(ui.chat_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(ui.chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ui.chat_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(ui.chat_view), 10);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(ui.chat_view), 10);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(ui.chat_view), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(ui.chat_view), 8);

    /* Chat text tags */
    gtk_text_buffer_create_tag(ui.chat_buffer, "system",
                               "foreground", "#555577",
                               "style", PANGO_STYLE_ITALIC, NULL);
    gtk_text_buffer_create_tag(ui.chat_buffer, "sent",
                               "foreground", "#00d4ff", NULL);
    gtk_text_buffer_create_tag(ui.chat_buffer, "received",
                               "foreground", "#00e88f", NULL);

    gtk_container_add(GTK_CONTAINER(chat_scroll), ui.chat_view);
    gtk_box_pack_start(GTK_BOX(ui.chat_box), chat_scroll, TRUE, TRUE, 0);

    GtkWidget *chat_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    ui.chat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui.chat_entry), "Type a message...");
    g_signal_connect(ui.chat_entry, "activate", G_CALLBACK(on_chat_send), NULL);
    ui.btn_chat_send = gtk_button_new_with_label("Send");
    sc = gtk_widget_get_style_context(ui.btn_chat_send);
    gtk_style_context_add_class(sc, "chat-send-btn");
    g_signal_connect(ui.btn_chat_send, "clicked", G_CALLBACK(on_chat_send), NULL);
    gtk_box_pack_start(GTK_BOX(chat_input_box), ui.chat_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chat_input_box), ui.btn_chat_send, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ui.chat_box), chat_input_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), chat_card, FALSE, FALSE, 0);

    /* ======== SET IP ======== */
    char ip[64];
    if (net_get_device_ip(ip, sizeof(ip)) == 0) {
        gtk_label_set_text(GTK_LABEL(ui.lbl_device_ip), ip);
    } else {
        gtk_label_set_text(GTK_LABEL(ui.lbl_device_ip), "Not connected");
        sc = gtk_widget_get_style_context(ui.lbl_device_ip);
        gtk_style_context_remove_class(sc, "ip-label");
        gtk_style_context_add_class(sc, "ip-label-dim");
    }

    /* Register chat callback */
    chat_set_callback(on_chat_received, NULL);

    /* Trigger initial preset info */
    on_quality_changed(GTK_COMBO_BOX(ui.combo_quality), NULL);

    /* Show and hide cards */
    gtk_widget_show_all(ui.window);
    gtk_widget_hide(stats_card);
    gtk_widget_hide(chat_card);

    /* Store card refs for show/hide - we use the parent cards */
    /* Override stats_box and chat_box to point to the cards */
    ui.stats_box = stats_card;
    ui.chat_box = chat_card;
}

int ui_run(int argc, char **argv)
{
    gtk_init(&argc, &argv);
    build_ui();
    gtk_main();
    return 0;
}