/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* execution.h
*      Declaration of the top-level functions executed by this program.
*      Includes the functions executed by worker and master threads.
*****************************************************************************/

#ifndef __GRAZELLE_EXECUTION_H
#define __GRAZELLE_EXECUTION_H


#include <stdint.h>


/* -------- GLOBALS -------------------------------------------------------- */

// Hardware measurements.
extern char* papi_events[];
extern int num_papi_events;
extern long long** papi_counter_values;

// Iteration statistics.
extern uint64_t total_iterations_executed;
extern uint64_t total_iterations_used_gather;
extern uint64_t total_iterations_used_scatter;


/* -------- ALGORITHM SELECTION -------------------------------------------- */

#if defined(BREADTH_FIRST_SEARCH)

#define SCALAR_REDUCE_OP(a,b)                   (((a) < (b)) ? (a) : (b))
#define execution_init                          execution_init_bfs
#define execution_cleanup                       execution_cleanup_bfs
#define execution_accumulator_bits_per_vertex   execution_accumulator_bits_per_vertex_bfs
#define execution_initialize_frontier_has_info  execution_initialize_frontier_has_info_bfs
#define execution_initialize_frontier_wants_info execution_initialize_frontier_wants_info_bfs
#define execution_initialize_vertex_accum       execution_initialize_vertex_accum_bfs
#define execution_initialize_vertex_prop        execution_initialize_vertex_prop_bfs
#define execution_impl                          execution_impl_bfs

#elif defined(CONNECTED_COMPONENTS)

#define SCALAR_REDUCE_OP(a,b)                   (((a) < (b)) ? (a) : (b))
#define execution_init                          execution_init_cc
#define execution_cleanup                       execution_cleanup_cc
#define execution_accumulator_bits_per_vertex   execution_accumulator_bits_per_vertex_cc
#define execution_initialize_frontier_has_info  execution_initialize_frontier_has_info_cc
#define execution_initialize_frontier_wants_info execution_initialize_frontier_wants_info_cc
#define execution_initialize_vertex_accum       execution_initialize_vertex_accum_cc
#define execution_initialize_vertex_prop        execution_initialize_vertex_prop_cc
#define execution_impl                          execution_impl_cc

#else

#define SCALAR_REDUCE_OP(a,b)                   ((a) + (b))
#define execution_init                          execution_init_pr
#define execution_cleanup                       execution_cleanup_pr
#define execution_accumulator_bits_per_vertex   execution_accumulator_bits_per_vertex_pr
#define execution_initialize_frontier_has_info  execution_initialize_frontier_has_info_pr
#define execution_initialize_frontier_wants_info execution_initialize_frontier_wants_info_pr
#define execution_initialize_vertex_accum       execution_initialize_vertex_accum_pr
#define execution_initialize_vertex_prop        execution_initialize_vertex_prop_pr
#define execution_impl                          execution_impl_pr

#endif


/* -------- FUNCTIONS ------------------------------------------------------ */

// Performs any needed initialization tasks to prepare for execution.
void execution_init();

// Cleans up after execution.
void execution_cleanup();

// Specifies the number of bits per vertex are required in the accumulator.
uint64_t execution_accumulator_bits_per_vertex();

// Initializes the HasInfo frontier for a group of 64 vertices starting with `base`.
// A bit-mask should be returned, with '1' meaning the vertex is in the frontier and '0' means it is not.
uint64_t execution_initialize_frontier_has_info(const uint64_t base);

// Initializes the WantsInfo frontier for a group of 64 vertices starting with `base`.
// A bit-mask should be returned, with '1' meaning the vertex is in the frontier and '0' means it is not.
uint64_t execution_initialize_frontier_wants_info(const uint64_t base);

// Initializes a vertex accumulator, given a vertex ID.
double execution_initialize_vertex_accum(const uint64_t id);

// Initializes a vertex property, given a vertex ID.
double execution_initialize_vertex_prop(const uint64_t id);

// Implements the PageRank/CC/BFS algorithms, depending on compile options. This function acts as a driver and sequences other operations by calling their respective functions.
void execution_impl(void* unused_arg);


#endif //__GRAZELLE_EXECUTION_H
