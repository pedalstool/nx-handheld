#ifndef SAVESYNC_CMD_H
#define SAVESYNC_CMD_H

#include <stddef.h>

#define SAVESYNC_PORT_N 27790

// peer is a dotted IPv4 address. 0 when the conf has none.
int savesync_load_peer(const char* conf, char* peer, size_t cap);
int savesync_save_peer(const char* conf, const char* peer);
int savesync_self_ip(char* out, size_t cap);

// Newer file wins, both directions. Writes one status line (no newline).
int savesync_sync_once(const char* root, const char* peer, char* status, size_t status_cap);

int savesync_menu(const char* root, const char* conf);

#endif
