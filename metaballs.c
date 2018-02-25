
/* metaballs.c
 *
 * Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
 *
 * This software may be modified and distributed under the terms
 * of the ISC license.  See the LICENSE file for details.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cgbp.h"
#include "hsv.h"

#define NUM_BALLS 6
#define NUM_RGB_CACHE 1024
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define TO_RGB(r, g, b) \
	(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

struct ball {
	size_t x, y;
	long speed_x, speed_y;
	float *dist_cache;
};

struct metaballs {
	struct ball balls[NUM_BALLS];
	uint32_t rgb_cache[NUM_RGB_CACHE];
};

static inline float rsqrt(float n) {
	void *v;
	int i;
	float x2;
	x2 = n * 0.5F;
	v  = &n;
	i  = *(int*)v;                      // evil floating point bit level hacking
	i  = 0x5f3759df - (i >> 1);         // what the fuck?
	v  = &i;
	n  = *(float*)v;
	n  = n * (1.5F - (x2 * n * n));     // 1st iteration
	return n;
}

static inline int metaballs_init_dist_cache(struct ball *b, float radius,
                                            struct cgbp_size size) {
	size_t x, y;
	float *dist;
	b->dist_cache = malloc(size.w * size.h * sizeof *b->dist_cache);
	if(b->dist_cache == NULL) {
		perror("malloc");
		return -1;
	}
	for(y = 0; y < size.h; y++)
		for(x = 0; x < size.w; x++) {
			dist = &b->dist_cache[y * size.w + x];
			*dist = 255 * radius * rsqrt(x * x + y * y);
		}
	return 0;
}

static inline void metaballs_init_color(struct metaballs *m) {
	uint16_t i;
	float rnd, mult, hue;
	double rgb[3] = { 0.0, 0.0, 0.0 };
	rnd = (float)rand() / RAND_MAX * 2.0;
	mult = .25 + (float)rand() / RAND_MAX * 1.75;
	for(i = 0; i < NUM_RGB_CACHE; i++) {
		if(rnd > 1.0)
			hue = (float)i / NUM_RGB_CACHE - (rnd - 1.0);
		else
			hue = (float)i / NUM_RGB_CACHE + rnd;
		hue *= mult;
		hsv_to_rgb(rgb, hue - floor(hue), 1.0, .8);
		m->rgb_cache[i] = TO_RGB(rgb[0] * 255, rgb[1] * 255, rgb[2] * 255);
	}
}

int metaballs_init(struct metaballs *m, struct cgbp_size size) {
	size_t i;
	m->balls[0].x = 600;
	m->balls[0].y = 600;
	for(i = 0; i < NUM_BALLS; i++)
		m->balls[i].dist_cache = NULL;
	for(i = 0; i < NUM_BALLS; i++) {
		m->balls[i].x = rand() % size.w;
		m->balls[i].y = rand() % size.h;
		m->balls[i].speed_x = rand() % 20;
		m->balls[i].speed_y = rand() % 20;
		if(metaballs_init_dist_cache(&m->balls[i], 30 + rand() % 60, size) < 0)
			return -1;
	}
	metaballs_init_color(m);
	return 0;
}

static inline float ball_dist(struct ball *b, size_t x, size_t y,
                              struct cgbp_size size) {
	if(b->x > x)
		x = b->x - x;
	else
		x -= b->x;
	if(b->y > y)
		y = b->y - y;
	else
		y -= b->y;
	return b->dist_cache[y * size.w + x];
}

int metaballs_update(struct cgbp *c, void *data) {
	struct metaballs *m = data;
	struct cgbp_size size = driver.size(c);
	size_t i, x, y;
	long remainder;
	float dist;
	for(i = 0; i < NUM_BALLS; i++) {
		// check if the difference would wrap beyond the screen
		if(m->balls[i].speed_x > 0)
			remainder = size.w - 1 - m->balls[i].x;
		else
			remainder = -m->balls[i].x;
		if(ABS(remainder) >= ABS(m->balls[i].speed_x))
			m->balls[i].x += m->balls[i].speed_x;
		else {
			// and apply the wrapped-around difference instead
			m->balls[i].speed_x *= -1;
			m->balls[i].x += m->balls[i].speed_x + remainder;
		}
		if(m->balls[i].speed_y > 0)
			remainder = size.h - 1 - m->balls[i].y;
		else
			remainder = -m->balls[i].y;
		if(ABS(remainder) >= ABS(m->balls[i].speed_y))
			m->balls[i].y += m->balls[i].speed_y;
		else {
			m->balls[i].speed_y *= -1;
			m->balls[i].y += m->balls[i].speed_y + remainder;
		}
	}
	for(y = 0; y < size.h; y++)
		for(x = 0; x < size.w; x++) {
			dist = 0;
			for(i = 0; i < NUM_BALLS; i++)
				dist += ball_dist(&m->balls[i], x, y, size);
			dist *= NUM_RGB_CACHE / 256;
			if(dist < 0)
				dist = 0;
			else if(dist >= NUM_RGB_CACHE)
				dist = NUM_RGB_CACHE - 1;
			driver.set_pixel(c, x, y, m->rgb_cache[(size_t)dist]);
		}
	return 0;
}

int metaballs_action(struct cgbp *c, void *data, char r) {
	if(r == 'q' || r == 'Q')
		c->running = 0;
	if(r == ' ')
		metaballs_init_color(data);
	return 0;
}

void metaballs_cleanup(struct metaballs *m) {
	size_t i;
	for(i = 0; i < NUM_BALLS; i++)
		if(m->balls[i].dist_cache != NULL)
			free(m->balls[i].dist_cache);
	return;
}

int main(void) {
	struct cgbp c;
	struct cgbp_callbacks cb = {
		.update = metaballs_update,
		.action = metaballs_action,
	};
	struct metaballs m;
	int ret = EXIT_FAILURE;
	srand(time(NULL));
	if(cgbp_init(&c) < 0 || metaballs_init(&m, driver.size(&c)) < 0)
		goto error;

	if(cgbp_main(&c, &m, cb) == 0)
		ret = EXIT_SUCCESS;
error:
	cgbp_cleanup(&c);
	metaballs_cleanup(&m);
	return ret;
}
