# $Id$

bin_PROGRAMS=rtcmp
rtcmp_SOURCES=adrt/adrt.c dry/dry.c partm.c perfcomp.c rayforce/rayforce.c rt/rt.c rtcmp.c tri.c
noinst_HEADERS=adrt/adrt.h dry/dry.h perfcomp.h rayforce/rayforce.h rt/rt.h rtcmp.h tri.h

test: rtcmp
	./rtcmp -a -b -r -s /usr/brlcad/share/brlcad/7.?.0/db/ktank.g tank computer g17 engine
