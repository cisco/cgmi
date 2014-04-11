ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

lib_LTLIBRARIES = libcgmiPlayer.la
libcgmiPlayer_la_SOURCES= cgmi-player.c cgmi-section-filter.c
libcgmiPlayer_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include
libcgmiPlayer_la_LDFLAGS= $(LDFLAGS) -lgstapp-0.10 -ldrm-proxy
include_HEADERS = $(top_srcdir)/source/include/cgmiPlayerApi.h

