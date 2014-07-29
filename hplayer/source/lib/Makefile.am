ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

lib_LTLIBRARIES = libcgmiPlayer-@GST_API_VERSION@.la
libcgmiPlayer_@GST_API_VERSION@_la_SOURCES= cgmi-player.c cgmi-section-filter.c cgmi-uti.c cgmi-diags.c
libcgmiPlayer_@GST_API_VERSION@_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include
libcgmiPlayer_@GST_API_VERSION@_la_LDFLAGS= $(LDFLAGS) -lgstapp-@GST_API_VERSION@
if USE_DRMPROXY
libcgmiPlayer_@GST_API_VERSION@_la_LDFLAGS+= -ldrm-proxy
endif
apidir = $(includedir)/cgmi-@GST_API_VERSION@
api_HEADERS = $(top_srcdir)/source/include/cgmiPlayerApi.h

cgmi-@GST_API_VERSION@.pc: cgmi.pc
	mv -f $< $@

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = cgmi-@GST_API_VERSION@.pc

