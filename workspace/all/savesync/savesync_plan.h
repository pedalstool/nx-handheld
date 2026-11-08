#ifndef SAVESYNC_PLAN_H
#define SAVESYNC_PLAN_H

// Newer-file rule shared by the Brick pak and the Thor app. A missing file
// has size < 0. Times are unix seconds. No SDL, so the host tests can run it.

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	SAVESYNC_SKIP = 0,
	SAVESYNC_PUSH, // local is the one to keep
	SAVESYNC_PULL, // remote is the one to keep
} SavesyncAction;

// A 0-byte file never replaces a non-empty one: minarch can leave a 0 KB
// .srm behind, and that must not wipe the other device's real save.
SavesyncAction savesync_decide(long local_size, long local_mtime, long remote_size, long remote_mtime);

// False when a push would replace a peer save this session never saw.
// offline_begin: the peer could not be read before the game started.
// base_mtime < 0: the path was not on disk then. remote_mtime < 0: the peer
// has no such file. Playing from a stale copy rewrites the file with "now"
// as its time, which would otherwise look newer than the real save.
bool savesync_allow_upload(bool offline_begin, long base_mtime, long remote_mtime);

// Relative save path: no absolute path, no "..", no empty segment.
bool savesync_path_ok(const char* rel);

// Percent-encode for the query string. '/' is left as the separator.
// Returns bytes written excluding the NUL, or -1 if it does not fit.
int savesync_encode_path(const char* rel, char* out, size_t cap);
// Decode into out. False if the encoding is broken or the path is unsafe.
bool savesync_decode_path(const char* in, char* out, size_t cap);

// One index line: "<mtime>\t<size>\t<path>". False if the line is unusable.
bool savesync_parse_index_line(const char* line, long* mtime, long* size, char* path, size_t path_cap);

// Cart SRAM. "Name.sav", "Name.srm", and MinUI "Name.gba.sav" are one save.
// Writes the shared key (folder + rom name). False when rel is not one of those.
bool savesync_cart_key(const char* rel, char* out, size_t cap);

// 1 when a is the cart file to keep. An empty file loses. A newer time wins.
// The same time keeps .sav, which is the name the games load.
int savesync_cart_prefer(const char* path_a, long size_a, long mtime_a, const char* path_b, long size_b, long mtime_b);

#endif
