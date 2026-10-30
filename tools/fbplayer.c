/*
 * fbplayer - draw Bad Apple ASCII frames straight to the framebuffer.
 *
 * The TWRP/OrangeFox installer console only appends lines: ui_print can never
 * clear it, so a per-frame animation accumulates in the scroll list and the
 * user has to chase it.  This player bypasses the console completely.  It
 * blanks the screen for every frame and draws all 19 rows at once, so the
 * picture is replaced instead of appended.
 *
 * The caller is responsible for stopping the recovery GUI (SIGSTOP) before
 * running this, and for resuming it (SIGCONT) afterwards.  Foxbrowse, which
 * ships with the begonia device tree, uses the same trick.
 *
 * Usage:
 *   fbplayer [frames.txt] [fps]
 *   fbplayer frames.txt --still 1234      draw one frame and exit
 *
 * If BA_FB_FILE is set the framebuffer is emulated with a regular file and the
 * fb0 ioctls are skipped, which makes local testing possible:
 *   BA_FB_FILE=/tmp/fb.raw BA_FB_W=1080 BA_FB_H=2340 fbplayer frames.txt --still 900
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "font.h"

#define MAX_COLS 128
#define MAX_ROWS 64

static int fb_fd = -1;
static unsigned char *fb_map = NULL;
static size_t fb_map_len = 0;
static int fb_w = 0;
static int fb_h = 0;
static int fb_stride = 0;
static int fb_bpp = 32;
static int fb_rshift = 16;
static int fb_gshift = 8;
static int fb_bshift = 0;
static int fb_pages = 1;
static int fb_page = 0;
static int fb_live = 0;
static int probe_only = 0;
static struct fb_var_screeninfo fb_var;

static char frame_rows[MAX_ROWS][MAX_COLS + 1];

static void die(const char *what)
{
	fprintf(stderr, "fbplayer: %s: %s\n", what, strerror(errno));
	exit(1);
}

static void logmsg(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

/* ---------------------------------------------------------------- fb setup */

static int env_int(const char *name, int fallback)
{
	const char *v = getenv(name);
	if (!v || !*v)
		return fallback;
	return atoi(v);
}

static void fb_emulated(void)
{
	const char *path = getenv("BA_FB_FILE");
	struct stat st;

	fb_w = env_int("BA_FB_W", 1080);
	fb_h = env_int("BA_FB_H", 2340);
	fb_bpp = env_int("BA_FB_BPP", 32);
	fb_stride = env_int("BA_FB_STRIDE", fb_w * (fb_bpp / 8));
	fb_pages = 1;
	fb_rshift = 16;
	fb_gshift = 8;
	fb_bshift = 0;

	if (stat(path, &st) != 0 || (size_t)st.st_size < (size_t)fb_stride * fb_h) {
		int fd = open(path, O_RDWR | O_CREAT, 0644);
		if (fd < 0)
			die(path);
		if (ftruncate(fd, (off_t)fb_stride * fb_h) != 0)
			die("ftruncate");
		if (close(fd) != 0)
			die("close");
	}

	fb_fd = open(path, O_RDWR);
	if (fb_fd < 0)
		die(path);
	fb_map_len = (size_t)fb_stride * fb_h;
	fb_map = mmap(NULL, fb_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_map == MAP_FAILED)
		die("mmap emulated fb");
	memset(fb_map, 0, fb_map_len);
	logmsg("fbplayer: emulated framebuffer %dx%d %dbpp stride=%d", fb_w, fb_h, fb_bpp, fb_stride);
}

static void fb_open(void)
{
	const char *path = "/dev/graphics/fb0";
	struct fb_fix_screeninfo fix;

	if (getenv("BA_FB_FILE")) {
		fb_emulated();
		return;
	}

	fb_fd = open(path, O_RDWR);
	if (fb_fd < 0)
		path = "/dev/fb0";
	if (fb_fd < 0)
		fb_fd = open(path, O_RDWR);
	if (fb_fd < 0)
		die("open framebuffer");

	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_var) < 0)
		die("FBIOGET_VSCREENINFO");
	if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0)
		die("FBIOGET_FSCREENINFO");

	fb_w = fb_var.xres;
	fb_h = fb_var.yres;
	fb_bpp = fb_var.bits_per_pixel;
	fb_stride = fix.line_length;
	fb_rshift = fb_var.red.offset;
	fb_gshift = fb_var.green.offset;
	fb_bshift = fb_var.blue.offset;
	fb_live = 1;

	fb_map_len = fix.smem_len ? fix.smem_len : (size_t)fb_stride * fb_h;
	fb_map = mmap(NULL, fb_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_map == MAP_FAILED)
		die("mmap framebuffer");

	/* Blank every page so nothing from the console survives.  In probe mode
	 * the GUI is still running, so leave its picture alone. */
	if (!probe_only)
		memset(fb_map, 0, fb_map_len);

	/* Bring the first page on screen and draw there; double buffering is
	 * used below when the driver exposes two pages. */
	if (fb_var.yres_virtual >= (unsigned)(fb_h * 2) &&
	    (size_t)fix.line_length * fb_h * 2 <= fb_map_len)
		fb_pages = 2;

	if (fb_pages == 2) {
		fb_var.yoffset = 0;
		if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &fb_var) < 0)
			fb_pages = 1;
	}
	if (fb_pages == 1) {
		fb_var.yoffset = 0;
		ioctl(fb_fd, FBIOPUT_VSCREENINFO, &fb_var);
	}

	fb_page = (fb_pages == 2) ? 1 : 0;

	logmsg("fbplayer: framebuffer %dx%d %dbpp stride=%d pages=%d red=%d green=%d blue=%d",
	       fb_w, fb_h, fb_bpp, fb_stride, fb_pages, fb_rshift, fb_gshift, fb_bshift);
}

static int fb_show(int page)
{
	if (!fb_live || fb_pages != 2)
		return 0;
	fb_var.yoffset = page * fb_h;
	if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &fb_var) < 0)
		return -1;
	return 0;
}

/* ----------------------------------------------------------------- drawing */

static inline uint32_t pixel(unsigned alpha)
{
	if (fb_bpp == 16)
		return ((alpha >> 3) << 11) | ((alpha >> 2) << 5) | (alpha >> 3);
	return ((uint32_t)alpha << fb_rshift) |
	       ((uint32_t)alpha << fb_gshift) |
	       ((uint32_t)alpha << fb_bshift);
}

static void draw_cell(unsigned char *base, int x, int y, int cw, int ch, int glyph)
{
	const unsigned char *src = font_bits[glyph];
	int gx, gy;

	if (cw == FONT_W && ch == FONT_H) {
		for (gy = 0; gy < ch; gy++) {
			unsigned char *row = base + (size_t)(y + gy) * fb_stride;
			const unsigned char *s = src + gy * FONT_W;
			for (gx = 0; gx < cw; gx++) {
				unsigned a = s[gx];
				if (!a)
					continue;
				if (fb_bpp == 16)
					((uint16_t *)row)[x + gx] = (uint16_t)pixel(a);
				else
					((uint32_t *)row)[x + gx] = pixel(a);
			}
		}
		return;
	}

	for (gy = 0; gy < ch; gy++) {
		unsigned char *row = base + (size_t)(y + gy) * fb_stride;
		int sy = gy * FONT_H / ch;
		const unsigned char *s = src + sy * FONT_W;
		for (gx = 0; gx < cw; gx++) {
			unsigned a = s[gx * FONT_W / cw];
			if (!a)
				continue;
			if (fb_bpp == 16)
				((uint16_t *)row)[x + gx] = (uint16_t)pixel(a);
			else
				((uint32_t *)row)[x + gx] = pixel(a);
		}
	}
}

static void draw_frame(const char rows[][MAX_COLS + 1], int nrows, int ncols)
{
	unsigned char *base = fb_map + (size_t)fb_page * fb_h * fb_stride;
	int cw = fb_w / ncols;
	int ch = cw * 36 / 20;
	int x0, y0, r, c, limit;

	if (cw < 2)
		cw = 2;
	if (ch < 2)
		ch = 2;
	/* Keep the console cell aspect (~20x36), but never taller than the
	 * screen allows.  A 1080 px panel gives 20x36 cells, i.e. 1040x684. */
	if (ch * nrows > fb_h) {
		ch = fb_h / nrows;
		if (ch < 2)
			ch = 2;
	}

	x0 = (fb_w - ncols * cw) / 2;
	y0 = (fb_h - nrows * ch) / 2;

	/* Full clear: this is the part the console cannot do. */
	memset(base, 0, (size_t)fb_h * fb_stride);

	limit = fb_h - y0;
	for (r = 0; r < nrows; r++) {
		int y = y0 + r * ch;
		if (y + ch > limit)
			break;
		for (c = 0; c < ncols; c++) {
			unsigned char ch_ = (unsigned char)rows[r][c];
			int x = x0 + c * cw;
			if (ch_ < FONT_FIRST || ch_ > FONT_LAST)
				continue;
			if (x < 0 || x + cw > fb_w)
				continue;
			draw_cell(base, x, y, cw, ch, ch_ - FONT_FIRST);
		}
	}

	if (fb_pages == 2) {
		if (fb_show(fb_page) == 0)
			fb_page ^= 1;
	}
}

/* ------------------------------------------------------------------- input */

static int read_frame(FILE *in, int nrows, int ncols)
{
	int r;
	int got = 0;
	for (r = 0; r < nrows; r++) {
		if (!fgets(frame_rows[r], MAX_COLS + 2, in)) {
			/* Short final frame: blank the rest instead of aborting. */
			memset(frame_rows[r], ' ', ncols);
			frame_rows[r][ncols] = '\0';
			continue;
		}
		got++;
		size_t len = strlen(frame_rows[r]);
		while (len && (frame_rows[r][len - 1] == '\n' || frame_rows[r][len - 1] == '\r'))
			frame_rows[r][--len] = '\0';
		while (len < (size_t)ncols) {
			frame_rows[r][len] = ' ';
			len++;
		}
		frame_rows[r][ncols] = '\0';
	}
	return got;
}

static void drain_input(void)
{
	char path[64];
	int i;

	for (i = 0; i < 32; i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		char buf[512];
		while (read(fd, buf, sizeof(buf)) > 0)
			;
		close(fd);
	}
}

/* -------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
	const char *frames_path = NULL;
	int fps = 15;
	long still = -1;
	int nrows = 19;
	int ncols = 52;
	FILE *in;
	struct timespec next;
	long frame = 0;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--still") && i + 1 < argc) {
			still = atol(argv[++i]);
		} else if (!strcmp(argv[i], "--probe")) {
			probe_only = 1;
		} else if (!strcmp(argv[i], "--rows") && i + 1 < argc) {
			nrows = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--cols") && i + 1 < argc) {
			ncols = atoi(argv[++i]);
		} else if (!strcmp(argv[i], "--fps") && i + 1 < argc) {
			fps = atoi(argv[++i]);
		} else if (argv[i][0] != '-') {
			frames_path = argv[i];
		}
	}
	if (fps <= 0 || fps > 120)
		fps = 15;
	if (nrows < 1 || nrows > MAX_ROWS || ncols < 1 || ncols > MAX_COLS) {
		fprintf(stderr, "fbplayer: bad geometry %dx%d\n", ncols, nrows);
		return 1;
	}

	in = frames_path ? fopen(frames_path, "r") : stdin;
	if (!in && !probe_only) {
		perror(frames_path);
		return 1;
	}

	fb_open();

	if (probe_only) {
		close(fb_fd);
		return 0;
	}

	if (still >= 0) {
		long skip;
		for (skip = 0; skip <= still; skip++) {
			if (read_frame(in, nrows, ncols) == 0)
				break;
			if (skip == still)
				break;
		}
		draw_frame(frame_rows, nrows, ncols);
		close(fb_fd);
		return 0;
	}

	clock_gettime(CLOCK_MONOTONIC, &next);
	for (;;) {
		if (read_frame(in, nrows, ncols) == 0)
			break;
		draw_frame(frame_rows, nrows, ncols);
		frame++;

		next.tv_nsec += 1000000000L / fps;
		while (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		/* If we fell behind (slow panel), resync instead of spinning. */
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec > next.tv_sec ||
		    (now.tv_sec == next.tv_sec && now.tv_nsec > next.tv_nsec)) {
			next = now;
			continue;
		}
		while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL) == EINTR)
			;
	}

	drain_input();

	/* Leave the screen black; the recovery GUI repaints on SIGCONT. */
	{
		unsigned char *base = fb_map + (size_t)fb_page * fb_h * fb_stride;
		memset(base, 0, (size_t)fb_h * fb_stride);
		if (fb_pages == 2)
			fb_show(fb_page);
	}

	logmsg("fbplayer: %ld frames drawn", frame);
	if (fb_map && fb_map != MAP_FAILED)
		munmap(fb_map, fb_map_len);
	if (fb_fd >= 0)
		close(fb_fd);
	return 0;
}
