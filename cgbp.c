
/* cgbp.c
 *
 * Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
 *
 * This software may be modified and distributed under the terms
 * of the ISC license.  See the LICENSE file for details.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cgbp.h"

#define CGBP_BACKEND_PATH_LEN (sizeof CGBP_BACKEND_PATH - 1)
#define CGBP_FPS 30

static inline struct timespec timespec_add(const struct timespec ts1,
                                           const struct timespec ts2) {
	register struct timespec result = ts1;
	register long sec;

	result.tv_nsec += ts2.tv_nsec;
	result.tv_sec += ts2.tv_sec;

	if(result.tv_nsec >= 1e9) {
		result.tv_sec += result.tv_nsec / 1e9;
		result.tv_nsec %= (long)1e9;
	}
	if(result.tv_nsec < 0) {
		sec = 1 + -(result.tv_nsec / 1e9);
		result.tv_nsec += 1e9 * sec;
		result.tv_sec -= sec;
	}
	return result;
}

#define timespec_diff(ts1, ts2) \
	(timespec_add(ts1, (struct timespec){ -(ts2).tv_sec, -(ts2).tv_nsec }))

#define timespec_double(ts) \
	((double)(ts).tv_sec + (double)(ts).tv_nsec / 1e9)

int cgbp_init(struct cgbp *c) {
	c->driver_data = NULL;
	c->timer_set = 0;
	c->running = 1;
	if(driver.init != NULL)
		c->driver_data = driver.init();
	if(c->driver_data == NULL) {
		cgbp_cleanup(c);
		return -1;
	}

	if(timer_create(CLOCK_MONOTONIC, NULL, &c->timerid) < 0) {
		perror("timer_create");
		cgbp_cleanup(c);
		return -1;
	}
	c->timer_set = 1;
	if(clock_gettime(CLOCK_MONOTONIC, &c->start_time) < 0) {
		perror("clock_gettime");
		return -1;
	}
	c->total_frametime = (struct timespec){ 0, 0 };
	c->num_frames = 0;
	return 0;
}

static char cgbp_ticked = 0;
void cgbp_alarm(int sig) {
	if(sig == SIGALRM)
		cgbp_ticked = 1;
}

int cgbp_main(struct cgbp *c, void *data, struct cgbp_callbacks cb) {
	struct sigaction sa = {
		.sa_flags = 0,
		.sa_handler = cgbp_alarm,
	};
	struct timespec frame_start, frame_end;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	timer_settime(c->timerid, 0, &(struct itimerspec){
		.it_value = { 0, 1e9 / CGBP_FPS },
		.it_interval = { 0, 1e9 / CGBP_FPS },
	}, NULL);

	do {
		if(cgbp_ticked == 0)
			sleep(1);
		cgbp_ticked = 0;
		if(clock_gettime(CLOCK_MONOTONIC, &frame_start) < 0) {
			perror("clock_gettime");
			return -1;
		}
		if(driver.update != NULL) {
			if(driver.update(c, data, cb) < 0)
				return -1;
		} else if(cb.update(c, data) < 0)
			return -1;
		if(clock_gettime(CLOCK_MONOTONIC, &frame_end) < 0) {
			perror("clock_gettime");
			return -1;
		}
		c->total_frametime = timespec_add(
			c->total_frametime,
			timespec_diff(frame_end, frame_start)
		);
		c->num_frames++;
	} while(c->running);

	timer_settime(c->timerid, 0, &(struct itimerspec){
		.it_value = { 0, 0 },
		.it_interval = { 0, 0 },
	}, NULL);
	return 0;
}

void cgbp_cleanup(struct cgbp *c) {
	struct timespec ts;
	double runtime;
	if(c->timer_set)
		timer_delete(c->timerid);
	if(c->driver_data != NULL)
		driver.cleanup(c->driver_data);

	if(clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
		perror("clock_gettime");
		return;
	}
	runtime = timespec_double(timespec_diff(ts, c->start_time));
	fprintf(stderr, "total runtime: %.2f\n", runtime);
	fprintf(stderr, "num frames: %zu\n", c->num_frames);
	fprintf(stderr, "FPS: %.2f\n", c->num_frames / runtime);
	fprintf(stderr, "time spent for calulation in total: %f\n",
	        timespec_double(c->total_frametime));
}
