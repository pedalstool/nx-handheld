#include "savesync_net.h"

#include "savesync_plan.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define SAVESYNC_MAX_FILE (32L * 1024L * 1024L)
#define HEADER_CAP 8192

static int set_timeouts(int fd, int seconds) {
	struct timeval tv;
	tv.tv_sec = seconds;
	tv.tv_usec = 0;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
		return -1;
	if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0)
		return -1;
	return 0;
}

static int connect_host_ms(const char* host, int port, int timeout_ms) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
		close(fd);
		return -1;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		close(fd);
		return -1;
	}
	int rc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rc != 0 && errno != EINPROGRESS) {
		close(fd);
		return -1;
	}
	fd_set wset;
	FD_ZERO(&wset);
	FD_SET(fd, &wset);
	struct timeval tv;
	if (timeout_ms < 1)
		timeout_ms = 1;
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;
	rc = select(fd + 1, NULL, &wset, NULL, &tv);
	if (rc <= 0) {
		close(fd);
		return -1;
	}
	int soerr = 0;
	socklen_t sl = sizeof(soerr);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
		close(fd);
		return -1;
	}
	if (fcntl(fd, F_SETFL, flags) != 0) {
		close(fd);
		return -1;
	}
	set_timeouts(fd, 8);
	return fd;
}

static int connect_host(const char* host, int port) {
	return connect_host_ms(host, port, 2000);
}

int savesync_reachable(const char* host, int port) {
	int fd = connect_host_ms(host, port, 400);
	if (fd < 0)
		return 0;
	close(fd);
	return 1;
}

static int write_all(int fd, const void* data, size_t n) {
	const char* p = data;
	while (n) {
		ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (w == 0)
			return -1;
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

static int read_headers(int fd, char* buf, size_t cap, int* header_len, int* received, long* content_length, long* mtime) {
	size_t got = 0;
	*content_length = 0;
	*mtime = -1;
	*received = 0;
	while (got + 1 < cap) {
		ssize_t r = recv(fd, buf + got, cap - 1 - got, 0);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0)
			break;
		got += (size_t)r;
		buf[got] = '\0';
		char* end = strstr(buf, "\r\n\r\n");
		if (!end)
			continue;
		*header_len = (int)(end - buf) + 4;
		*content_length = -1; // until we see the header; -1 means "until close" only if absent
		char* line = buf;
		while (line < end) {
			char* nl = strstr(line, "\r\n");
			if (!nl || nl > end)
				break;
			*nl = '\0';
			if (strncmp(line, "Content-Length:", 15) == 0)
				*content_length = strtol(line + 15, NULL, 10);
			else if (strncmp(line, "X-Mtime:", 8) == 0)
				*mtime = strtol(line + 8, NULL, 10);
			*nl = '\r';
			line = nl + 2;
		}
		if (*content_length < 0)
			*content_length = 0;
		*received = (int)got;
		return 0;
	}
	return -1;
}

static int read_body(int fd, const char* already, int already_len, int header_len, long content_length, unsigned char** out) {
	if (content_length < 0 || content_length > SAVESYNC_MAX_FILE)
		return -1;
	unsigned char* buf = malloc((size_t)content_length + 1);
	if (!buf)
		return -1;
	int have = already_len - header_len;
	if (have < 0)
		have = 0;
	if (have > content_length)
		have = (int)content_length;
	if (have)
		memcpy(buf, already + header_len, (size_t)have);
	while (have < content_length) {
		ssize_t r = recv(fd, buf + have, (size_t)(content_length - have), 0);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			free(buf);
			return -1;
		}
		if (r == 0) {
			free(buf);
			return -1;
		}
		have += (int)r;
	}
	buf[content_length] = 0;
	*out = buf;
	return 0;
}

static int status_code(const char* headers) {
	const char* sp = strchr(headers, ' ');
	if (!sp)
		return -1;
	return (int)strtol(sp + 1, NULL, 10);
}

int savesync_fetch_index(const char* host, int port, SavesyncIndex* out) {
	memset(out, 0, sizeof(*out));
	int fd = connect_host(host, port);
	if (fd < 0)
		return -1;
	const char* req = "GET /index HTTP/1.0\r\nHost: savesync\r\nConnection: close\r\n\r\n";
	char headers[HEADER_CAP];
	int header_len = 0, received = 0;
	long content_length = 0, mtime = -1;
	unsigned char* body = NULL;
	int rc = -1;
	if (write_all(fd, req, strlen(req)) != 0)
		goto done;
	if (read_headers(fd, headers, sizeof(headers), &header_len, &received, &content_length, &mtime) != 0)
		goto done;
	if (status_code(headers) != 200)
		goto done;
	if (read_body(fd, headers, received, header_len, content_length, &body) != 0)
		goto done;
	char* text = (char*)body;
	char* save = NULL;
	for (char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		long mt = 0, sz = 0;
		char path[512];
		if (!savesync_parse_index_line(line, &mt, &sz, path, sizeof(path)))
			continue;
		// index_add is not exported. Rebuild through the load path? Append by
		// scanning into a file is wasteful. savesync_scan's adder is static.
		// Store by writing a memory list here via a local duplicate of add.
		if (out->count == out->cap) {
			int cap = out->cap ? out->cap * 2 : 32;
			SavesyncEntry* grown = realloc(out->items, (size_t)cap * sizeof(SavesyncEntry));
			if (!grown)
				goto done;
			out->items = grown;
			out->cap = cap;
		}
		char* copy = strdup(path);
		if (!copy)
			goto done;
		out->items[out->count].path = copy;
		out->items[out->count].mtime = mt;
		out->items[out->count].size = sz;
		out->count++;
	}
	rc = 0;
done:
	free(body);
	close(fd);
	if (rc != 0)
		savesync_index_free(out);
	return rc;
}

int savesync_download(const char* host, int port, const char* rel, unsigned char** data, long* size, long* mtime) {
	char enc[768];
	if (savesync_encode_path(rel, enc, sizeof(enc)) < 0)
		return -1;
	char req[1024];
	snprintf(req, sizeof(req), "GET /file?p=%s HTTP/1.0\r\nHost: savesync\r\nConnection: close\r\n\r\n", enc);
	int fd = connect_host(host, port);
	if (fd < 0)
		return -1;
	char headers[HEADER_CAP];
	int header_len = 0, received = 0;
	long content_length = 0, mt = -1;
	int rc = -1;
	if (write_all(fd, req, strlen(req)) != 0)
		goto done;
	if (read_headers(fd, headers, sizeof(headers), &header_len, &received, &content_length, &mt) != 0)
		goto done;
	if (status_code(headers) != 200)
		goto done;
	if (read_body(fd, headers, received, header_len, content_length, data) != 0)
		goto done;
	*size = content_length;
	if (mtime)
		*mtime = mt;
	rc = 0;
done:
	close(fd);
	return rc;
}

int savesync_upload(const char* host, int port, const char* rel, const unsigned char* data, long size, long mtime) {
	char enc[768];
	if (savesync_encode_path(rel, enc, sizeof(enc)) < 0 || size < 0)
		return -1;
	char head[1024];
	int head_n = snprintf(head, sizeof(head),
						  "PUT /file?p=%s HTTP/1.0\r\nHost: savesync\r\nContent-Length: %ld\r\nX-Mtime: %ld\r\nConnection: close\r\n\r\n",
						  enc, size, mtime);
	if (head_n < 0 || head_n >= (int)sizeof(head))
		return -1;
	int fd = connect_host(host, port);
	if (fd < 0)
		return -1;
	char headers[HEADER_CAP];
	int header_len = 0, received = 0;
	long content_length = 0, mt = -1;
	int rc = -1;
	if (write_all(fd, head, (size_t)head_n) != 0 || write_all(fd, data, (size_t)size) != 0)
		goto done;
	if (read_headers(fd, headers, sizeof(headers), &header_len, &received, &content_length, &mt) != 0)
		goto done;
	int code = status_code(headers);
	if (code == 204)
		rc = 0;
	else if (code == 409)
		rc = 1;
done:
	close(fd);
	return rc;
}

static void respond(int fd, int code, const char* reason, const char* extra, const unsigned char* body, long body_len) {
	char head[512];
	int n = snprintf(head, sizeof(head), "HTTP/1.0 %d %s\r\nContent-Length: %ld\r\nConnection: close\r\n%s\r\n",
					 code, reason, body_len, extra ? extra : "");
	if (n < 0 || n >= (int)sizeof(head))
		return;
	if (write_all(fd, head, (size_t)n) != 0)
		return;
	if (body && body_len)
		write_all(fd, body, (size_t)body_len);
}

static const char* query_param(const char* target, const char* key, char* out, size_t cap) {
	const char* q = strchr(target, '?');
	if (!q)
		return NULL;
	q++;
	size_t klen = strlen(key);
	while (*q) {
		if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
			q += klen + 1;
			size_t n = 0;
			while (q[n] && q[n] != '&' && q[n] != ' ')
				n++;
			if (n >= cap)
				return NULL;
			memcpy(out, q, n);
			out[n] = '\0';
			return out;
		}
		const char* amp = strchr(q, '&');
		if (!amp)
			return NULL;
		q = amp + 1;
	}
	return NULL;
}

static void handle_client(int fd, const char* root) {
	char buf[HEADER_CAP];
	int header_len = 0, received = 0;
	long content_length = 0, mtime = -1;
	if (read_headers(fd, buf, sizeof(buf), &header_len, &received, &content_length, &mtime) != 0)
		return;
	char method[8] = {0};
	char target[768] = {0};
	if (sscanf(buf, "%7s %767s", method, target) != 2) {
		respond(fd, 400, "Bad Request", NULL, NULL, 0);
		return;
	}
	char path_only[768];
	snprintf(path_only, sizeof(path_only), "%s", target);
	char* qq = strchr(path_only, '?');
	if (qq)
		*qq = '\0';

	if (strcmp(method, "GET") == 0 && strcmp(path_only, "/index") == 0) {
		SavesyncIndex ix;
		if (savesync_scan(root, &ix) != 0) {
			respond(fd, 500, "Error", NULL, NULL, 0);
			return;
		}
		size_t cap = 64;
		for (int i = 0; i < ix.count; i++)
			cap += strlen(ix.items[i].path) + 48;
		char* body = malloc(cap);
		if (!body) {
			savesync_index_free(&ix);
			respond(fd, 500, "Error", NULL, NULL, 0);
			return;
		}
		size_t o = 0;
		for (int i = 0; i < ix.count; i++) {
			int n = snprintf(body + o, cap - o, "%ld\t%ld\t%s\n", ix.items[i].mtime, ix.items[i].size, ix.items[i].path);
			if (n < 0 || (size_t)n >= cap - o)
				break;
			o += (size_t)n;
		}
		savesync_index_free(&ix);
		respond(fd, 200, "OK", "Content-Type: text/plain\r\n", (unsigned char*)body, (long)o);
		free(body);
		return;
	}

	char raw[700];
	char rel[512];
	if (!query_param(target, "p", raw, sizeof(raw)) || !savesync_decode_path(raw, rel, sizeof(rel))) {
		respond(fd, 400, "Bad Request", NULL, NULL, 0);
		return;
	}

	if (strcmp(method, "GET") == 0 && strcmp(path_only, "/file") == 0) {
		unsigned char* data = NULL;
		long size = 0, mt = 0;
		char local[512];
		if (savesync_cart_resolve(root, rel, local, sizeof(local)) != 0 ||
			savesync_read_file(root, local, &data, &size, &mt) != 0) {
			respond(fd, 404, "Not Found", NULL, NULL, 0);
			return;
		}
		char extra[64];
		snprintf(extra, sizeof(extra), "X-Mtime: %ld\r\n", mt);
		respond(fd, 200, "OK", extra, data, size);
		free(data);
		return;
	}

	if (strcmp(method, "PUT") == 0 && strcmp(path_only, "/file") == 0) {
		unsigned char* body = NULL;
		// Body bytes may already sit in buf past the header.
		if (read_body(fd, buf, received, header_len, content_length, &body) != 0) {
			respond(fd, 400, "Bad Request", NULL, NULL, 0);
			return;
		}
		struct stat st;
		char local[512];
		char full[512];
		long local_size = -1, local_mtime = 0;
		if (savesync_cart_resolve(root, rel, local, sizeof(local)) == 0 &&
			snprintf(full, sizeof(full), "%s/%s", root, local) < (int)sizeof(full) &&
			lstat(full, &st) == 0 && S_ISREG(st.st_mode)) {
			local_size = (long)st.st_size;
			local_mtime = (long)st.st_mtime;
		}
		SavesyncAction action = savesync_decide(local_size, local_mtime, content_length, mtime);
		if (action != SAVESYNC_PULL) {
			free(body);
			respond(fd, 409, "Conflict", NULL, NULL, 0);
			return;
		}
		if (mtime < 0)
			mtime = local_mtime + 1;
		if (savesync_cart_write(root, rel, body, content_length, mtime) != 0) {
			free(body);
			respond(fd, 500, "Error", NULL, NULL, 0);
			return;
		}
		free(body);
		respond(fd, 204, "No Content", NULL, NULL, 0);
		return;
	}

	respond(fd, 404, "Not Found", NULL, NULL, 0);
}

int savesync_serve(const char* root, int port) {
	int srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;
	int one = 1;
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((unsigned short)port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(srv, 4) != 0) {
		close(srv);
		return -1;
	}
	for (;;) {
		int fd = accept(srv, NULL, NULL);
		if (fd < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		set_timeouts(fd, 20);
		handle_client(fd, root);
		close(fd);
	}
	close(srv);
	return 0;
}
