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

#ifndef __GRAZELLE_SCHEDULER_H
#define __GRAZELLE_SCHEDULER_H


#include <stdint.h>


/* -------- GLOBALS -------------------------------------------------------- */

// Number of units of work to create per node for the pull engine.
extern uint64_t sched_pull_units_per_node;

// Number of units of work to create in total for the pull engine.
extern uint64_t sched_pull_units_total;


#endif //__GRAZELLE_SCHEDULER_H
