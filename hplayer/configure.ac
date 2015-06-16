#This is the minimum version of autoconf that is required to execute these scripts
AC_PREREQ([2.61])
AC_INIT([cgmiPlayer], [1.00.00.0],[snobym@cisco.com])


AC_ARG_WITH([gstreamer-api],
    AC_HELP_STRING([--with-gstreamer-api=VERSION],
                   [build against the specified GStreamer API version
                    @<:@default=0.10@:>@]),
    [GST_API_VERSION="$with_gstreamer_api"], [GST_API_VERSION=0.10])

case $GST_API_VERSION in
0.10)
    AC_MSG_NOTICE(Using Gstreamer API 0.10)
    ;;
1.0)
    AC_MSG_NOTICE(Using Gstreamer API 1.0)
    ;;
*)
    AC_MSG_ERROR([Unsupported GStreamer API, use 0.10 or 1.0])
    ;;
esac


AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
  [GStreamer API Version])
#
# If the OS is Linux then enable the GNU system extentions
#
AC_USE_SYSTEM_EXTENSIONS

# This will detect the host that is building this software, it will enable us to switch to cross compile mode
# when necessary
AC_CANONICAL_HOST

# Get system canonical name
AC_DEFINE_UNQUOTED(OS, "${host}", [cpu-machine-OS])


#Here in indicate to autoconf that we want automake to autogenerate our makefiles.
#It will also add the -Wall command to the compiler line.
AM_INIT_AUTOMAKE([ -Wall parallel-tests color-tests silent-rules foreign])

#This will hide the compiler and linker line by default
# If you want this shown just do a make with the arguement "V=1"
AM_SILENT_RULES([yes])
AM_PROG_AR

#This indicates that we want to use minimum version 2.2 for our libtool
LT_PREREQ([2.2])
LT_INIT([dlopen disable-static ])

#This is just used to get some configuration data it's also so that the
#configure script knows the directory on which it is being executed
#on is actually the project directory
AC_CONFIG_SRCDIR([source/lib/cgmi-player.c])

#this tells autoconf where to find the makefiles to execute.
AC_CONFIG_FILES([ Makefile
                  source/lib/Makefile
                  source/ipc/client/Makefile
                  source/ipc/daemon/Makefile
                  source/lib/cgmi.pc
                  ])

                  #source/ipc/daemon/Makefile
#This is an auto generated file that is genreated from configure.ac
#it will hold all the #defines for configuration varibles in this project.
#such things as PACKAGE_BUG_REPORT, PACKAGE_VERSION, and PACKAGE_NAME are contained here.
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AM_PROG_CC_C_O
AC_PROG_CXX

# Checks for size of definitions
AC_TYPE_INT8_T
AC_TYPE_UINT8_T
AC_TYPE_INT64_T
AC_TYPE_UINT64_T

# this will tell the autoconf system where the m4 Macro directory is
# it's best just to create this directory and have autoconf install
# the necessary scripts into this directory for you.
AC_CONFIG_MACRO_DIR([m4])

#
# Make sure we have pkg-config
PKG_PROG_PKG_CONFIG([0.25])
AS_IF([test -n "$PKG_CONFIG"],[ ],[AC_MSG_ERROR(This build requires pkg-config version "0.25" please install or upgrade it)])


# check for GNU only recursive mutexes
# This is an example so searching for specific functionality
# The variable P_HAS_GNU_MUTEXES will hold the answer in config.h
P_HAS_GNU_MUTEXES=0
AC_MSG_CHECKING(if gnu enhanced recursive mutexes are available)
AC_TRY_COMPILE(   [#include <pthread.h>],
                  [pthread_mutexattr_t attr;
               pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);],
               P_HAS_GNU_MUTEXES=1)
   if test ${P_HAS_GNU_MUTEXES} = 1; then
      AC_MSG_RESULT(yes)
   else
      AC_MSG_RESULT(no)
      fi
# The following will place define in config.h
#
#
#
AC_DEFINE_UNQUOTED(P_HAS_GNU_MUTEXES, ${P_HAS_GNU_MUTEXES},[Platform has non POSIX enhanced mutexes])


AC_CHECK_LIB([dl], [dlopen])

#
# The following is to enable or disable IPC for the cli test app.
#
AC_MSG_CHECKING(Build CLI with no IPC...)
AC_ARG_ENABLE([cli-noipc],
              [AS_HELP_STRING([--enable-cli-noipc],
                             [If set to =yes  the cli executable will link directly with CGMI and no IPC will be used ]) ],
                             [AC_MSG_RESULT(yes) ],
                             [AC_MSG_RESULT(no)])
AM_CONDITIONAL(CLI_NO_IPC,test x$enable_cli_noipc = xyes)

#
# The RDK emulator does NOT have the priorities set for the video sinks for some reason,
# this means that standard playbin can not autoplug we can enable the flag to automatically
# set fbdevsink as the video sink on the emulator.
#
AC_MSG_CHECKING(Set the video sink manually for the RDK emulator... )
AC_ARG_ENABLE([rdk-emulator],
              [AS_HELP_STRING([--enable-rdk-emulator],
                             [If set to =yes  the video-sink will be set to fbdevsink for playbin, otherwise it will be autoplugged]) ],
                             [AC_MSG_RESULT(yes)
                             AC_DEFINE([RDK_EMULATOR], [1], [RDK Emulator customizations enabled] )
                              ],
                             [AC_MSG_RESULT(no)])
AM_CONDITIONAL(RDK_EMULATOR,test x$enable_rdk_emulator = xyes)


AC_MSG_CHECKING(Build with DRMProxy...)
AC_ARG_ENABLE([use-drmproxy],
              [AS_HELP_STRING([--enable-use-drmproxy],
                             [If set to =yes drmproxy will link to cgmi library ]) ],
                             [AC_MSG_RESULT(yes)
                             AC_DEFINE([USE_DRMPROXY], [1], [DrmProxy enabled] )
                             ],
                             [AC_MSG_RESULT(no)])
AM_CONDITIONAL(USE_DRMPROXY,test x$enable_use_drmproxy = xyes)
AC_ARG_ENABLE([infinite-soup-timeout],
              AS_HELP_STRING([--enable-infinite-soup-timeout],[enable infinite-soup-timeout (default is no)]),
              [
                case "${enableval}" in
                 yes) AC_DEFINE([USE_INFINITE_SOUP_TIMEOUT], [1], [infinite-soup-timeout is enabled])
                      echo "infinite-soup-timeout is enabled";;
                 no)  AC_MSG_ERROR([infinite-soup-timeout is disabled]);;
                 *) AC_MSG_ERROR([bad value ${enableval} for --enable-infinite-soup-timeout]) ;;
                esac
              ],
              [echo "infinite-soup-timeout is disabled"])
#
# The following is to enable or disable timing metrics
#
AC_MSG_CHECKING(Build unity platform with timing metrics..)
AC_ARG_ENABLE([tmet],
              [AS_HELP_STRING([--enable-tmet],
                             [If set to =yes  the timing metrics will be included in CGMI ]) ],
                             [AC_MSG_RESULT(yes) ],
                             [AC_MSG_RESULT(no)])
AM_CONDITIONAL(TMET_ENABLED,test x$enable_tmet = xyes)

#
# Start Checking what packages we have available.
PKG_CHECK_MODULES([GLIB],    [glib-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GMODULE], [gmodule-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GTHREAD], [gthread-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GIO],     [gio-unix-2.0 >= 2.32.0])

AS_VERSION_COMPARE([$GST_API_VERSION], [0.10],
                   [],
                   [PKG_CHECK_MODULES([GST],[gstreamer-0.10 >= 0.10.30])],
                   [PKG_CHECK_MODULES([GST],[gstreamer-1.0 >= 1.2.0])])

AC_MSG_CHECKING(Checking to see if we should autodetect DLNA streams (enables libcurl) ... )
AC_ARG_ENABLE([dlna_autodetect],
              [AS_HELP_STRING([--enable-dlna-autodetect],
                             [If set to =yes  libcurl will be enabled to auto detect if the http url is DLNA  ]) ],
                             [AC_MSG_RESULT(yes)
                             AC_DEFINE([ENABLE_DLNA_AUTODETECT], [1], [DLNA autodection enabled] )
                              ],
                             [AC_MSG_RESULT(no)])
AM_CONDITIONAL(DLNA_AUTODETECT_ENABLED,test x$enable_dlna_autodetect = xyes)



# Add the CFLAGS and LDFLAGS for each package to the environment
CFLAGS="$CFLAGS $GLIB_CFLAGS $GOBJECT_CFLAGS $GMODULE_CFLAGS $GTHREAD_CFLAGS $GIO_CFLAGS $GST_CFLAGS "
LDFLAGS="$LDFLAG $GLIB_LIBS $GOBJECT_LIBS $GMODULE_LIBS $GTHREAD_LIBS $GIO_LIBS $GST_LIBS "

#check to see if we need to include libdiagtmets
if test x$enable_tmet = xyes; then
   PKG_CHECK_MODULES([LIBDIAGTMETS],     [libdiagtmets])
   CFLAGS+="$LIBDIAGTMETS_CFLAGS"
   LDFLAGS+="$LIBDIAGTMETS_LIBS"
fi

if test x$enable_dlna_autodetect = xyes; then
   PKG_CHECK_MODULES([CURL], [libcurl])
   CFLAGS+="$CURL_CFLAGS"
   LDFLAGS+="$CURL_LIBS"
fi
#
# Check for Doxygen for the documentation generation
#
AC_CHECK_PROGS([DOXYGEN], [doxygen])
if test -z "$DOXYGEN";
   then AC_MSG_WARN([Doxygen not found - continuing without Doxygen support])
fi

AM_CONDITIONAL([HAVE_DOXYGEN],
               [test -n "$DOXYGEN"])AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([doc/Doxyfile])])

AC_CONFIG_FILES([doc/Makefile])
AC_SUBST([DOXYGEN])
#
# make sure the dbus code generator is available
#
AC_PATH_PROG([GDBUS_CODEGEN], [gdbus-codegen], AC_MSG_ERROR([gdbus-codegen is required but was not found]))

# Must have AC_OUTPUT to have
# makefiles created.
AC_OUTPUT

# Here is a nice little print to tell the guy configuring your project
# how things are configured.
echo \
"-------------------------------------------------

 ${PACKAGE_NAME} Version ${PACKAGE_VERSION}

 Prefix: '${prefix}'.
 Compiler: '${CC}
Building for debug: '${debugit}'
 CC      : ${CC}
 CPP     : ${CPP}
 CFLAGS  : ${CFLAGS}
 CPPFLAGS  : ${CPPFLAGS}

 LINKER  : $LD $LDFLAGS $LIBS

 LD      : $LD
 LDFLAGS : $LDFLAGS
 LIBS    : $LIBS
 PTHREAD : $PTHREAD_LIBS


 Now type 'make @<:@<target>@:>@'
   where the optional <target> is:
     all                - build all binaries
     install            - install everything
     clean              - removes all built source files.
     distclean          - removes everything configure created

 Notes: To build in Verbose mode execute : make V=1

--------------------------------------------------"


