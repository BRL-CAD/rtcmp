# $Id$

AC_INIT
AC_CONFIG_SRCDIR([rtcmp.c])
AC_CANONICAL_HOST
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE(rtcmp, 0.0)
AM_CONFIG_HEADER([config.h])

AC_PROG_CC
AC_PROG_RANLIB
AC_PROG_INSTALL

# top_builddir is automatically added, but we need top_srcdir, too
CFLAGS="$CFLAGS "'-I${top_srcdir}'

# gutted from BRL-CAD
thread_link_works=no
LIBTHREAD=""
AC_CHECK_LIB(c_r, pthread_create, thread_link_works=yes ; LIBTHREAD="-pthread",
    dnl try solaris threading
    [AC_CHECK_LIB(thread, thr_create, thread_link_works=yes ; LIBTHREAD="-lthread",
        dnl pthreading is a good default if none of the others are available
        dnl XXX need to test HPUX -- may require -lcma with pthreads
        [AC_CHECK_LIB(pthread, pthread_create, thread_link_works=yes ; LIBTHREAD="-lpthread")])])

# should probably do something if no threading is found... meh
AC_SUBST(LIBTHREAD)

AC_ARG_WITH(brlcad,
	    [ --with-brlcad		Path to BRL-CAD install.],
	    BRLCAD_PATH="$withval"
	    BRLCAD_CFLAGS="-I$withval/include -I$withval/include/brlcad"
	    BRLCAD_LDFLAGS="-L$withval/lib",
	    BRLCAD_PATH="/usr/brlcad"
	    BRLCAD_CFLAGS="-I/usr/brlcad/include -I/usr/brlcad/include/brlcad"
	    BRLCAD_LDFLAGS="-L/usr/brlcad/lib")

# Make sure we have BRL-CAD available... 
CFLAGS="$CFLAGS $BRLCAD_CFLAGS"
LDFLAGS="$LDFLAGS $BRLCAD_LDFLAGS $LIBTHREAD"
LIBS="$LIBS -lbu -lbn -lrt"
AC_CHECK_LIB(rt, rt_prep,,AC_ERROR(Need BRL-CAD),[${LIBTHREAD} -lm -lbu -lbn -ltcl])

# Look for libtie/adrt
SAVED_LIBS="$LIBS"
TIELIBS="$BRLCAD_PATH/lib/librt.a"
LIBS="$LIBS $TIELIBS -lm"
AC_MSG_CHECKING(checking for TIE in librt)
AC_TRY_LINK([
	#include "brlcad/tie.h"
	],[tie_prep0((struct tie_s *)(0));],
	AC_MSG_RESULT(yes); AC_DEFINE(HAVE_TIE,1,"Use TIE/ADRT"),
	AC_MSG_RESULT(no); LIBS="$SAVED_LIBS")

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
