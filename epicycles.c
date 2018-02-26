
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cgbp.h"

#define STEP_DIV 256
#define STEPS_PER_FRAME 16

#define SIGN(x) ((x) < 0 ? -1 : 1)
#define ABS(x) ((long)(x) < 0 ? -((long)(x)) : ((long)(x)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TO_RGB(r, g, b) \
	(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

// http://www.jstor.org/stable/2691465?origin=crossref&seq=1#page_scan_tab_contents

struct epicycle {
	double rot_off, r_mul, r_mul_delta;
	size_t prev_x, prev_y, cx, cy, scale, step;
};

void epicycles_init(struct cgbp *c, struct epicycle *e) {
	struct cgbp_size size = driver.size(c);
	e->cx = size.w / 2;
	e->cy = size.h / 2;
	e->scale = MIN(size.w, size.h) / 4;
	e->step = 0;
	e->prev_x = SIZE_MAX;
	e->prev_y = SIZE_MAX;
	e->rot_off = 0;
	e->r_mul = 1;
	e->r_mul_delta = .01 / STEPS_PER_FRAME;
}

void draw_line(struct cgbp *c, struct cgbp_size size, size_t start_x,
               size_t start_y, size_t end_x, size_t end_y, uint32_t color) {
	long delta_x, delta_y, pos, other;
	delta_x = end_x - start_x;
	delta_y = end_y - start_y;
	if((size_t)end_x < size.w && (size_t)end_y < size.h)
		driver.set_pixel(c, end_x, end_y, color);
	if(ABS(delta_x) < ABS(delta_y))
		goto use_y;
	for(pos = start_x; (size_t)pos != end_x; pos += SIGN(delta_x)) {
		other = start_y + delta_y * ABS(pos - start_x) / ABS(delta_x);
		if((size_t)pos < size.w && (size_t)other < size.h)
			driver.set_pixel(c, pos, other, color);
	}
	return;
use_y:
	for(pos = start_y; (size_t)pos != end_y; pos += SIGN(delta_y)) {
		other = start_x + delta_x * ABS(pos - start_y) / ABS(delta_y);
		if((size_t)other < size.w && (size_t)pos < size.h)
			driver.set_pixel(c, other, pos, color);
	}
	return;
}

int epicycles_step(struct cgbp *c, struct cgbp_size size, struct epicycle *e) {
	float fx, fy;
	size_t x, y;

	fx = cos(e->rot_off + (double)e->step / STEP_DIV) * e->r_mul +
	     cos(e->rot_off + (double)e->step * 7 / STEP_DIV) / 2 * e->r_mul +
	     sin(e->rot_off + (double)e->step * 17 / STEP_DIV) / 3 * e->r_mul;
	fy = sin(e->rot_off + (double)e->step / STEP_DIV) * e->r_mul +
	     sin(e->rot_off + (double)e->step * 7 / STEP_DIV) / 2 * e->r_mul +
	     cos(e->rot_off + (double)e->step * 17 / STEP_DIV) / 3 * e->r_mul;

	x = fx * e->scale + e->cx;
	y = fy * e->scale + e->cy;
	if(e->prev_x != SIZE_MAX && e->prev_y != SIZE_MAX)
		draw_line(c, size, e->prev_x, e->prev_y, x, y, 0xffffff);
	e->prev_x = x;
	e->prev_y = y;
	e->step++;

	e->rot_off += M_PI / 360 / STEPS_PER_FRAME / 4;
	e->r_mul += e->r_mul_delta;
	if(e->r_mul > 2 || e->r_mul < .4)
		e->r_mul_delta *= -1;
	return 0;
}

#define B(x) ((x) & 0xff)
#define BLUR_FAC 128
static inline uint32_t blur(uint32_t *c, size_t step) {
	uint32_t x = B(c[0]) / 20 + B(c[1]) / 5 + B(c[2]) / 20 +
	             B(c[3]) / 5 + BLUR_FAC * B(c[4]) + B(c[5]) / 5 +
	             B(c[6]) / 20 + B(c[7]) / 5 + B(c[8]) / 20;
	x = (x / (BLUR_FAC + 1)) & 0xff;
	return (x << 16) | (x << 8) | x;
	(void)step;
}

int epicycles_update(struct cgbp *c, void *data) {
	struct epicycle *e = data;
	struct cgbp_size size = driver.size(c);
	size_t i, x, y;
	uint32_t pline1[size.w], pline2[size.w], *pline_new, *pline_old, *tmp;
	uint32_t pleft1, pleft2, *pleft_new, *pleft_old, neighbors[9];
	pline_new = pline1;
	pline_old = pline2;
	pleft_new = &pleft1;
	pleft_old = &pleft2;
	for(y = 0; y < size.h; y++) {
		for(x = 0; x < size.w; x++)
			pline_new[x] = driver.get_pixel(c, x, y);
		for(x = 0; x < size.w; x++) {
			*pleft_new = driver.get_pixel(c, x, y);
			if(y > 0) {
				neighbors[0] = x > 0 ? pline_old[x - 1] : 0;
				neighbors[1] = pline_old[x];
				neighbors[2] = x < size.w - 1 ? pline_old[x + 1] : 0;
			} else
				neighbors[0] = neighbors[1] = neighbors[2] = 0;

			neighbors[3] = x > 0 ? *pleft_old : 0;
			neighbors[4] = driver.get_pixel(c, x, y);
			neighbors[5] = x < size.w - 1 ? driver.get_pixel(c, x + 1, y) : 0;

			if(y < size.h - 1) {
				neighbors[6] = x > 0 ? driver.get_pixel(c, x - 1, y + 1) : 0;
				neighbors[7] = driver.get_pixel(c, x, y + 1);
				neighbors[8] = x < size.w - 1 ?
				               driver.get_pixel(c, x + 1, y + 1) : 0;
			} else
				neighbors[6] = neighbors[7] = neighbors[8] = 0;
			driver.set_pixel(c, x, y, blur(neighbors, e->step));

			tmp = pleft_new;
			pleft_new = pleft_old;
			pleft_old = tmp;
		}
		tmp = pline_new;
		pline_new = pline_old;
		pline_old = tmp;
	}
	for(i = 0; i < STEPS_PER_FRAME; i++)
		if(epicycles_step(c, size, e) < 0)
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
