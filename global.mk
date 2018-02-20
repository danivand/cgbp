
# global.mk
#
# Copyright (c) 2018, mar77i <mar77i at protonmail dot ch>
#
# This software may be modified and distributed under the terms
# of the ISC license.  See the LICENSE file for details.

# useful compiler flags; thanks xavier roche @ httrack blog

CFLAGS += -std=c99 -pipe -fvisibility=hidden -Wall -Wextra -Wformat \
	-Wformat-security -Wreturn-type -Wpointer-arith -Winit-self \
	-Wsign-compare -Wmultichar -Wuninitialized -Werror -funroll-loops \
	-funswitch-loops -pedantic

SHARED_CFLAGS = -fPIC -fvisibility=default

LDFLAGS += -Wl,-z,relro,-z,now,-O1,--as-needed,--no-undefined \
	-Wl,--build-id=sha1,--no-allow-shlib-undefined -rdynamic

VALGRIND_FLAGS = --trace-children=yes --leak-check=full --track-origins=yes \
	--show-leak-kinds=all

LINK = $(CC) $(LDFLAGS) $> $^ $(LOADLIBES) $(LDLIBS) $(LDADD) -o $@
LINK_SHARED = \
	$(CC) $(LDFLAGS) $> $^ $(LOADLIBES) $(LDLIBS) $(LDADD) -shared -o $@
LINK_STATIC = ar rcs $@ $> $^

vg:
	valgrind $(VALGRIND_FLAGS) $(ARGS)

DEV_CFLAGS = -Og -g
PROD_CFLAGS = -O3
PROD_LDFLAGS = -Wl,--discard-all

.PHONY: vg
