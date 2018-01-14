/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* intrinhelper.h
*      Helpers for intrinsic operations that differ between platforms.
*****************************************************************************/

#ifndef __GRAZELLE_INTRINHELPER_H
#define __GRAZELLE_INTRINHELPER_H


#include "versioninfo.h"

#include <immintrin.h>


/* -------- PLATFORM-SPECIFIC MACROS --------------------------------------- */

#ifdef GRAZELLE_WINDOWS

#define _mm256_extract_epi64(__m256i_in, idx)   __m256i_in.m256i_u64[idx]

#else

// no equivalent definitions required for non-Microsoft compilers

#endif


#endif //__GRAZELLE_INTRINHELPER_H
