#ifndef SAVESYNC_NET_H
#define SAVESYNC_NET_H

// Tiny HTTP/1.0 between the Brick and the Thor. Port SAVESYNC_PORT.
// upload returns 0 stored, 1 rejected because the peer's file is newer, -1 error.
// fetch and download return 0 or -1.

#include "savesync_fs.h"

#define SAVESYNC_PORT 27790

int savesync_serve(const char* root, int port);
int savesync_fetch_index(const char* host, int port, SavesyncIndex* out);
int savesync_download(const char* host, int port, const char* rel, unsigned char** data, long* size, long* mtime);
int savesync_upload(const char* host, int port, const char* rel, const unsigned char* data, long size, long mtime);

#endif
