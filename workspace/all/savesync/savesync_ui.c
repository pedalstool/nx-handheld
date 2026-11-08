#include "savesync_cmd.h"

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

#ifndef KEY_BACK
#define KEY_BACK 158
#endif

// 5x7 glyphs, the high bit of each row on the left. Enough for the setup screen.
static const unsigned char* glyph(char c) {
	static const unsigned char g[][7] = {
		[' '] = {0, 0, 0, 0, 0, 0, 0},
		['.'] = {0, 0, 0, 0, 0, 0x04, 0x04},
		['-'] = {0, 0, 0, 0x1F, 0, 0, 0},
		['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
		['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
		['2'] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F},
		['3'] = {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E},
		['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
		['5'] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
		['6'] = {0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E},
		['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
		['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
		['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
		['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
		['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
		['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
		['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
		['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
		['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
		['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E},
		['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
		['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
		['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
		['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
		['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
		['N'] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
		['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
		['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
		['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
		['S'] = {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E},
		['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
		['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
		['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
		['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
		['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
		['Y'] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
	};
	unsigned char uc = (unsigned char)c;
	if (uc < sizeof(g) / sizeof(g[0]) && (g[uc][0] || g[uc][1] || g[uc][2] || c == ' ' || c == '.' || c == '-'))
		return g[uc];
	if (c >= 'a' && c <= 'z')
		return glyph((char)(c - 32));
	static const unsigned char block[7] = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F};
	return block;
}

struct Fb {
	int fd;
	unsigned char* mem;
	size_t len;
	int w, h, stride, bpp;
	int yoff;
	int ro, go, bo;
};

static int fb_open(struct Fb* fb) {
	memset(fb, 0, sizeof(*fb));
	fb->fd = open("/dev/fb0", O_RDWR);
	if (fb->fd < 0)
		return -1;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fb->fd, FBIOGET_FSCREENINFO, &fix) != 0) {
		close(fb->fd);
		return -1;
	}
	fb->w = (int)var.xres;
	fb->h = (int)var.yres;
	fb->stride = (int)fix.line_length;
	fb->bpp = var.bits_per_pixel;
	fb->yoff = (int)var.yoffset;
	fb->ro = (int)(var.red.offset / 8);
	fb->go = (int)(var.green.offset / 8);
	fb->bo = (int)(var.blue.offset / 8);
	fb->len = (size_t)fix.line_length * var.yres_virtual;
	fb->mem = mmap(NULL, fb->len, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
	if (fb->mem == MAP_FAILED) {
		close(fb->fd);
		return -1;
	}
	if (fb->bpp != 32) {
		munmap(fb->mem, fb->len);
		close(fb->fd);
		return -1;
	}
	return 0;
}

static void fb_close(struct Fb* fb) {
	if (fb->mem && fb->mem != MAP_FAILED)
		munmap(fb->mem, fb->len);
	if (fb->fd >= 0)
		close(fb->fd);
}

static void putpx(struct Fb* fb, int x, int y, int r, int g, int b) {
	if (x < 0 || y < 0 || x >= fb->w || y >= fb->h)
		return;
	unsigned char* p = fb->mem + (size_t)(y + fb->yoff) * (size_t)fb->stride + (size_t)x * 4;
	p[fb->ro] = (unsigned char)r;
	p[fb->go] = (unsigned char)g;
	p[fb->bo] = (unsigned char)b;
	if (fb->ro != 3 && fb->go != 3 && fb->bo != 3)
		p[3] = 255;
}

static void fill(struct Fb* fb, int r, int g, int b) {
	unsigned char bytes[4] = {0, 0, 0, 0};
	bytes[fb->ro] = (unsigned char)r;
	bytes[fb->go] = (unsigned char)g;
	bytes[fb->bo] = (unsigned char)b;
	if (fb->ro != 3 && fb->go != 3 && fb->bo != 3)
		bytes[3] = 255;
	unsigned int pix;
	memcpy(&pix, bytes, 4);
	for (int y = 0; y < fb->h; y++) {
		unsigned int* row = (unsigned int*)(fb->mem + (size_t)(y + fb->yoff) * (size_t)fb->stride);
		for (int x = 0; x < fb->w; x++)
			row[x] = pix;
	}
}

static void text(struct Fb* fb, int x, int y, const char* s, int r, int g, int b, int scale) {
	for (int i = 0; s[i]; i++) {
		const unsigned char* rows = glyph(s[i]);
		for (int row = 0; row < 7; row++) {
			for (int col = 0; col < 5; col++) {
				if ((rows[row] >> (4 - col)) & 1) { // bit 4 is the left column
					for (int sy = 0; sy < scale; sy++)
						for (int sx = 0; sx < scale; sx++)
							putpx(fb, x + col * scale + sx, y + row * scale + sy, r, g, b);
				}
			}
		}
		x += 6 * scale;
	}
}

struct Pad {
	int fds[16];
	int n;
};

static int pad_open(struct Pad* pad) {
	memset(pad, 0, sizeof(*pad));
	for (int i = 0; i < 16; i++) {
		char path[64];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd >= 0)
			pad->fds[pad->n++] = fd;
	}
	return pad->n ? 0 : -1;
}

static void pad_close(struct Pad* pad) {
	for (int i = 0; i < pad->n; i++)
		close(pad->fds[i]);
}

// Returns 1 up, 2 down, 3 left, 4 right, 5 A, 6 B, or 0.
static int pad_poll(struct Pad* pad) {
	fd_set set;
	FD_ZERO(&set);
	int maxfd = -1;
	for (int i = 0; i < pad->n; i++) {
		FD_SET(pad->fds[i], &set);
		if (pad->fds[i] > maxfd)
			maxfd = pad->fds[i];
	}
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 150000;
	if (maxfd < 0 || select(maxfd + 1, &set, NULL, NULL, &tv) <= 0)
		return 0;
	for (int i = 0; i < pad->n; i++) {
		if (!FD_ISSET(pad->fds[i], &set))
			continue;
		struct input_event ev;
		while (read(pad->fds[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (ev.type == EV_KEY && (ev.value == 1 || ev.value == 2)) {
				int dir = ev.code == KEY_UP || ev.code == KEY_DOWN || ev.code == KEY_LEFT || ev.code == KEY_RIGHT;
				if (ev.value == 2 && !dir)
					continue;
				if (ev.code == BTN_EAST || ev.code == KEY_ENTER)
					return 5;
				if (ev.code == BTN_SOUTH || ev.code == KEY_ESC || ev.code == KEY_BACK)
					return 6;
				if (ev.code == KEY_UP)
					return 1;
				if (ev.code == KEY_DOWN)
					return 2;
				if (ev.code == KEY_LEFT)
					return 3;
				if (ev.code == KEY_RIGHT)
					return 4;
			}
			if (ev.type == EV_ABS && ev.value != 0) {
				if (ev.code == ABS_HAT0Y && ev.value < 0)
					return 1;
				if (ev.code == ABS_HAT0Y && ev.value > 0)
					return 2;
				if (ev.code == ABS_HAT0X && ev.value < 0)
					return 3;
				if (ev.code == ABS_HAT0X && ev.value > 0)
					return 4;
			}
		}
	}
	return 0;
}

static void draw(struct Fb* fb, const int oct[4], int cursor, const char* self, const char* status) {
	fill(fb, 18, 20, 24);
	text(fb, 48, 48, "SAVE SYNC", 220, 220, 220, 4);
	text(fb, 48, 130, "BRICK", 160, 170, 180, 3);
	text(fb, 280, 130, self[0] ? self : "NO WIFI", 220, 220, 220, 3);
	text(fb, 48, 190, "THOR", 160, 170, 180, 3);
	// Highlight the octet being edited by drawing it in pieces.
	int x = 280;
	for (int i = 0; i < 4; i++) {
		char piece[8];
		snprintf(piece, sizeof(piece), "%d", oct[i]);
		int r = 220, g = 220, b = 220;
		if (i == cursor) {
			r = 125;
			g = 220;
			b = 154;
		}
		text(fb, x, 190, piece, r, g, b, 3);
		x += (int)strlen(piece) * 18;
		if (i < 3) {
			text(fb, x, 190, ".", 220, 220, 220, 3);
			x += 18;
		}
	}
	text(fb, 48, 280, status[0] ? status : "A SYNC NOW", 220, 210, 170, 3);
	text(fb, 48, 360, "UP DOWN CHANGES THE NUMBER", 140, 150, 160, 2);
	text(fb, 48, 400, "LEFT RIGHT PICKS A GROUP", 140, 150, 160, 2);
	text(fb, 48, 440, "A SYNCS    B CLOSES", 140, 150, 160, 2);
	text(fb, 48, 480, "HELD MEANS PRESS A TO SEND IT", 140, 150, 160, 2);
	text(fb, 48, 560, "SAME FOLDERS AS SAVES ON THE BRICK", 140, 150, 160, 2);
}

int savesync_menu(const char* root, const char* conf) {
	struct Fb fb;
	if (fb_open(&fb) != 0) {
		fprintf(stderr, "savesync: no framebuffer\n");
		return 1;
	}
	struct Pad pad;
	pad_open(&pad);
	int oct[4] = {0, 0, 0, 0};
	char peer[32];
	if (savesync_load_peer(conf, peer, sizeof(peer)) == 0) {
		int a, b, c, d;
		if (sscanf(peer, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
			oct[0] = a;
			oct[1] = b;
			oct[2] = c;
			oct[3] = d;
		}
	}
	char self[32];
	if (savesync_self_ip(self, sizeof(self)) != 0)
		self[0] = '\0';
	char status[96] = "";
	FILE* status_file = fopen("/tmp/savesync.status", "r");
	if (status_file) {
		if (fgets(status, sizeof(status), status_file)) {
			size_t n = strlen(status);
			if (n && status[n - 1] == '\n')
				status[n - 1] = '\0';
		}
		fclose(status_file);
	}
	int cursor = 0;
	int dirty = 1;
	for (;;) {
		if (dirty) {
			draw(&fb, oct, cursor, self, status);
			dirty = 0;
		}
		int key = pad_poll(&pad);
		if (key == 1) {
			oct[cursor] = (oct[cursor] + 1) % 256;
			dirty = 1;
		} else if (key == 2) {
			oct[cursor] = (oct[cursor] + 255) % 256;
			dirty = 1;
		} else if (key == 3) {
			cursor = (cursor + 3) % 4;
			dirty = 1;
		} else if (key == 4) {
			cursor = (cursor + 1) % 4;
			dirty = 1;
		} else if (key == 6) {
			break;
		} else if (key == 5) {
			char ip[32];
			snprintf(ip, sizeof(ip), "%d.%d.%d.%d", oct[0], oct[1], oct[2], oct[3]);
			if (savesync_save_peer(conf, ip) != 0) {
				snprintf(status, sizeof(status), "SET THE THOR IP");
			} else {
				snprintf(status, sizeof(status), "SYNCING");
				draw(&fb, oct, cursor, self, status);
				savesync_sync_once(root, ip, status, sizeof(status));
			}
			dirty = 1;
		}
	}
	// Keep a finished address so the next game can find the Thor.
	char ip[32];
	snprintf(ip, sizeof(ip), "%d.%d.%d.%d", oct[0], oct[1], oct[2], oct[3]);
	savesync_save_peer(conf, ip);
	pad_close(&pad);
	fb_close(&fb);
	return 0;
}
