
/* reactdiff.c
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

struct reactdiff {
	struct rdxel {
		float a, b;
	} *abmap;
	float da, db, feed, kill;
	size_t l, t, w, h;
};

int reactdiff_init(struct cgbp *c, struct reactdiff *r) {
	struct cgbp_size size = driver.size(c);
	size_t i, x, y;
	r->w = 640;
	r->h = 480;
	r->l = (size.w - r->w) / 2;
	r->t = (size.h - r->h) / 2;
	r->abmap = malloc(sizeof *r->abmap * r->w * r->h);
	if(r->abmap == NULL) {
		perror("malloc");
		return -1;
	}
	r->da = 1.0;
	r->db = .5;
	r->feed = .055;
	r->kill = .062;
#define A_INIT 1
#define B_INIT 0
/*
	r->feed = .0207;
	r->kill = .0509;
#define A_INIT 1
#define B_INIT 0
*/
/*
	// this one features spiral waves after a couple minutes
	r->feed = .01;
	r->kill = .036;
#define A_INIT ((float)rand() / RAND_MAX * .78)
#define B_INIT ((float)rand() / RAND_MAX * .2)
*/
/*
	r->feed = .011;
	r->kill = .035;
#define A_INIT ((float)rand() / RAND_MAX * .84)
#define B_INIT ((float)rand() / RAND_MAX * .18)
*/
	for(i = 0; i < r->w * r->h; i++) {
		r->abmap[i].a = A_INIT;
		r->abmap[i].b = B_INIT;
	}
#define SEED_SIZE 100
	for(y = (r->h - SEED_SIZE) / 2; y < (r->h + SEED_SIZE) / 2; y++)
		for(x = (r->w - SEED_SIZE) / 2; x < (r->w + SEED_SIZE) / 2; x++) {
			r->abmap[y * r->w + x].a = 0;
			r->abmap[y * r->w + x].b = 1;
		}
	for(y = 0; (size_t)y < size.h; y++)
		for(x = 0; (size_t)x < size.w; x++)
			driver.set_pixel(c, x, y, 0x333333);
	return 0;
}

#define LAPLACE(v, p, x) ( \
	+ (v)[0].x / 20 + (v)[1].x / 5 + (v)[2].x / 20 \
	+ (v)[3].x / 5  - (p).x        + (v)[4].x / 5 \
	+ (v)[5].x / 20 + (v)[6].x / 5 + (v)[7].x / 20 \
)
static inline struct rdxel laplace(struct rdxel *neighbors, struct rdxel *p) {
	return (struct rdxel){
		.a = LAPLACE(neighbors, *p, a),
		.b = LAPLACE(neighbors, *p, b),
	};
}

static inline void get_neighbors(struct reactdiff *r, struct rdxel *neighbors,
                                 struct rdxel *pline, struct rdxel *pleft,
                                 size_t x, size_t y) {
	struct rdxel z = { 0, 0 };
	size_t pos = y * r->w + x;
	enum {
		TOP    = (1 << 0),
		BOTTOM = (1 << 1),
		LEFT   = (1 << 2),
		RIGHT  = (1 << 3),
	} a = (y == 0) * TOP | (y == r->h - 1) * BOTTOM |
	      (x == 0) * LEFT | (x == r->w - 1) * RIGHT;
	neighbors[0] = (a & (TOP|LEFT)) == 0 ? pline[x - 1] : z;
	neighbors[1] = (a & TOP) == 0 ? pline[x] : z;
	neighbors[2] = (a & (TOP|RIGHT)) == 0 ? pline[x + 1] : z;
	neighbors[3] = (a & LEFT) == 0 ? *pleft : z;
	//neighbors[4] = r->abmap[pos];
	neighbors[4] = (a & RIGHT) == 0 ? r->abmap[pos + 1] : z;
	pos += r->w;
	neighbors[5] = (a & (BOTTOM|LEFT)) == 0 ? r->abmap[pos - 1] : z;
	neighbors[6] = (a & BOTTOM) == 0 ? r->abmap[pos] : z;
	neighbors[7] = (a & (BOTTOM|RIGHT)) == 0 ? r->abmap[pos + 1] : z;
}

static inline uint32_t colorify(struct rdxel *ptr) {
	uint8_t a = (ptr->a > ptr->b ? ptr->a - ptr->b : 0) * 0xff;
/*
	return ((int)((ptr->a < 0 ? 0 : ptr->a > 1 ? 1 : ptr->a) * 0xff) << 16) |
	       ((int)(ptr->b < 0 ? 0 : ptr->b > 1 ? 1 : ptr->b) * 0xff);
	a = (ptr->a < 0 ? 0 : ptr->a > 1 ? 1 : ptr->a) * 0xff;
	b = (ptr->b < 0 ? 0 : ptr->b > 1 ? 1 : ptr->b) * 0xff;
	a = ptr->a * 0xff;
	b = ptr->b * 0xff;
*/
	return (a << 16) | (a << 8) | a;
}

int reactdiff_step(struct cgbp *c, struct reactdiff *r) {
	struct cgbp_size size = driver.size(c);
	struct rdxel pline1[r->w + 1], pline2[r->w + 1], *pline_new, *pline_old,
	             *pleft_new, *pleft_old, *tmp, lab, neighbors[8], *p;
	size_t x, y;
	float abb;
	pline_new = pline1;
	pline_old = pline2;
	pleft_new = &pline1[r->w];
	pleft_old = &pline2[r->w];
	for(y = 0; y < r->h; y++) {
		for(x = 0; x < r->w; x++)
			pline_new[x] = r->abmap[y * r->w + x];
		for(x = 0; x < r->w; x++) {
			p = &r->abmap[y * r->w + x];
			*pleft_new = *p;
			get_neighbors(r, neighbors, pline_old, pleft_old, x, y);
			lab = laplace(neighbors, p);
			abb = p->a * p->b * p->b;
			p->a += r->da * lab.a - abb + r->feed * (1 - p->a);
			p->b += r->db * lab.b + abb - (r->kill + r->feed) * p->b;
			if(r->l + x < size.w && r->t + y < size.h)
				driver.set_pixel(c, r->l + x, r->t + y, colorify(p));
			tmp = pleft_new;
			pleft_new = pleft_old;
			pleft_old = tmp;
		}
		tmp = pline_new;
		pline_new = pline_old;
		pline_old = tmp;
	}
	return 0;
}

int reactdiff_update(struct cgbp *c, void *data) {
	struct reactdiff *r = data;
	uint8_t i;
	for(i = 0; i < 8; i++)
		if(reactdiff_step(c, r) < 0)
			return -1;
	return 0;
}

int reactdiff_action(struct cgbp *c, void *data, char r) {
	if(r == 'q' || r == 'Q')
		c->running = 0;
	return 0;
	(void)data;
}

void reactdiff_cleanup(struct reactdiff *r) {
	free(r->abmap);
}

int main(void) {
	struct cgbp c;
	struct reactdiff r = { .abmap = NULL, };
	int ret = EXIT_FAILURE;
	srand(time(NULL));
	if(cgbp_init(&c) < 0 || reactdiff_init(&c, &r) < 0)
		goto error;
	if(cgbp_main(&c, &r,
	  (struct cgbp_callbacks){ reactdiff_update, reactdiff_action }) == 0)
		ret = EXIT_SUCCESS;
error:
	cgbp_cleanup(&c);
	reactdiff_cleanup(&r);
	return ret;
}
