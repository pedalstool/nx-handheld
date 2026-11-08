#ifndef SAVESYNC_FS_H
#define SAVESYNC_FS_H

// The save tree on disk. Paths are relative to root (the Brick's Saves/).

#include <stddef.h>

typedef struct {
	char* path;
	long mtime; // unix seconds
	long size;
} SavesyncEntry;

typedef struct {
	SavesyncEntry* items;
	int count;
	int cap;
} SavesyncIndex;

void savesync_index_free(SavesyncIndex* ix);
const SavesyncEntry* savesync_index_find(const SavesyncIndex* ix, const char* path);

// Regular files only. Symlinks, *.savesync-tmp, and hidden names are skipped.
int savesync_scan(const char* root, SavesyncIndex* out);

int savesync_index_save(const SavesyncIndex* ix, const char* file);
int savesync_index_load(SavesyncIndex* ix, const char* file);

// Caller frees *out. mtime_out may be NULL.
int savesync_read_file(const char* root, const char* rel, unsigned char** out, long* size, long* mtime_out);
// Writes via a temp file in the same directory, then sets the source mtime.
// The receiver must keep that time: stamping "now" would make the copy look
// newer and bounce straight back.
int savesync_write_file(const char* root, const char* rel, const unsigned char* data, long size, long mtime);

#endif
