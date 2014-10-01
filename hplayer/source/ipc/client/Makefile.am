ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
# because the auto generated file will be put into the buildir.  WE NEVER write to the srcdir
VPATH = $(srcdir) $(builddir)


EXTRA_DIST = $(top_srcdir)/source/ipc/xml/cgmi_dbus_api.xml
noinst_programs = gdbus-codegen 

lib_LTLIBRARIES = libcgmi-client-@GST_API_VERSION@.la

libcgmi_client_@GST_API_VERSION@_la_SOURCES= cgmiClient.c
nodist_libcgmi_client_@GST_API_VERSION@_la_SOURCES = cgmi_dbus_client_generated.c cgmi_dbus_client_generated.h

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
libcgmi_client_@GST_API_VERSION@_la_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
libcgmi_client_@GST_API_VERSION@_la_LDFLAGS= $(LDFLAGS) = -no-undefined  

bin_PROGRAMS = cgmi-client-test-@GST_API_VERSION@ cgmi_cli-@GST_API_VERSION@

cgmi_client_test_@GST_API_VERSION@_SOURCES= cgmiClientTest.c
cgmi_client_test_@GST_API_VERSION@_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
cgmi_client_test_@GST_API_VERSION@_LDFLAGS = $(LDFLAGS) 
if CLI_NO_IPC
cgmi_client_test_@GST_API_VERSION@_DEPENDENCIES = $(top_builddir)/source/lib/libcgmiPlayer-@GST_API_VERSION@.la
cgmi_client_test_@GST_API_VERSION@_LDADD = $(LDFLAGS) $(top_builddir)/source/lib/libcgmiPlayer-@GST_API_VERSION@.la
else
cgmi_client_test_@GST_API_VERSION@_DEPENDENCIES = libcgmi-client-@GST_API_VERSION@.la 
cgmi_client_test_@GST_API_VERSION@_LDADD = $(LDFLAGS) $(top_builddir)/source/ipc/client/libcgmi-client-@GST_API_VERSION@.la
endif

cgmi_cli_@GST_API_VERSION@_SOURCES= cgmi_cli.c
cgmi_cli_@GST_API_VERSION@_CPPFLAGS = $(CFLAGS) -I$(top_srcdir)/source/include/ 
cgmi_cli_@GST_API_VERSION@_LDFLAGS = $(LDFLAGS) 
#cgmi_cli_@GST_API_VERSION@_LDFLAGS = $(LDFLAGS) -ldiagclient -lpthreaddiag -lstdc++ 
if CLI_NO_IPC
cgmi_cli_@GST_API_VERSION@_DEPENDENCIES = $(top_builddir)/source/lib/libcgmiPlayer-@GST_API_VERSION@.la
cgmi_cli_@GST_API_VERSION@_LDADD = $(LDFLAGS) $(top_builddir)/source/lib/libcgmiPlayer-@GST_API_VERSION@.la
else
cgmi_cli_@GST_API_VERSION@_DEPENDENCIES = libcgmi-client-@GST_API_VERSION@.la 
cgmi_cli_@GST_API_VERSION@_LDADD = $(LDFLAGS) $(top_builddir)/source/ipc/client/libcgmi-client-@GST_API_VERSION@.la
endif

if TMET_ENABLED
cgmi_cli_@GST_API_VERSION@_CPPFLAGS += -DTMET_ENABLED
endif
