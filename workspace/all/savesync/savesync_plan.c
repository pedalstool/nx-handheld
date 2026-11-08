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

static int ends_ci(const char* s, const char* suf) {
	size_t n = strlen(s);
	size_t m = strlen(suf);
	if (n <= m)
		return 0;
	for (size_t i = 0; i < m; i++) {
		char a = s[n - m + i];
		if (a >= 'A' && a <= 'Z')
			a = (char)(a - 'A' + 'a');
		if (a != suf[i])
			return 0;
	}
	return 1;
}

bool savesync_is_save(const char* rel) {
	if (!rel || !rel[0])
		return false;
	const char* base = strrchr(rel, '/');
	base = base ? base + 1 : rel;
	return ends_ci(base, ".sav") || ends_ci(base, ".srm") || ends_ci(base, ".rtc");
}

static int tag_eq(const char* s, size_t n, const char* tag) {
	size_t m = strlen(tag);
	if (n != m)
		return 0;
	for (size_t i = 0; i < n; i++) {
		char a = s[i];
		if (a >= 'a' && a <= 'z')
			a = (char)(a - 'a' + 'A');
		if (a != tag[i])
			return 0;
	}
	return 1;
}

bool savesync_console_ok(const char* rel) {
	if (!rel || !rel[0])
		return false;
	const char* slash = strchr(rel, '/');
	if (!slash || slash == rel)
		return false;
	size_t n = (size_t)(slash - rel);
	return tag_eq(rel, n, "GB") || tag_eq(rel, n, "GBC") || tag_eq(rel, n, "GBA");
}

bool savesync_sync_path(const char* rel) {
	return savesync_is_save(rel) && savesync_console_ok(rel);
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

bool savesync_cart_key(const char* rel, char* out, size_t cap) {
	if (!rel || !out || cap < 2)
		return false;
	size_t n = strlen(rel);
	if (n + 1 > cap)
		return false;
	int sav = n > 4 && strcmp(rel + n - 4, ".sav") == 0;
	int srm = n > 4 && strcmp(rel + n - 4, ".srm") == 0;
	if (!sav && !srm)
		return false;
	n -= 4;
	memcpy(out, rel, n);
	out[n] = '\0';
	// MinUI keeps the rom extension: "Golden Sun.gba.sav" is the same cart as
	// "Golden Sun.sav".
	if (sav) {
		char* slash = strrchr(out, '/');
		char* base = slash ? slash + 1 : out;
		char* dot = strrchr(base, '.');
		if (dot && dot != base) {
			const char* ext = dot + 1;
			size_t el = strlen(ext);
			int ok = el >= 2 && el <= 4;
			for (size_t i = 0; ok && i < el; i++) {
				char c = ext[i];
				if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
					ok = 0;
			}
			if (ok)
				*dot = '\0';
		}
	}
	return out[0] != '\0';
}

int savesync_cart_prefer(const char* path_a, long size_a, long mtime_a, const char* path_b, long size_b, long mtime_b) {
	int a_empty = size_a <= 0;
	int b_empty = size_b <= 0;
	if (a_empty != b_empty)
		return b_empty ? 1 : 0;
	if (mtime_a != mtime_b)
		return mtime_a > mtime_b ? 1 : 0;
	int a_sav = path_a && strlen(path_a) >= 4 && strcmp(path_a + strlen(path_a) - 4, ".sav") == 0;
	int b_sav = path_b && strlen(path_b) >= 4 && strcmp(path_b + strlen(path_b) - 4, ".sav") == 0;
	if (a_sav != b_sav)
		return a_sav ? 1 : 0;
	return 0;
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
