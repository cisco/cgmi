ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
# because the auto generated file will be put into the buildir.  WE NEVER write to the srcdir
VPATH = $(srcdir) $(builddir)


EXTRA_DIST = $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml
noinst_programs = gdbus-codegen 

#Note the client has the exact same generated file.
# we could optimize the build  by only generating this c file once.
# TODO.
# BUILD rule for cgmi_dbus_server_generated.c
$(builddir)/cgmi_dbus_server_generated.c: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/cgmi_dbus_server_generated $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml

$(builddir)/cgmi_dbus_server_generated.h: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/cgmi_dbus_server_generated $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml
BUILT_SOURCES =  $(builddir)/cgmi_dbus_server_generated.h

bin_PROGRAMS = cgmi-daemon
cgmi_daemon_SOURCES=cgmiDaemon.c
nodist_cgmi_daemon_SOURCES=cgmi_dbus_server_generated.c cgmi_dbus_server_generated.h

cgmi_daemon_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include
cgmi_daemon_LDFLAGS= $(LDFLAGS) 
#cgmi_daemon_LDFLAGS= $(LDFLAGS) -ldiagclient -lpthreaddiag -lstdc++ 
# Add the library for the actual player here.
#
cgmi_daemon_LDADD= $(LDFLAGS) $(top_builddir)/source/lib/libcgmiPlayer.la

