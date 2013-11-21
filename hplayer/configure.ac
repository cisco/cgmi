#This is the minimum version of autoconf that is required to execute these scripts
AC_PREREQ([2.61])
#Here we show that the name of the project "example_template", the version "1.0" and the string used for bug reports "example_template_support@cisco.com"
AC_INIT([cgmiPlayer], [0.00.11.0],[snobym@cisco.com])
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

#This is just used to get some configuration data it's aslo so that the 
#configure script knows the directory on which it is being executed 
#on is actually the project directory
AC_CONFIG_SRCDIR([source/lib/cgmi-player.c])

#this tells autoconf where to find the makefiles to execute.
AC_CONFIG_FILES([ Makefile          
                  source/lib/Makefile
                  source/ipc/client/Makefile
                  source/ipc/daemon/Makefile
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
AC_DEFINE_UNQUOTED(P_HAS_GNU_MUTEXES, ${P_HAS_GNU_MUTEXES},[Platform has non POSIX enhanced mutexes])


AC_CHECK_LIB([dl], [dlopen])

#
# Start Checking what packages we have available.
PKG_CHECK_MODULES([GLIB],    [glib-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GOBJECT], [gobject-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GMODULE], [gmodule-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GTHREAD], [gthread-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GIO],     [gio-unix-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GST],     [gstreamer-0.10 >= 0.10.30])

# Add the CFLAGS and LDFLAGS for each package to the environment
CFLAGS="$CFLAGS $GLIB_CFLAGS $GOBJECT_CFLAGS $GMODULE_CFLAGS $GTHREAD_CFLAGS $GIO_CFLAGS $GST_CFLAGS"
LDFLAGS="$LDFLAG $GLIB_LIBS $GOBJECT_LIBS $GMODULE_LIBS $GTHREAD_LIBS $GIO_LIBS $GST_LIBS"

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
 CFLAGS  : ${CFLAGS} ${LIBCURL_CFLAGS}   
 CPPFLAGS  : ${CPPFLAGS} $LIBCURL_CPPFLAGS

 LINKER  : $LD $LDFLAGS $LIBS

 LD      : $LD
 LDFLAGS : $LDFLAGS ${LIBCURL}
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


