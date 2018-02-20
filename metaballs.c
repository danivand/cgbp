
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
#include <math.h>

#include "cgbp.h"
#include "hsv.h"

#define NUM_BALLS 10
#define RADIUS_LOW(w)  ((w) / 20)
#define RADIUS_HIGH(w) ((w) / 8)
#define MAX_SPEED(w) (w / 100)

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define TO_RGB(r, g, b) \
	(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))


struct metaballs {
	struct {
		float radius;
		unsigned int x, y;
		int speed_x, speed_y;
	} balls[NUM_BALLS];
	float *dist_cache;
	uint32_t rgb_cache[256];
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

static inline void metaballs_init_color(struct metaballs *m) {
	uint16_t i;
	float rnd, mult, hue;
	double rgb[3] = { 0.0, 0.0, 0.0 };
	rnd = (float)rand() / RAND_MAX * 2.0;
	mult = .25 + (float)rand() / RAND_MAX * 1.75;
	for(i = 0; i < 256; i++) {
		if(rnd > 1.0)
			hue = (float)i / 255.0 - (rnd - 1.0);
		else
			hue = (float)i / 255.0 + rnd;
		hue *= mult;
		hsv_to_rgb(rgb, hue - floor(hue), 1.0, .8);
		m->rgb_cache[i] = TO_RGB(rgb[0] * 255, rgb[1] * 255, rgb[2] * 255);
	}
}

int metaballs_init(struct metaballs *m, struct cgbp_size size) {
	float fx, fy, dist;
	size_t i, l = size.w * size.h;
	m->dist_cache = malloc(l * sizeof *m->dist_cache);
	if(m->dist_cache == NULL) {
		perror("malloc");
		return -1;
	}
	for(i = 1; i < l; i++) {
		fx = i % size.w;
		fy = i / size.h;
		dist = rsqrt(fx * fx + fy * fy);
		m->dist_cache[i] = dist * size.w * 2 / 5;
	}
	for(i = 0; i < NUM_BALLS; i++) {
		m->balls[i].radius = (float)(
			RADIUS_LOW(size.w) + random() % (
				RADIUS_HIGH(size.w) -
				RADIUS_LOW(size.w)
			)
		) / NUM_BALLS;
		m->balls[i].x = random() % size.w;
		m->balls[i].y = random() % size.h;
		m->balls[i].speed_x = random() % MAX_SPEED(size.w);
		m->balls[i].speed_y = random() % MAX_SPEED(size.w);
	}
	metaballs_init_color(m);
	return 0;
}

static inline int metaballs_dist(struct metaballs *m, size_t x, size_t y,
                                 size_t width) {
	register int a, b;
	register size_t i;
	register float dist = 0;
	for(i = 0; i < NUM_BALLS; i++) {
		a = x - m->balls[i].x;
		b = y - m->balls[i].y;
		dist += m->dist_cache[ABS(a) + width * ABS(b)] * m->balls[i].radius;
	}
	if(dist > 255)
		return 255;
	return dist;
}

#define sqrt(x) (1 / (rsqrt(x)))

int metaballs_update(struct cgbp *c, void *data) {
	struct metaballs *m = data;
	struct cgbp_size size = driver.size(c->driver_data);
	size_t i, px, py;
	int dist;
	for(py = 0; py < size.h; py++)
		for(px = 0; px < size.w; px++)
			driver.set_pixel(c->driver_data, px, py, 0);
	for(i = 0; i < NUM_BALLS; i++) {
		m->balls[i].x += m->balls[i].speed_x;
		if(m->balls[i].x >= size.w) {
			m->balls[i].speed_x *= -1;
			m->balls[i].x += m->balls[i].speed_x;
		}
		m->balls[i].y += m->balls[i].speed_y;
		if(m->balls[i].y >= size.h) {
			m->balls[i].speed_y *= -1;
			m->balls[i].y += m->balls[i].speed_y;
		}
	}
	for(py = 0; py < size.h; py++)
		for(px = 0; px < size.w; px++)
			driver.set_pixel(c->driver_data, px, py, m->rgb_cache[
				metaballs_dist(m, px, py, size.w)
			]);
	return 0;
	(void)dist;
}

int metaballs_action(struct cgbp *c, void *data, char r) {
	if(r == 'q' || r == 'Q')
		c->running = 0;
	if(r == ' ')
		metaballs_init_color(data);
	return 0;
	(void)c;
}

void metaballs_cleanup(struct metaballs *m) {
	free(m->dist_cache);
}

int main(void) {
	struct cgbp c;
	struct cgbp_callbacks cb = {
		.update = metaballs_update,
		.action = metaballs_action,
	};
	struct metaballs m;
	int ret = EXIT_FAILURE;
	if(cgbp_init(&c) < 0 || metaballs_init(&m, driver.size(c.driver_data)) < 0)
		goto error;

	if(cgbp_main(&c, &m, cb) == 0)
		ret = EXIT_SUCCESS;
error:
	cgbp_cleanup(&c);
	metaballs_cleanup(&m);
	return ret;
}
