ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
# because the auto generated file will be put into the buildir.  WE NEVER write to the srcdir
VPATH = $(srcdir) $(builddir)


EXTRA_DIST = $(top_srcdir)/source/ipc/xml/hplayer_dbus_api.xml
noinst_programs = gdbus-codegen 

lib_LTLIBRARIES = libhplayer-client.la

libhplayer_client_la_SOURCES= hplayer_dbus_client.c
nodist_libhplayer_client_la_SOURCES = hplayer_dbus_client_generated.c hplayer_dbus_client_generated.h

# hplayer_dbus_client_generated.c  is a built source that must be cleaned
CLEANFILES = hplayer_dbus_client_generated.c hplayer_dbus_client_generated.h

# BUILD rule for hplayer_dbus_client_generated.c
$(builddir)/hplayer_dbus_client_generated.c: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/hplayer_dbus_client_generated $(top_srcdir)/source/ipc/xml/hplayer_dbus_api.xml

$(builddir)/hplayer_dbus_client_generated.h: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/hplayer_dbus_client_generated $(top_srcdir)/source/ipc/xml/hplayer_dbus_api.xml
BUILT_SOURCES= $(builddir)/hplayer_dbus_client_generated.h 

#
# Generate the C file from the XML source file.
#
libhplayer_client_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
libhplayer_client_la_LDFLAGS= $(LDFLAGS) = -no-undefined  

bin_PROGRAMS = hplayer-client-dbus-test
hplayer_client_dbus_test_SOURCES= saildbustest.c
hplayer_client_dbus_test_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
hplayer_client_dbus_test_LDFLAGS = $(LDFLAGS) 
hplayer_client_dbus_test_DEPENDENCIES = libhplayer-client.la 
hplayer_client_dbus_test_LDADD = $(LDFLAGS) $(top_builddir)/source/ipc/client/libhplayer-client.la


