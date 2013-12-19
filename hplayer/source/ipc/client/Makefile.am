ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
# because the auto generated file will be put into the buildir.  WE NEVER write to the srcdir
VPATH = $(srcdir) $(builddir)


EXTRA_DIST = $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml
noinst_programs = gdbus-codegen 

lib_LTLIBRARIES = libcgmi-client.la

libcgmi_client_la_SOURCES= cgmiClient.c
nodist_libcgmi_client_la_SOURCES = cgmi_dbus_client_generated.c cgmi_dbus_client_generated.h

# cgmi_dbus_client_generated.c  is a built source that must be cleaned
CLEANFILES = cgmi_dbus_client_generated.c cgmi_dbus_client_generated.h

# BUILD rule for cgmi_dbus_client_generated.c
$(builddir)/cgmi_dbus_client_generated.c: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/cgmi_dbus_client_generated $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml

$(builddir)/cgmi_dbus_client_generated.h: ${EXTRA_DIST} 
	gdbus-codegen --generate-c-code $(builddir)/cgmi_dbus_client_generated $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml
BUILT_SOURCES= $(builddir)/cgmi_dbus_client_generated.h 

#
# Generate the C file from the XML source file.
#
libcgmi_client_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
libcgmi_client_la_LDFLAGS= $(LDFLAGS) = -no-undefined  

bin_PROGRAMS = cgmi-client-test cgmi_cli

cgmi_client_test_SOURCES= cgmiClientTest.c
cgmi_client_test_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
cgmi_client_test_LDFLAGS = $(LDFLAGS) 
if CLI_NO_IPC
cgmi_client_test_DEPENDENCIES = $(top_builddir)/source/lib/libcgmiPlayer.la
cgmi_client_test_LDADD = $(LDFLAGS) $(top_builddir)/source/lib/libcgmiPlayer.la
else
cgmi_client_test_DEPENDENCIES = libcgmi-client.la 
cgmi_client_test_LDADD = $(LDFLAGS) $(top_builddir)/source/ipc/client/libcgmi-client.la
endif

cgmi_cli_SOURCES= cgmi_cli.c
cgmi_cli_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
cgmi_cli_LDFLAGS = $(LDFLAGS) 
#cgmi_cli_LDFLAGS = $(LDFLAGS) -ldiagclient -lpthreaddiag -lstdc++ 
if CLI_NO_IPC
cgmi_cli_DEPENDENCIES = $(top_builddir)/source/lib/libcgmiPlayer.la
cgmi_cli_LDADD = $(LDFLAGS) $(top_builddir)/source/lib/libcgmiPlayer.la
else
cgmi_cli_DEPENDENCIES = libcgmi-client.la 
cgmi_cli_LDADD = $(LDFLAGS) $(top_builddir)/source/ipc/client/libcgmi-client.la
endif
