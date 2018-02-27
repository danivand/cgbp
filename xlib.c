
/* xlib.c
 *
 * Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
 *
 * This software may be modified and distributed under the terms
 * of the ISC license.  See the LICENSE file for details.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "cgbp.h"

#define BITDEPTH 32

struct xlib {
	Display *disp;
	int scr;
	XVisualInfo vinfo;
	Colormap cmap;
	Window win;
	GC gc;
	XImage *img;
	XIM xim;
	XIC xic;
	size_t img_allo;
	uint8_t cmap_set: 1, win_set: 1, gc_set: 1;
};

static inline int setup_input(struct xlib *x) {
	x->xim = XOpenIM(x->disp, NULL, NULL, NULL);
	if(x->xim != NULL)
		goto done;
	XSetLocaleModifiers("@im=local");
	x->xim = XOpenIM(x->disp, NULL, NULL, NULL);
	if(x->xim != NULL)
		goto done;
	XSetLocaleModifiers("@im=");
	x->xim = XOpenIM(x->disp, NULL, NULL, NULL);
	if(x->xim == NULL) {
		fprintf(stderr, "Error: XOpenIM: could not open input devive.\n");
		return -1;
	}
done:
	x->xic = XCreateIC(x->xim, XNInputStyle, XIMPreeditNothing
					   | XIMStatusNothing, XNClientWindow, x->win,
					   XNFocusWindow, x->win, NULL);
	if(x->xic == NULL)
		return -1;
	return 0;
}

void invisible_cursor(struct xlib *x) {
	Pixmap p;
	XSetWindowAttributes xa;
	XColor d = { .pixel = XBlackPixel(x->disp, x->scr) };
	p = XCreatePixmap(x->disp, x->win, 1, 1, 1);
	xa.cursor = XCreatePixmapCursor(x->disp, p, p, &d, &d, 0, 0);
	XChangeWindowAttributes(x->disp, x->win, CWCursor, &xa);
	XFreeCursor(x->disp, xa.cursor);
	XFreePixmap(x->disp, p);
}

void xlib_cleanup(struct cgbp *c);

int xlib_init(struct cgbp *c) {
	struct xlib *x = malloc(sizeof *x);
	Window root;
	XWindowAttributes attr;
	size_t bytesize, i;
	if(x == NULL) {
		perror("malloc");
		return -1;
	}
	c->driver_data = x;
	x->cmap_set = 0;
	x->win_set = 0;
	x->gc_set = 0;
	x->xic = NULL;
	x->xim = NULL;
	x->img = NULL;
	x->disp = XOpenDisplay(NULL);
	if(x->disp == NULL) {
		fprintf(stderr, "Error: failed to open Display.\n");
		goto error;
	}
	x->scr = DefaultScreen(x->disp);
	if(XMatchVisualInfo(x->disp, x->scr, BITDEPTH, TrueColor, &x->vinfo) == 0) {
		fprintf(stderr, "Error: XMatchVisualInfo: no such visual.\n");
		goto error;
	}
	root = RootWindow(x->disp, x->scr);
	x->cmap = XCreateColormap(x->disp, root, x->vinfo.visual, AllocNone);
	x->cmap_set = 1;

	if(XGetWindowAttributes(x->disp, root, &attr) == 0) {
		fprintf(stderr, "Error: XGetWindowAttributes failed.\n");
		goto error;
	}
	x->win = XCreateWindow(x->disp, root,
		0, 0, attr.width, attr.height,
		0, x->vinfo.depth, InputOutput, x->vinfo.visual,
		CWBackPixel|CWColormap|CWBorderPixel|CWEventMask|CWOverrideRedirect,
		&(XSetWindowAttributes){
			.background_pixel = BlackPixel(x->disp, x->scr),
			.border_pixel = 0,
			.colormap = x->cmap,
			.event_mask = StructureNotifyMask,
			.override_redirect = True,
		}
	);
	x->win_set = 1;
	x->gc = XCreateGC(x->disp, x->win, 0, NULL);
	x->gc_set = 1;

	XMapWindow(x->disp, x->win);
	XGrabKeyboard(x->disp, x->win, 0, GrabModeAsync, GrabModeAsync,
	              CurrentTime);
	XMoveResizeWindow(x->disp, x->win, 0, 0, attr.width, attr.height);
	XRaiseWindow(x->disp, x->win);

	x->img = XCreateImage(
		x->disp, x->vinfo.visual, x->vinfo.depth, ZPixmap, 0, NULL,
		attr.width, attr.height, 8, 0
	);
	if(x->img == NULL) {
		fprintf(stderr, "Error: XCreateImage failed.\n");
		goto error;
	}
	bytesize = x->img->depth / CHAR_BIT * x->img->width * x->img->height;
	x->img->data = malloc(bytesize);
	if(x->img->data == NULL) {
		perror("malloc");
		goto error;
	}
	for(i = 0; i < bytesize; i++)
		x->img->data[i] = i % (x->img->depth / CHAR_BIT) > 2 ? 255 : 0;
	if(setup_input(x) < 0)
		goto error;
	invisible_cursor(x);
	return 0;
error:
	xlib_cleanup(c);
	return -1;
}

static inline int handle_events(struct cgbp *c, void *cb_data, XEvent *ev,
                                struct cgbp_callbacks cb) {
	struct xlib *x = c->driver_data;
	KeySym keysym;
	Status st;
	int len;
	char buf[32];
	switch(ev->type) {
	case KeyPress:
		if(cb.action == NULL)
			break;
		len = XmbLookupString(x->xic, &ev->xkey, buf, sizeof buf, &keysym, &st);
		if(st != XLookupChars && st != XLookupBoth)
			break;
		if(len == 1 && cb.action(c, cb_data, *buf) < 0)
			return -1;
		break;
	case KeyRelease:
	case MapNotify:
		break;
	default:
		printf("event: %d\n", ev->type);
		break;
	}
	return 0;
}

int xlib_update(struct cgbp *c, void *cb_data, struct cgbp_callbacks cb) {
	struct xlib *x = c->driver_data;
	XEvent ev;
	while(XPending(x->disp) > 0) {
		XNextEvent(x->disp, &ev);
		if(handle_events(c, cb_data, &ev, cb) < 0)
			return -1;
	}
	if(cb.update != NULL && cb.update(c, cb_data) < 0)
		return -1;
	XPutImage(x->disp, x->win, x->gc, x->img,
	          0, 0, 0, 0, x->img->width, x->img->height);
	return 0;
}

void xlib_cleanup(struct cgbp *c) {
	struct xlib *x = c->driver_data;
	if(x->xic != NULL)
		XDestroyIC(x->xic);
	if(x->xim != NULL)
		XCloseIM(x->xim);
	if(x->img != NULL)
		XDestroyImage(x->img);
	if(x->cmap_set == 1)
		XFreeColormap(x->disp, x->cmap);
	if(x->gc_set)
		XFreeGC(x->disp, x->gc);
	if(x->win_set)
		XDestroyWindow(x->disp, x->win);
	if(x->disp != NULL)
		XCloseDisplay(x->disp);
	free(x);
}

uint32_t xlib_get_pixel(struct cgbp *c, size_t cx, size_t cy) {
	struct xlib *x = c->driver_data;
	size_t bytes_pp = x->img->bits_per_pixel / CHAR_BIT,
	       base = cy * x->img->bytes_per_line + cx * bytes_pp;
	uint32_t value = 0, i;
	if(cx >= (size_t)x->img->width || cy >= (size_t)x->img->height)
		return 0;
	for(i = 0; i < bytes_pp; i++)
		value |= x->img->data[base + i] << (8 * i);
	return value & 0xffffff;
}

void xlib_set_pixel(struct cgbp *c, size_t cx, size_t cy, uint32_t color) {
	struct xlib *x = c->driver_data;
	size_t i, bytes_per_pixel = x->img->bits_per_pixel / CHAR_BIT, px;
	px = cy * x->img->bytes_per_line + cx * bytes_per_pixel;
	color = 0xff000000 | (color & 0xffffff);
	for(i = 0; i < bytes_per_pixel; i++)
		x->img->data[px + i] = color >> (8 * i);
}

struct cgbp_size xlib_size(struct cgbp *c) {
	struct xlib *x = c->driver_data;
	return (struct cgbp_size){ x->img->width, x->img->height };
}

struct cgbp_driver driver = {
	xlib_init,
	xlib_update,
	xlib_cleanup,
	xlib_get_pixel,
	xlib_set_pixel,
	xlib_size,
};
