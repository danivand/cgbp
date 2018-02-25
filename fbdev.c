
/* fbdev.c
 *
 * Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
 *
 * This software may be modified and distributed under the terms
 * of the ISC license.  See the LICENSE file for details.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "cgbp.h"

struct fbdev {
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;
	struct termios tc;
	int fbfd, old_fl;
	uint8_t *fbmm, *data, tc_set: 1;
};

static inline ssize_t fbdev_write_term(const char *str, size_t len) {
	ssize_t ret;
	if(len == 0)
		len = strlen(str);
	do {
		errno = 0;
		ret = write(STDOUT_FILENO, str, len);
	} while(ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
	return ret;
}

void fbdev_cleanup(struct cgbp *c);

int fbdev_init(struct cgbp *c) {
	struct fbdev *f = malloc(sizeof *f);
	size_t buffer_size;
	tcflag_t lflag_orig;
	if(f == NULL) {
		perror("malloc");
		return -1;
	}
	c->driver_data = f;

	f->fbfd = open("/dev/fb0", O_RDWR);
	if(f->fbfd < 0) {
		perror("open");
		goto error;
	}
	if(ioctl(f->fbfd, FBIOGET_VSCREENINFO, &f->vinfo) < 0)
		goto error;
	if(ioctl(f->fbfd, FBIOGET_FSCREENINFO, &f->finfo) < 0)
		goto error;
	buffer_size = f->vinfo.yres * f->finfo.line_length;

	// unneeded - need to no longer assume a pixel in *fbmm to be uint32_t
	f->fbmm = mmap(0, buffer_size, PROT_READ|PROT_WRITE,
	               MAP_SHARED, f->fbfd, 0);
	if(f->fbmm == MAP_FAILED) {
		perror("mmap");
		f->fbmm = NULL;
		goto error;
	}
	f->data = malloc(buffer_size);
	if(f->data == NULL) {
		perror("malloc");
		goto error;
	}
	memset(f->data, 0, buffer_size);

	// turn off cursor
	fbdev_write_term("\x1b[?25l", 6);
	f->old_fl = fcntl(STDIN_FILENO, F_GETFL, 0);
	if(fcntl(STDIN_FILENO, F_SETFL, f->old_fl|O_NONBLOCK) < 0) {
		perror("fcntl");
		goto error;
	}
	if(tcgetattr(STDIN_FILENO, &f->tc) < 0) {
		perror("tcgetattr");
		goto error;
	}
	f->tc_set = 1;
	lflag_orig = f->tc.c_lflag;
	f->tc.c_lflag &= ~(ICANON|ECHO);
	if(tcsetattr(STDIN_FILENO, TCSANOW, &f->tc) < 0) {
		perror("tcsetattr");
		goto error;
	}
	f->tc.c_lflag = lflag_orig;
	return 0;
error:
	fbdev_cleanup(c);
	return -1;
}

struct cgbp_driver driver;

int fbdev_update(struct cgbp *c, void *cb_data, struct cgbp_callbacks cb) {
	struct fbdev *f = c->driver_data;
	char r;
	if(cb.action != NULL) {
		if(read(STDIN_FILENO, &r, 1) > 0 && cb.action(c, cb_data, r) < 0)
			return -1;
	}
	if(cb.update != NULL && cb.update(c, cb_data) < 0)
		return -1;
	memcpy(f->fbmm, f->data, f->vinfo.yres * f->finfo.line_length);
	return 0;
}

void fbdev_cleanup(struct cgbp *c) {
	struct fbdev *f = c->driver_data;
	if(f->fbmm != NULL) {
		memset(f->fbmm, 0, f->vinfo.yres * f->finfo.line_length);
		munmap(f->fbmm, f->vinfo.yres * f->finfo.line_length);
	}
	if(f->fbfd >= 0)
		close(f->fbfd);
	if(f->data != NULL)
		free(f->data);

	// turn on cursor
	fbdev_write_term("\x1b[?25h", 6);
	if(fcntl(STDIN_FILENO, F_SETFL, f->old_fl) < 0)
		perror("fcntl");
	if(f->tc_set == 1 && tcsetattr(STDIN_FILENO, TCSANOW, &f->tc) < 0)
		perror("tcsetattr");
	free(f);
}

uint32_t fbdev_get_pixel(struct cgbp *c, size_t x, size_t y) {
	struct fbdev *f = c->driver_data;
	size_t bytes_pp = f->vinfo.bits_per_pixel / CHAR_BIT;
	size_t base = y * f->finfo.line_length + x * bytes_pp;
	size_t value = f->data[base], i;
	if(x > f->vinfo.xres || y > f->vinfo.yres)
		return 0;
	for(i = 1; i < bytes_pp; i++)
		value = (value << 8) | f->data[base + i];
	return value;
}

void fbdev_set_pixel(struct cgbp *c, size_t x, size_t y, uint32_t color) {
	struct fbdev *f = c->driver_data;
	size_t bytes_pp = f->vinfo.bits_per_pixel / CHAR_BIT;
	size_t base = y * f->finfo.line_length + x * bytes_pp, i;
	if(x > f->vinfo.xres || y > f->vinfo.yres)
		return;
	for(i = 0; i < bytes_pp; i++)
		f->data[base + i] = color >> (8 * i);
}

struct cgbp_size fbdev_size(struct cgbp *c) {
	struct fbdev *f = c->driver_data;
	return (struct cgbp_size){ f->vinfo.xres, f->vinfo.yres };
}

struct cgbp_driver driver = {
	fbdev_init,
	fbdev_update,
	fbdev_cleanup,
	fbdev_get_pixel,
	fbdev_set_pixel,
	fbdev_size,
};
