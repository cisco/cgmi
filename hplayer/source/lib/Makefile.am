ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

lib_LTLIBRARIES = libhplayer.la
libhplayer_la_SOURCES=player.c gst-player.c
libhplayer_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include

libhplayer_la_LDFLAGS= $(LDFLAGS) 


bin_PROGRAMS = snobytest
snobytest_SOURCES=snobytest.c
snobytest_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include
snobytest_DEPENDENCIES = libhplayer.la 
snobytest_LDADD = $(LDFLAGS) $(top_builddir)/source/lib/libhplayer.la 
