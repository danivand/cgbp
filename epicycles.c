
// epicycles.c

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "cgbp.h"

#define STEP_DIV 256

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define TO_RGB(r, g, b) \
	(((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

struct epicycle {
	size_t cx, cy, scale, step;
};

void epicycles_init(struct cgbp *c, struct epicycle *e) {
	struct cgbp_size size = driver.size(c->driver_data);
	e->cx = size.w / 2;
	e->cy = size.h / 2;
	e->scale = MIN(size.w, size.h) / 4;
	e->step = 0;
}

static inline uint32_t darken(uint32_t color) {
	uint8_t channels[3] = { color >> 16, color >> 8, color };
	return TO_RGB(
		channels[0] - (channels[0] > 0), channels[1] - (channels[1] > 0),
		channels[2] - (channels[2] > 0));
}

int epicycles_update(struct cgbp *c, void *data) {
	struct cgbp_size size = driver.size(c->driver_data);
	struct epicycle *e = data;
	size_t x, y;
	float fx, fy;

	if(e->step % 8 != 0)
		goto skip_darken;
	for(y = 0; y < size.h; y++)
		for(x = 0; x < size.w; x++)
			driver.set_pixel(c->driver_data, x, y,
			                 darken(driver.get_pixel(c->driver_data, x, y)));
skip_darken:
	fx = cos((double)e->step / STEP_DIV) +
	     cos((double)e->step * 7 / STEP_DIV) / 2 +
	     sin((double)e->step * 17 / STEP_DIV) / 3;
	fy = sin((double)e->step / STEP_DIV) +
	     sin((double)e->step * 7 / STEP_DIV) / 2 +
	     cos((double)e->step * 17 / STEP_DIV) / 3;
	driver.set_pixel(c->driver_data, fx * e->scale + e->cx, fy * e->scale + e->cy, 0xffffff);
	e->step++;
	return 0;
}

int epicycles_action(struct cgbp *c, void *data, char r) {
	if(r == 'q' || r == 'Q')
		c->running = 0;
	return 0;
	(void)data;
}


// http://www.jstor.org/stable/2691465?origin=crossref&seq=1#page_scan_tab_contents

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
