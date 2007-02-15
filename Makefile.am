# $Id$

bin_PROGRAMS=rtcmp
rtcmp_SOURCES=rtcmp.c partm.c perfcomp.c rt/rt.c adrt/adrt.c rayforce/rayforce.c
noinst_HEADERS=rtcmp.h perfcomp.h adrt/adrt.h rt/rt.h rayforce/rayforce.h

test: rtcmp
	./rtcmp -a -b -r -s /usr/brlcad/share/brlcad/7.?.0/db/ktank.g tank computer g17 engine
