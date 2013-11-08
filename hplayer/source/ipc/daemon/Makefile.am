ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
# because the auto generated file will be put into the buildir.  WE NEVER write to the srcdir
VPATH = $(srcdir) $(builddir)


EXTRA_DIST = $(top_srcdir)/source/ipc/xml/hplayer_dbus_api.xml
noinst_programs = gdbus-codegen 

#Note the client has the exact same generated file.
# we could optimize the build  by only generating this c file once.
# TODO.
# BUILD rule for hplayer_dbus_server_generated.c
$(builddir)/hplayer_dbus_server_generated.c: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/hplayer_dbus_server_generated $(top_srcdir)/source/ipc/xml/hplayer_dbus_api.xml

$(builddir)/hplayer_dbus_server_generated.h: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/hplayer_dbus_server_generated $(top_srcdir)/source/ipc/xml/hplayer_dbus_api.xml
BUILT_SOURCES =  $(builddir)/hplayer_dbus_server_generated.h

bin_PROGRAMS = hplayer-daemon
hplayer_daemon_SOURCES=sailDaemon.c recorder.c
nodist_hplayer_daemon_SOURCES=hplayer_dbus_server_generated.c hplayer_dbus_server_generated.h

hplayer_daemon_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include
hplayer_daemon_LDFLAGS= $(LDFLAGS) 
# Add the library for the actual player here.
#
hplayer_daemon_LDADD= $(LDFLAGS) $(top_builddir)/source/lib/libhplayer.la

