
// lorenz.c

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "cgbp.h"
#include "hsv.h"

#define MIN_Z .2

#define SIGN(x) ((x) < 0 ? -1 : 1)
#define ABS(x) ((long)(x) < 0 ? -((long)(x)) : ((long)(x)))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARRAY_LENGTH(x) (sizeof (x) / sizeof *(x))
#define TO_RGB(r, g, b) \
	(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

struct point2d { float x, y; };
struct point3d { float x, y, z; };

struct lorenz {
	struct cam {
		float rotxz, rotyz, fac;
		struct point3d pos;
		float dist;
	} c;
	float dt, maxext;
	struct point_bucket {
		struct point3d p[1024];
		size_t num;
		struct point_bucket *next;
	} bucket, *last;
};

static inline struct point2d rotate(const struct point2d p, float rad) {
	float c = cosf(rad), s = sinf(rad);
	return (struct point2d){ p.x * c - p.y * s, p.y * c + p.x * s };
}

#define X_FROM_XZ (xz.x)
#define Z_FROM_XZ (xz.y)
#define Y_FROM_YZ (yz.x)
#define Z_FROM_YZ (yz.y)
struct point2d cam_vt(struct cam *c, struct point3d p) {
	struct point2d xz, yz;
	float fac;
	p.x -= c->pos.x;
	p.y -= c->pos.y;
	p.z -= c->pos.z;
	xz = rotate((struct point2d){ p.x, p.z }, c->rotxz);
	yz = rotate((struct point2d){ p.y, Z_FROM_XZ }, c->rotyz);
	fac = c->fac / Z_FROM_YZ;
	return (struct point2d){ fac * X_FROM_XZ, fac * Y_FROM_YZ };
}

void cam_updatepos(struct cam *c) {
	float angle = c->rotxz - M_PI;
	c->pos.x = sin(angle) * c->dist;
	c->pos.z = cos(angle) * c->dist;
	c->rotyz = atanf(c->pos.y / -c->dist);
}

void cam_dump(struct cam *c) {
	printf("cam xz %f; yz %f; fac %f; pos: (%f, %f, %f)\n",
	       c->rotxz, c->rotyz, c->fac, c->pos.x, c->pos.y, c->pos.z);
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

void draw_bounding_box(struct cgbp *cg, struct cgbp_size size, struct cam *c) {
	struct point3d box[8] = {
		{ -1, -1, -1 },
		{  1, -1, -1 },
		{  1,  1, -1 },
		{ -1,  1, -1 },
		{ -1, -1,  1 },
		{  1, -1,  1 },
		{  1,  1,  1 },
		{ -1,  1,  1 },
	};
	struct point2d box2d[8];
	size_t i, j;
	for(i = 0; i < 8; i++) {
		box2d[i] = cam_vt(c, box[i]);
		box2d[i].x += size.w / 2;
		box2d[i].y = size.h / 2 - box2d[i].y;
	}
	for(i = 0; i < 8; i++)
		for(j = i + 1; j < 8; j++)
			if((box[i].x == box[j].x) + (box[i].y == box[j].y) +
			  (box[i].z == box[j].z) == 2)
				draw_line(cg, size, box2d[i].x, box2d[i].y,
				          box2d[j].x, box2d[j].y, 0xffffff);
}

static inline int lorenz_append(struct lorenz *l, struct point3d *p) {
	if(l->last->num >= ARRAY_LENGTH(l->last->p)) {
		l->last->next = malloc(sizeof *l->last->next);
		if(l->last->next == NULL) {
			perror("malloc");
			return -1;
		}
		l->last = l->last->next;
		l->last->num = 0;
		l->last->next = NULL;
	}
	l->last->p[l->last->num++] = *p;
	return 0;
}

static inline struct point3d scale(float scale, struct point3d p) {
	return (struct point3d){ p.x / scale, p.y / scale, p.z / scale };
}

#define CENTERX(x, s) (x + (s).w / 2)
#define CENTERY(y, s) (y + (s).h / 2)
static inline void lorenz_draw(struct cgbp *c, struct cgbp_size size,
                               struct lorenz *l) {
	struct point2d new, old;
	struct point_bucket *cur;
	double rgb[3] = { 0 }, h = 0;
	size_t i;
	old = cam_vt(&l->c, scale(l->maxext, l->bucket.p[0]));
	for(cur = &l->bucket; cur != NULL; cur = cur->next) {
		if(cur->num == 0)
			continue;
		for(i = 0; i < cur->num; i++) {
			new = cam_vt(&l->c, scale(l->maxext, cur->p[i]));
			h += .001;
			while(h > 1) h -= 1;
			hsv_to_rgb(rgb, h, 1, 1);
			draw_line(c, size, CENTERX(old.x, size), CENTERY(old.y, size),
			          CENTERX(new.x, size), CENTERY(new.y, size),
			          TO_RGB(rgb[0] * 0xff, rgb[1] * 0xff, rgb[2] * 0xff));
			old = new;
		}
	}
}

int lorenz_update(struct cgbp *c, void *data) {
	struct lorenz *l = data;
	struct cgbp_size size = driver.size(c);
	struct point3d o, param = { 10., 28., 8. / 3. }, d;
	size_t x, y;
	for(y = 0; y < size.h; y++)
		for(x = 0; x < size.w; x++)
			driver.set_pixel(c, x, y, 0);
	draw_bounding_box(c, size, &l->c);
	if(l->last->num == 0)
		o = (struct point3d){ -9.229547, -9.023968, 28.181185 };
	else
		o = l->last->p[l->last->num - 1];
	d.x = o.x + param.x * (o.y - o.x) * l->dt;
	d.y = o.y + (o.x * (param.y - o.z) - o.y) * l->dt;
	d.z = o.z + (o.x * o.y - param.z * o.z) * l->dt;
	if(fabsf(d.x) > l->maxext)
		l->maxext = fabsf(d.x);
	if(fabsf(d.y) > l->maxext)
		l->maxext = fabsf(d.y);
	if(fabsf(d.z) > l->maxext)
		l->maxext = fabsf(d.z);
	if(lorenz_append(l, &d) < 0)
		return -1;
	lorenz_draw(c, size, l);
	return 0;
}

int lorenz_action(struct cgbp *c, void *data, char r) {
	struct lorenz *l = data;
	char cammove = 0;
	if(r == 'q' || r == 'Q')
		c->running = 0;
	if(r == 'w' || r == 'W')
		l->c.dist = MAX(2.5, l->c.dist - .1), cammove = 1;
	if(r == 's' || r == 'S')
		l->c.dist = MIN(8, l->c.dist + .1), cammove = 1;
	if(r == 'a' || r == 'A')
		l->c.rotxz += M_PI / 180, cammove = 1;
	if(r == 'd' || r == 'D')
		l->c.rotxz -= M_PI / 180, cammove = 1;
	if(cammove)
		cam_updatepos(&l->c);
	return 0;
}

void lorenz_cleanup(struct lorenz *l) {
	struct point_bucket *b = l->bucket.next;
	while(b != NULL) {
		l->last = b->next;
		free(b);
		b = l->last;
	}
}

int main(void) {
	struct lorenz l = {
		.c = {
			.rotxz = 0, .rotyz = 0, .fac = 1,
			.pos = { 0, .75, -5 },
			.dist = 5,
		},
		.dt = .01,
		.bucket = {
			.p = { { 0, 0, 0 } },
			.num = 0,
			.next = NULL,
		},
		.last = &l.bucket,
	};
	struct cgbp c;
	struct cgbp_size size = { 0 };
	int ret;
	if(cgbp_init(&c) < 0)
		goto error;
	cam_updatepos(&l.c);
	size = driver.size(&c);
	l.c.fac = MIN(size.w, size.h);
	if(cgbp_main(&c, &l,
	  (struct cgbp_callbacks){ lorenz_update, lorenz_action }) == 0)
		ret = EXIT_SUCCESS;
error:
	cgbp_cleanup(&c);
	lorenz_cleanup(&l);
	return ret;
}
