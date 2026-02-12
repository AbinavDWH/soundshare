#ifndef CHAT_H
#define CHAT_H

#include "soundshare.h"

#define CHAT_MAX_SENDER  256
#define CHAT_MAX_MSG    4096

/* Callback invoked on the GTK thread when a message arrives */
typedef void (*ChatMessageCallback)(const char *sender,
                                    const char *message,
                                    void *user_data);

/* Set the receive callback (UI sets this) */
void chat_set_callback(ChatMessageCallback cb, void *user_data);

/* ---- server (sender side) ---- */
int  chat_server_start(void);
void chat_server_stop(void);
void chat_server_broadcast(const char *sender, const char *message);

/* ---- client (receiver side) ---- */
int  chat_client_start(const char *server_ip);
void chat_client_stop(void);
void chat_client_send(const char *sender, const char *message);

#endif /* CHAT_H */