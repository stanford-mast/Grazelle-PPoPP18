/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* functionhelper.h
*      Helpers for ensuring assembly function compatibility across platforms.
*****************************************************************************/

#ifndef __GRAZELLE_FUNCTIONHELPER_H
#define __GRAZELLE_FUNCTIONHELPER_H


#include "versioninfo.h"


/* -------- PLATFORM-SPECIFIC MACROS --------------------------------------- */

#ifdef GRAZELLE_WINDOWS

#define __WRITTEN_IN_ASSEMBLY__  

#else

#define __WRITTEN_IN_ASSEMBLY__                 __attribute__((ms_abi))

#endif


/* -------- LANGUAGE-SPECIFIC MACROS --------------------------------------- */

#ifdef __cplusplus

#define C_COMPATIBLE_API                        extern "C"

#else

#define C_COMPATIBLE_API

#endif


#endif //__GRAZELLE_FUNCTIONHELPER_H
