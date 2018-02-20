
/* hsv.h
 *
 * Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
 *
 * This software may be modified and distributed under the terms
 * of the ISC license.  See the LICENSE file for details.
 */

#ifndef HSV_H
#define HSV_H

#include <math.h>

static inline void hsv_to_rgb(double result[], double h, double s, double v) {
	double i, f, p, q, t;
	if(s == 0) {
		result[0] = v;
		result[1] = v;
		result[2] = v;
		return;
	}
	i = floor(h * 6.0);
	f = (h * 6.0) - i;
	p = v * (1.0 - s);
	q = v * (1.0 - s * f);
	t = v * (1.0 - s * (1.0-f));
	switch((int)i % 6) {
	case 0:
		result[0] = v;
		result[1] = t;
		result[2] = p;
		return;
	case 1:
		result[0] = q;
		result[1] = v;
		result[2] = p;
		return;
	case 2:
		result[0] = p;
		result[1] = v;
		result[2] = t;
		return;
	case 3:
		result[0] = p;
		result[1] = q;
		result[2] = v;
		return;
	case 4:
		result[0] = t;
		result[1] = p;
		result[2] = v;
		return;
	case 5:
		result[0] = v;
		result[1] = p;
		result[2] = q;
		return;
	}
}

#endif // HSV_H
