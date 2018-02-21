
# cgbp makefile
#
# Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
#
# This software may be modified and distributed under the terms
# of the ISC license.  See the LICENSE file for details.

# use bmake instead.

CFLAGS = -D_DEFAULT_SOURCE $(PROD_CFLAGS)
LDFLAGS = $(PROD_LDFLAGS)
LDLIBS = -lrt
LDLIBS_xlib = -lX11
LDLIBS_epicycles = -lm

DRIVERS = fbdev xlib
TARGETS = langtonsant metaballs epicycles
BIN_TARGETS =

RM_FILES = cgbp.o

.MAIN: all

cgbp.o: cgbp.c

# build drivers
.for drv in $(DRIVERS)
$(drv): $(TARGETS:C/$/_$(drv)/)
drv_obj_$(drv) = $(drv:C/$/.o/)

$(drv_obj_$(drv)): $(drv:C/$/.c/) cgbp.h
	$(CC) $(CFLAGS) $(SHARED_CFLAGS) -c $<

RM_FILES += $(drv_obj_$(drv))
.endfor # drv in $(DRIVERS)

# build targets
.for target in $(TARGETS)

$(target:C/$/.o/): $(target:C/$/.c/) cgbp.h
RM_FILES += $(target:C/$/.o/)

# combine targets with drivers
.for drv in $(DRIVERS)
bin_$(target)_$(drv) = $(target:C/$/_$(drv)/)
$(bin_$(target)_$(drv)): cgbp.o $(target:C/$/.o/) $(drv:C/$/.o/)
	$(LINK) $(LDLIBS_$(drv)) $(LDLIBS_$(target))
RM_FILES += $(bin_$(target)_$(drv))
BIN_TARGETS += $(bin_$(target)_$(drv))
.endfor # drv in $(DRIVERS)

.endfor # target in $(TARGETS)

all: $(BIN_TARGETS)

clean:
	rm $(RM_FILES) || true

include ../global.mk

.PHONY: all clean $(DRIVERS)
