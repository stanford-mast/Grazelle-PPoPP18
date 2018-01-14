/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* allochelper.h
*      Helpers for portably allocating memory.
*****************************************************************************/

#ifndef __GRAZELLE_ALLOCHELPER_H
#define __GRAZELLE_ALLOCHELPER_H


#include "versioninfo.h"

#include <malloc.h>


/* -------- PLATFORM-SPECIFIC MACROS --------------------------------------- */

#ifdef GRAZELLE_WINDOWS

#define alloc_aligned_mem(sz, align)            _aligned_malloc(sz, align)
#define free_aligned_mem(ptr)                   _aligned_free(ptr)

#else

#define alloc_aligned_mem(sz, align)            memalign(align, sz)
#define free_aligned_mem(ptr)                   free(ptr)
    
#endif


#endif //__GRAZELLE_ALLOCHELPER_H
