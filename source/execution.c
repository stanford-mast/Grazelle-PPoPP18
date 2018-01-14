/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* execution.c
*      Implementation of the top-level functions executed by this program.
*      Defines common variables used across algorithms.
*****************************************************************************/

#include <stdint.h>


/* -------- GLOBALS -------------------------------------------------------- */
// See "execution.h" for documentation.

uint64_t total_iterations_executed = 0ull;
uint64_t total_iterations_used_gather = 0ull;
uint64_t total_iterations_used_scatter = 0ull;
