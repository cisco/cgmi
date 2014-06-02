ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

lib_LTLIBRARIES = libcgmiPlayer-@GST_API_VERSION@.la
libcgmiPlayer_@GST_API_VERSION@_la_SOURCES= cgmi-player.c cgmi-section-filter.c cgmi-uti.c
libcgmiPlayer_@GST_API_VERSION@_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include
libcgmiPlayer_@GST_API_VERSION@_la_LDFLAGS= $(LDFLAGS) -lgstapp-@GST_API_VERSION@
if USE_DRMPROXY
libcgmiPlayer_@GST_API_VERSION@_la_LDFLAGS+= -ldrm-proxy
endif
include_HEADERS = $(top_srcdir)/source/include/cgmiPlayerApi.h

