#include "savesync_fs.h"

#include "savesync_plan.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SAVESYNC_MAX_FILE (32L * 1024L * 1024L)

void savesync_index_free(SavesyncIndex* ix) {
	if (!ix)
		return;
	for (int i = 0; i < ix->count; i++)
		free(ix->items[i].path);
	free(ix->items);
	ix->items = NULL;
	ix->count = 0;
	ix->cap = 0;
}

const SavesyncEntry* savesync_index_find(const SavesyncIndex* ix, const char* path) {
	if (!ix || !path)
		return NULL;
	for (int i = 0; i < ix->count; i++) {
		if (strcmp(ix->items[i].path, path) == 0)
			return &ix->items[i];
	}
	return NULL;
}

static int index_add(SavesyncIndex* ix, const char* path, long mtime, long size) {
	if (ix->count == ix->cap) {
		int cap = ix->cap ? ix->cap * 2 : 32;
		SavesyncEntry* grown = realloc(ix->items, (size_t)cap * sizeof(SavesyncEntry));
		if (!grown)
			return -1;
		ix->items = grown;
		ix->cap = cap;
	}
	char* copy = strdup(path);
	if (!copy)
		return -1;
	ix->items[ix->count].path = copy;
	ix->items[ix->count].mtime = mtime;
	ix->items[ix->count].size = size;
	ix->count++;
	return 0;
}

static int hidden_name(const char* name) {
	return name[0] == '.';
}

static int is_tmp_name(const char* name) {
	size_t n = strlen(name);
	const char* suf = ".savesync-tmp";
	size_t sn = strlen(suf);
	return n > sn && strcmp(name + n - sn, suf) == 0;
}

static int scan_dir(const char* root, const char* rel, SavesyncIndex* out) {
	char full[512];
	if (rel[0]) {
		if (snprintf(full, sizeof(full), "%s/%s", root, rel) >= (int)sizeof(full))
			return -1;
	} else if (snprintf(full, sizeof(full), "%s", root) >= (int)sizeof(full)) {
		return -1;
	}
	DIR* dir = opendir(full);
	if (!dir)
		return 0; // a missing Saves/ is an empty tree, not a failure
	struct dirent* ent;
	int rc = 0;
	while ((ent = readdir(dir)) != NULL) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		if (hidden_name(ent->d_name) || is_tmp_name(ent->d_name))
			continue;
		char child[512];
		if (rel[0]) {
			if (snprintf(child, sizeof(child), "%s/%s", rel, ent->d_name) >= (int)sizeof(child)) {
				rc = -1;
				break;
			}
		} else if (snprintf(child, sizeof(child), "%s", ent->d_name) >= (int)sizeof(child)) {
			rc = -1;
			break;
		}
		if (!savesync_path_ok(child))
			continue;
		char child_full[512];
		if (snprintf(child_full, sizeof(child_full), "%s/%s", root, child) >= (int)sizeof(child_full)) {
			rc = -1;
			break;
		}
		struct stat st;
		if (lstat(child_full, &st) != 0)
			continue;
		if (S_ISLNK(st.st_mode))
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (scan_dir(root, child, out) != 0) {
				rc = -1;
				break;
			}
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue;
		if (index_add(out, child, (long)st.st_mtime, (long)st.st_size) != 0) {
			rc = -1;
			break;
		}
	}
	closedir(dir);
	return rc;
}

int savesync_scan(const char* root, SavesyncIndex* out) {
	memset(out, 0, sizeof(*out));
	if (!root || !root[0])
		return -1;
	return scan_dir(root, "", out);
}

int savesync_index_save(const SavesyncIndex* ix, const char* file) {
	char tmp[512];
	if (snprintf(tmp, sizeof(tmp), "%s.savesync-tmp", file) >= (int)sizeof(tmp))
		return -1;
	FILE* f = fopen(tmp, "w");
	if (!f)
		return -1;
	for (int i = 0; i < ix->count; i++) {
		if (fprintf(f, "%ld\t%ld\t%s\n", ix->items[i].mtime, ix->items[i].size, ix->items[i].path) < 0) {
			fclose(f);
			unlink(tmp);
			return -1;
		}
	}
	if (fclose(f) != 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, file) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

int savesync_index_load(SavesyncIndex* ix, const char* file) {
	memset(ix, 0, sizeof(*ix));
	FILE* f = fopen(file, "r");
	if (!f)
		return -1;
	char line[640];
	while (fgets(line, sizeof(line), f)) {
		long mt = 0, sz = 0;
		char path[512];
		size_t n = strlen(line);
		if (n && line[n - 1] == '\n')
			line[n - 1] = '\0';
		if (!savesync_parse_index_line(line, &mt, &sz, path, sizeof(path)))
			continue;
		if (index_add(ix, path, mt, sz) != 0) {
			fclose(f);
			savesync_index_free(ix);
			return -1;
		}
	}
	fclose(f);
	return 0;
}

static int full_path(const char* root, const char* rel, char* out, size_t cap) {
	if (!savesync_path_ok(rel))
		return -1;
	if (snprintf(out, cap, "%s/%s", root, rel) >= (int)cap)
		return -1;
	return 0;
}

int savesync_read_file(const char* root, const char* rel, unsigned char** out, long* size, long* mtime_out) {
	char path[512];
	if (full_path(root, rel, path, sizeof(path)) != 0)
		return -1;
	struct stat st;
	if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return -1;
	if (st.st_size < 0 || st.st_size > SAVESYNC_MAX_FILE)
		return -1;
	FILE* f = fopen(path, "rb");
	if (!f)
		return -1;
	unsigned char* buf = malloc((size_t)st.st_size + 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	size_t got = fread(buf, 1, (size_t)st.st_size, f);
	fclose(f);
	if (got != (size_t)st.st_size) {
		free(buf);
		return -1;
	}
	*out = buf;
	*size = (long)got;
	if (mtime_out)
		*mtime_out = (long)st.st_mtime;
	return 0;
}

static int mkdir_parents(const char* file_path) {
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", file_path);
	for (char* p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
				*p = '/';
				return -1;
			}
			*p = '/';
		}
	}
	return 0;
}

int savesync_write_file(const char* root, const char* rel, const unsigned char* data, long size, long mtime) {
	if (!data || size < 0 || size > SAVESYNC_MAX_FILE)
		return -1;
	char path[512];
	if (full_path(root, rel, path, sizeof(path)) != 0)
		return -1;
	if (mkdir_parents(path) != 0)
		return -1;
	char tmp[540];
	if (snprintf(tmp, sizeof(tmp), "%s.savesync-tmp", path) >= (int)sizeof(tmp))
		return -1;
	FILE* f = fopen(tmp, "wb");
	if (!f)
		return -1;
	size_t put = fwrite(data, 1, (size_t)size, f);
	if (fclose(f) != 0 || put != (size_t)size) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	struct timespec ts[2];
	ts[0].tv_sec = mtime;
	ts[0].tv_nsec = 0;
	ts[1].tv_sec = mtime;
	ts[1].tv_nsec = 0;
	// Best effort: a filesystem that ignores it still has the bytes, and the
	// next compare may bounce once. exFAT on the card does honor mtime.
	utimensat(AT_FDCWD, path, ts, 0);
	return 0;
}
