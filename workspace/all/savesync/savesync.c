#include "savesync_cmd.h"

#include "savesync_fs.h"
#include "savesync_net.h"
#include "savesync_plan.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BASE_PATH "/tmp/savesync.base"
#define BEGAN_PATH "/tmp/savesync.began"
#define HOLD_PATH "/tmp/savesync.hold"
#define STATUS_PATH "/tmp/savesync.status"
#define PID_PATH "/tmp/savesync.pid"

static void write_status(const char* line) {
	FILE* f = fopen(STATUS_PATH, "w");
	if (!f)
		return;
	fputs(line, f);
	fputc('\n', f);
	fclose(f);
}

static int ipv4_ok(const char* s) {
	struct in_addr addr;
	return s && inet_pton(AF_INET, s, &addr) == 1;
}

int savesync_load_peer(const char* conf, char* peer, size_t cap) {
	if (peer && cap)
		peer[0] = '\0';
	FILE* f = fopen(conf, "r");
	if (!f)
		return -1;
	char line[128];
	int ok = -1;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "peer ", 5) != 0 && strncmp(line, "peer\t", 5) != 0)
			continue;
		char* ip = line + 5;
		while (*ip == ' ' || *ip == '\t')
			ip++;
		char* end = ip;
		while (*end && *end != '\n' && *end != '\r' && *end != ' ')
			end++;
		*end = '\0';
		if (!ipv4_ok(ip) || strcmp(ip, "0.0.0.0") == 0)
			continue;
		snprintf(peer, cap, "%s", ip);
		ok = 0;
		break;
	}
	fclose(f);
	return ok;
}

int savesync_save_peer(const char* conf, const char* peer) {
	if (!ipv4_ok(peer) || strcmp(peer, "0.0.0.0") == 0)
		return -1;
	char tmp[512];
	if (snprintf(tmp, sizeof(tmp), "%s.savesync-tmp", conf) >= (int)sizeof(tmp))
		return -1;
	FILE* f = fopen(tmp, "w");
	if (!f)
		return -1;
	fprintf(f, "peer %s\n", peer);
	if (fclose(f) != 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, conf) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

int savesync_self_ip(char* out, size_t cap) {
	if (out && cap)
		out[0] = '\0';
	struct ifaddrs* list = NULL;
	if (getifaddrs(&list) != 0)
		return -1;
	int found = -1;
	for (struct ifaddrs* it = list; it; it = it->ifa_next) {
		if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET)
			continue;
		if (it->ifa_name && strcmp(it->ifa_name, "lo") == 0)
			continue;
		char ip[32];
		const struct sockaddr_in* in = (const struct sockaddr_in*)it->ifa_addr;
		if (!inet_ntop(AF_INET, &in->sin_addr, ip, sizeof(ip)))
			continue;
		if (strncmp(ip, "169.254.", 8) == 0)
			continue;
		snprintf(out, cap, "%s", ip);
		found = 0;
		break;
	}
	freeifaddrs(list);
	return found;
}

static int same_cart(const char* a, const char* b) {
	if (strcmp(a, b) == 0)
		return 1;
	char ka[512], kb[512];
	return savesync_cart_key(a, ka, sizeof(ka)) && savesync_cart_key(b, kb, sizeof(kb)) && strcmp(ka, kb) == 0;
}

static const SavesyncEntry* best_cart(const SavesyncIndex* ix, const char* path) {
	char key[512];
	if (!savesync_cart_key(path, key, sizeof(key)))
		return savesync_index_find(ix, path);
	const SavesyncEntry* best = NULL;
	for (int i = 0; i < ix->count; i++) {
		char other[512];
		if (!savesync_cart_key(ix->items[i].path, other, sizeof(other)) || strcmp(other, key) != 0)
			continue;
		if (!best || savesync_cart_prefer(ix->items[i].path, ix->items[i].size, ix->items[i].mtime, best->path, best->size, best->mtime))
			best = &ix->items[i];
	}
	return best;
}

static int path_held(const char* path) {
	FILE* f = fopen(HOLD_PATH, "r");
	if (!f)
		return 0;
	char line[512];
	int found = 0;
	while (fgets(line, sizeof(line), f)) {
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (same_cart(line, path)) {
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

static void remember_hold(const char* path) {
	if (path_held(path))
		return;
	FILE* f = fopen(HOLD_PATH, "a");
	if (!f)
		return;
	fprintf(f, "%s\n", path);
	fclose(f);
}

static void clear_holds(void) {
	unlink(HOLD_PATH);
}

// want_push / want_pull select a direction. A pre-game sync only pulls, so a
// stale local timestamp cannot overwrite the Thor before the game starts.
static void apply_transfers(const char* root, const char* peer, const SavesyncIndex* local, const SavesyncIndex* remote, bool guard, bool offline, const SavesyncIndex* base, int want_push, int want_pull, int* pushed, int* pulled, int* held) {
	if (want_push) {
	for (int i = 0; i < local->count; i++) {
		const SavesyncEntry* e = &local->items[i];
		if (!savesync_sync_path(e->path) || best_cart(local, e->path) != e)
			continue;
		const SavesyncEntry* r = best_cart(remote, e->path);
		long rsize = r ? r->size : -1;
		long rmtime = r ? r->mtime : -1;
		if (savesync_decide(e->size, e->mtime, rsize, rmtime) != SAVESYNC_PUSH)
			continue;
		long base_mtime = -1;
		if (base) {
			const SavesyncEntry* b = best_cart(base, e->path);
			if (b)
				base_mtime = b->mtime;
		}
		// A held path stays held after a later successful check. Otherwise the
		// rewrite from the offline session would upload once the Thor is back.
		if (guard && (!savesync_allow_upload(offline, base_mtime, rmtime) || path_held(e->path))) {
			(*held)++;
			remember_hold(e->path);
			continue;
		}
		unsigned char* data = NULL;
		long size = 0, mt = 0;
		if (savesync_read_file(root, e->path, &data, &size, &mt) != 0) {
			continue;
		}
		const char* dest = r ? r->path : e->path;
		int up = savesync_upload(peer, SAVESYNC_PORT, dest, data, size, mt);
		free(data);
		if (up == 0)
			(*pushed)++;
		else if (up == 1)
			(*held)++;
	}
	}
	if (want_pull) {
	for (int i = 0; i < remote->count; i++) {
		const SavesyncEntry* e = &remote->items[i];
		if (!savesync_sync_path(e->path) || best_cart(remote, e->path) != e)
			continue;
		const SavesyncEntry* loc = best_cart(local, e->path);
		long lsize = loc ? loc->size : -1;
		long lmtime = loc ? loc->mtime : 0;
		if (savesync_decide(lsize, lmtime, e->size, e->mtime) != SAVESYNC_PULL)
			continue;
		unsigned char* data = NULL;
		long size = 0, mt = e->mtime;
		if (savesync_download(peer, SAVESYNC_PORT, e->path, &data, &size, &mt) != 0)
			continue;
		if (mt < 0)
			mt = e->mtime;
		// The peer may have moved on between the index and the download.
		if (savesync_decide(lsize, lmtime, size, mt) != SAVESYNC_PULL) {
			free(data);
			continue;
		}
		const char* dest = loc ? loc->path : e->path;
		if (savesync_cart_write(root, dest, data, size, mt) == 0)
			(*pulled)++;
		free(data);
	}
	}
}

static int read_began_offline(void) {
	FILE* f = fopen(BEGAN_PATH, "r");
	if (!f)
		return 0;
	char buf[16] = {0};
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return strncmp(buf, "offline", 7) == 0;
}

static void write_began(int offline) {
	FILE* f = fopen(BEGAN_PATH, "w");
	if (!f)
		return;
	fputs(offline ? "offline\n" : "ok\n", f);
	fclose(f);
}

// Pulls first when asked, so a game starts from the newest save. guard arms
// the "don't upload a rewrite of a save we never saw" check for the push
// that runs after the game.
static int reconcile(const char* root, const char* peer, int do_pull, int do_push, int guard, char* status, size_t status_cap) {
	int pushed = 0, pulled = 0, held = 0;
	SavesyncIndex local, remote, base;
	memset(&local, 0, sizeof(local));
	memset(&remote, 0, sizeof(remote));
	memset(&base, 0, sizeof(base));
	int have_base = 0;
	if (savesync_scan(root, &local) != 0) {
		snprintf(status, status_cap, "CANNOT READ SAVES");
		write_status(status);
		return -1;
	}
	if (do_pull) {
		// Snapshot what is on disk before we talk to the peer. If the peer
		// is down, this is the save the game will actually start from.
		savesync_index_save(&local, BASE_PATH);
	}
	if (do_pull) {
		if (savesync_fetch_index(peer, SAVESYNC_PORT, &remote) != 0) {
			write_began(1);
			savesync_index_free(&local);
			snprintf(status, status_cap, "THOR UNREACHABLE");
			write_status(status);
			return -1;
		}
		// Download only. Uploading here would send a save the game has not
		// played yet, including a rewrite from a session that never saw the Thor.
		apply_transfers(root, peer, &local, &remote, 0, 0, NULL, 0, 1, &pushed, &pulled, &held);
		savesync_index_free(&local);
		memset(&local, 0, sizeof(local));
		savesync_scan(root, &local);
		savesync_index_save(&local, BASE_PATH);
		write_began(0);
		savesync_index_free(&local);
		savesync_index_free(&remote);
		if (held)
			snprintf(status, status_cap, "SENT %d  GOT %d  HELD %d", pushed, pulled, held);
		else
			snprintf(status, status_cap, "SENT %d  GOT %d", pushed, pulled);
		write_status(status);
		return 0;
	}
	if (do_push) {
		int offline = guard && read_began_offline();
		if (guard && savesync_index_load(&base, BASE_PATH) == 0)
			have_base = 1;
		savesync_index_free(&local);
		savesync_scan(root, &local);
		if (savesync_fetch_index(peer, SAVESYNC_PORT, &remote) != 0) {
			savesync_index_free(&local);
			savesync_index_free(&base);
			snprintf(status, status_cap, "THOR UNREACHABLE");
			write_status(status);
			return -1;
		}
		// Take a newer Thor save first, then send. After the download the
		// local copy carries the Thor's time, so it is not sent straight back.
		apply_transfers(root, peer, &local, &remote, 0, 0, NULL, 0, 1, &pushed, &pulled, &held);
		savesync_index_free(&local);
		memset(&local, 0, sizeof(local));
		savesync_scan(root, &local);
		apply_transfers(root, peer, &local, &remote, guard, offline, have_base ? &base : NULL, 1, 0, &pushed, &pulled, &held);
	}
	savesync_index_free(&local);
	savesync_index_free(&remote);
	savesync_index_free(&base);
	if (held)
		snprintf(status, status_cap, "SENT %d  GOT %d  HELD %d", pushed, pulled, held);
	else
		snprintf(status, status_cap, "SENT %d  GOT %d", pushed, pulled);
	write_status(status);
	(void)have_base;
	return 0;
}

static void add_counts(const char* status, int* sent, int* got, int* held) {
	int s = 0, g = 0, h = 0;
	if (sscanf(status, "SENT %d GOT %d HELD %d", &s, &g, &h) < 2)
		return;
	*sent += s;
	*got += g;
	*held += h;
}

int savesync_sync_once(const char* root, const char* peer, char* status, size_t status_cap) {
	// Pull while the guard is still in place, then drop it. A is an explicit
	// sync: the newer file is sent, including one a previous exit held back.
	int rc = reconcile(root, peer, 1, 0, 0, status, status_cap);
	if (rc != 0)
		return rc;
	int sent = 0, got = 0, held = 0;
	add_counts(status, &sent, &got, &held);
	clear_holds();
	rc = reconcile(root, peer, 0, 1, 0, status, status_cap);
	if (rc != 0)
		return rc;
	add_counts(status, &sent, &got, &held);
	if (held)
		snprintf(status, status_cap, "SENT %d  GOT %d  HELD %d", sent, got, held);
	else
		snprintf(status, status_cap, "SENT %d  GOT %d", sent, got);
	write_status(status);
	return 0;
}

static int peer_or_status(const char* conf, char* peer, size_t cap, char* status, size_t status_cap) {
	if (savesync_load_peer(conf, peer, cap) != 0) {
		snprintf(status, status_cap, "NO THOR IP");
		write_status(status);
		return -1;
	}
	return 0;
}

static const char* conf_path(void) {
	const char* env = getenv("SAVESYNC_CONF");
	if (env && env[0])
		return env;
	const char* userdata = getenv("USERDATA_PATH");
	static char buf[512];
	if (userdata && userdata[0]) {
		snprintf(buf, sizeof(buf), "%s/savesync.conf", userdata);
		return buf;
	}
	return "/mnt/SDCARD/.userdata/tg5040/savesync.conf";
}

static const char* root_path(void) {
	const char* env = getenv("SAVESYNC_ROOT");
	if (env && env[0])
		return env;
	const char* saves = getenv("SAVES_PATH");
	if (saves && saves[0])
		return saves;
	return "/mnt/SDCARD/Saves";
}

static int pid_alive(void) {
	FILE* f = fopen(PID_PATH, "r");
	if (!f)
		return 0;
	int pid = 0;
	int n = fscanf(f, "%d", &pid);
	fclose(f);
	return n == 1 && pid > 0 && kill(pid, 0) == 0;
}

static int cmd_serve(const char* root) {
	if (pid_alive())
		return 0;
	pid_t pid = fork();
	if (pid < 0)
		return 1;
	if (pid > 0)
		return 0;
	setsid();
	FILE* f = fopen(PID_PATH, "w");
	if (f) {
		fprintf(f, "%d\n", (int)getpid());
		fclose(f);
	}
	int devnull = open("/dev/null", O_RDWR);
	if (devnull >= 0) {
		dup2(devnull, 0);
		dup2(devnull, 1);
		dup2(devnull, 2);
		if (devnull > 2)
			close(devnull);
	}
	return savesync_serve(root, SAVESYNC_PORT) == 0 ? 0 : 1;
}

static void usage(void) {
	fprintf(stderr, "usage: savesync serve|pull|push|once|menu\n");
}

int main(int argc, char** argv) {
	const char* root = root_path();
	const char* conf = conf_path();
	const char* cmd = argc > 1 ? argv[1] : "menu";
	char peer[32];
	char status[96];
	if (strcmp(cmd, "serve") == 0)
		return cmd_serve(root);
	if (strcmp(cmd, "menu") == 0)
		return savesync_menu(root, conf);
	if (strcmp(cmd, "pull") != 0 && strcmp(cmd, "push") != 0 && strcmp(cmd, "once") != 0) {
		usage();
		return 1;
	}
	if (peer_or_status(conf, peer, sizeof(peer), status, sizeof(status)) != 0)
		return 0;
	int rc;
	if (strcmp(cmd, "pull") == 0)
		rc = reconcile(root, peer, 1, 0, 0, status, sizeof(status));
	else if (strcmp(cmd, "push") == 0)
		rc = reconcile(root, peer, 0, 1, 1, status, sizeof(status));
	else
		rc = savesync_sync_once(root, peer, status, sizeof(status));
	fprintf(stderr, "%s\n", status);
	// A down Thor must not stop the game from launching.
	return rc == 0 || strcmp(status, "THOR UNREACHABLE") == 0 ? 0 : 1;
}
