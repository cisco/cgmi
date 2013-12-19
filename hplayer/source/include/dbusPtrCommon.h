
#ifndef __CGMI_IPC_COMMON_H__
#define __CGMI_IPC_COMMON_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Check GCC architecture
#if __GNUC__
#if __x86_64__ || __ppc64__
#define ARCH_64_BIT
#else
#define ARCH_32_BIT
#endif
#endif

// Based on architecture define pointer data type for DBUS
#ifdef ARCH_64_BIT
#define tCgmiDbusPointer guint64
#define DBUS_POINTER_TYPE "t"
#else
#ifdef ARCH_32_BIT
#define tCgmiDbusPointer guint
#define DBUS_POINTER_TYPE "u"
#else
#error Unknown architecture?
#endif
#endif


#ifdef __cplusplus
}
#endif

#endif 