
/* epicycles.c
 *
 * Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
 *
 * This software may be modified and distributed under the terms
 * of the ISC license.  See the LICENSE file for details.
 */

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "cgbp.h"

#define STEP_DIV 256

#define SIGN(x) ((x) < 0 ? -1 : 1)
#define ABS(x) ((long)(x) < 0 ? -((long)(x)) : ((long)(x)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TO_RGB(r, g, b) \
	(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

// http://www.jstor.org/stable/2691465?origin=crossref&seq=1#page_scan_tab_contents

struct epicycle {
	size_t prev_x, prev_y, cx, cy, scale, step;
};

void epicycles_init(struct cgbp *c, struct epicycle *e) {
	struct cgbp_size size = driver.size(c->driver_data);
	e->cx = size.w / 2;
	e->cy = size.h / 2;
	e->scale = MIN(size.w, size.h) / 4;
	e->step = 0;
	e->prev_x = SIZE_MAX;
	e->prev_y = SIZE_MAX;
}

static inline uint32_t darken(uint32_t color) {
	uint8_t channels[3] = { color >> 16, color >> 8, color };
	return TO_RGB(
		channels[0] - 3 * (channels[0] > 2),
		channels[1] - 3 * (channels[1] > 2),
		channels[2] - 3 * (channels[2] > 2));
}

void draw_line(void *driver_data, size_t start_x, size_t start_y, size_t end_x,
               size_t end_y, uint32_t color) {
	long delta_x, delta_y, pos, other;
	delta_x = end_x - start_x;
	delta_y = end_y - start_y;
	driver.set_pixel(driver_data, end_x, end_y, color);
	if(ABS(delta_x) < ABS(delta_y))
		goto use_y;
	for(pos = start_x; (size_t)pos != end_x; pos += SIGN(delta_x)) {
		other = start_y + delta_y * ABS(pos - start_x) / ABS(delta_x);
		driver.set_pixel(driver_data, pos, other, color);
	}
	return;
use_y:
	for(pos = start_y; (size_t)pos != end_y; pos += SIGN(delta_y)) {
		other = start_x + delta_x * ABS(pos - start_y) / ABS(delta_y);
		driver.set_pixel(driver_data, other, pos, color);
	}
	return;
}

int epicycles_step(struct cgbp *c, void *data) {
	struct epicycle *e = data;
	float fx, fy;
	size_t x, y;

	fx = cos((double)e->step / STEP_DIV) +
	     cos((double)e->step * 7 / STEP_DIV) / 2 +
	     sin((double)e->step * 17 / STEP_DIV) / 3;
	fy = sin((double)e->step / STEP_DIV) +
	     sin((double)e->step * 7 / STEP_DIV) / 2 +
	     cos((double)e->step * 17 / STEP_DIV) / 3;

	x = fx * e->scale + e->cx;
	y = fy * e->scale + e->cy;
	if(e->prev_x != SIZE_MAX && e->prev_y != SIZE_MAX)
		draw_line(c->driver_data, e->prev_x, e->prev_y, x, y, 0xffffff);
	e->prev_x = x;
	e->prev_y = y;
	e->step++;
	return 0;
}

int epicycles_update(struct cgbp *c, void *data) {
	size_t i, x, y;
	struct cgbp_size size = driver.size(c->driver_data);
	for(y = 0; y < size.h; y++)
		for(x = 0; x < size.w; x++)
			driver.set_pixel(c->driver_data, x, y,
			                 darken(driver.get_pixel(c->driver_data, x, y)));
	for(i = 0; i < 16; i++)
		if(epicycles_step(c, data) < 0)
			return -1;
	return 0;
}

int epicycles_action(struct cgbp *c, void *data, char r) {
	if(r == 'q' || r == 'Q')
		c->running = 0;
	return 0;
	(void)data;
}


int main(void) {
	struct cgbp c;
	struct cgbp_callbacks cb = {
		.update = epicycles_update,
		.action = epicycles_action,
	};
	struct epicycle e;
	int ret = EXIT_FAILURE;
	srand(time(NULL));
	if(cgbp_init(&c) < 0)
		goto error;
	epicycles_init(&c, &e);

	if(cgbp_main(&c, &e, cb) == 0)
		ret = EXIT_SUCCESS;
error:
	cgbp_cleanup(&c);
	return ret;
}
