/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* floathelper.h
*      Helpers for manipulating floating-point values in ways generally not
*      supported in C.
*****************************************************************************/

#ifndef __GRAZELLE_FLOATHELPER_H
#define __GRAZELLE_FLOATHELPER_H


#include <stdint.h>


/* -------- MACROS --------------------------------------------------------- */

// Required within whatever scope uses any of the following macros, based on the data type.
// Declares a variable on the stack, so recommended use is once per function.
#define float_helper_scope_init()               union {uint32_t u; float f;} __floathelper__
#define double_helper_scope_init()              union {uint64_t u; double d;} __doublehelper__

// Performs bitwise logical operations on values of "float" type.
#define float_helper_and(floatval, anduint)     (__floathelper__.f = floatval, __floathelper__.u &= anduint, __floathelper__.f)
#define float_helper_or(floatval, anduint)      (__floathelper__.f = floatval, __floathelper__.u |= anduint, __floathelper__.f)
#define float_helper_xor(floatval, anduint)     (__floathelper__.f = floatval, __floathelper__.u ^= anduint, __floathelper__.f)

// Performs bitwise logical operations on values of "double" type.
#define double_helper_and(doubleval, anduint)   (__doublehelper__.d = doubleval, __doublehelper__.u &= anduint, __doublehelper__.d)
#define double_helper_or(doubleval, anduint)    (__doublehelper__.d = doubleval, __doublehelper__.u |= anduint, __doublehelper__.d)
#define double_helper_xor(doubleval, anduint)   (__doublehelper__.d = doubleval, __doublehelper__.u ^= anduint, __doublehelper__.d)


#endif //__GRAZELLE_FLOATHELPER_H
