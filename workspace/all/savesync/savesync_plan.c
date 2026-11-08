#include "savesync_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SavesyncAction savesync_decide(long local_size, long local_mtime, long remote_size, long remote_mtime) {
	bool local = local_size >= 0;
	bool remote = remote_size >= 0;
	if (!local && !remote)
		return SAVESYNC_SKIP;
	// An empty file is not a save. Prefer the side that actually has bytes,
	// whichever way the clocks point.
	if (local && local_size == 0 && remote && remote_size > 0)
		return SAVESYNC_PULL;
	if (remote && remote_size == 0 && local && local_size > 0)
		return SAVESYNC_PUSH;
	if (local && local_size == 0)
		return SAVESYNC_SKIP;
	if (remote && remote_size == 0)
		return SAVESYNC_SKIP;
	if (!remote)
		return SAVESYNC_PUSH;
	if (!local)
		return SAVESYNC_PULL;
	if (local_mtime > remote_mtime)
		return SAVESYNC_PUSH;
	if (remote_mtime > local_mtime)
		return SAVESYNC_PULL;
	return SAVESYNC_SKIP;
}

bool savesync_allow_upload(bool offline_begin, long base_mtime, long remote_mtime) {
	if (!offline_begin)
		return true;
	if (remote_mtime < 0)
		return true; // peer has nothing to protect
	// Peer has a save we did not download before playing. The local file may
	// be that older copy rewritten on the way out.
	if (base_mtime < 0)
		return false;
	if (remote_mtime > base_mtime)
		return false;
	return true;
}

bool savesync_path_ok(const char* rel) {
	if (!rel || !rel[0] || rel[0] == '/')
		return false;
	if (strlen(rel) > 480)
		return false;
	const char* p = rel;
	while (*p) {
		if ((unsigned char)*p < 0x20 || *p == '\\' || *p == '\x7f')
			return false;
		p++;
	}
	const char* seg = rel;
	for (;;) {
		const char* slash = strchr(seg, '/');
		size_t n = slash ? (size_t)(slash - seg) : strlen(seg);
		if (n == 0)
			return false;
		if (n == 1 && seg[0] == '.')
			return false;
		if (n == 2 && seg[0] == '.' && seg[1] == '.')
			return false;
		if (!slash)
			return true;
		seg = slash + 1;
	}
}

static int hex_nibble(int c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int savesync_encode_path(const char* rel, char* out, size_t cap) {
	if (!rel || !out || cap == 0)
		return -1;
	size_t o = 0;
	for (const unsigned char* p = (const unsigned char*)rel; *p; p++) {
		int raw = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '/' || *p == '-' || *p == '_' || *p == '.' || *p == '~';
		if (raw) {
			if (o + 1 >= cap)
				return -1;
			out[o++] = (char)*p;
		} else {
			if (o + 3 >= cap)
				return -1;
			static const char* hex = "0123456789ABCDEF";
			out[o++] = '%';
			out[o++] = hex[*p >> 4];
			out[o++] = hex[*p & 15];
		}
	}
	out[o] = '\0';
	return (int)o;
}

bool savesync_decode_path(const char* in, char* out, size_t cap) {
	if (!in || !out || cap == 0)
		return false;
	size_t o = 0;
	for (const char* p = in; *p; p++) {
		unsigned char c;
		if (*p == '%') {
			if (!p[1] || !p[2])
				return false;
			int hi = hex_nibble((unsigned char)p[1]);
			int lo = hex_nibble((unsigned char)p[2]);
			if (hi < 0 || lo < 0)
				return false;
			c = (unsigned char)((hi << 4) | lo);
			p += 2;
		} else if (*p == '+') {
			c = ' ';
		} else {
			c = (unsigned char)*p;
		}
		if (o + 1 >= cap)
			return false;
		out[o++] = (char)c;
	}
	out[o] = '\0';
	return savesync_path_ok(out);
}

bool savesync_parse_index_line(const char* line, long* mtime, long* size, char* path, size_t path_cap) {
	if (!line || !mtime || !size || !path)
		return false;
	char* end = NULL;
	long mt = strtol(line, &end, 10);
	if (end == line || *end != '\t')
		return false;
	char* end2 = NULL;
	long sz = strtol(end + 1, &end2, 10);
	if (end2 == end + 1 || *end2 != '\t')
		return false;
	const char* rel = end2 + 1;
	// Drop a trailing CR from an HTTP body.
	size_t n = strlen(rel);
	if (n && rel[n - 1] == '\r')
		n--;
	if (n == 0 || n >= path_cap)
		return false;
	memcpy(path, rel, n);
	path[n] = '\0';
	if (!savesync_path_ok(path) || sz < 0)
		return false;
	*mtime = mt;
	*size = sz;
	return true;
}
