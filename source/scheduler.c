/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* scheduler.h
*      Scheduling-related globals. Primarily affects the pull engine.
*****************************************************************************/

#include <stdint.h>


/* -------- GLOBALS -------------------------------------------------------- */
// See "scheduler.h" for documentation.

uint64_t sched_pull_units_per_node = 0ull;

uint64_t sched_pull_units_total = 0ull;
