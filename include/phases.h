/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* phases.h
*      Declaration of functions that implement execution phases and related
*      operations.
*****************************************************************************/

#ifndef __GRAZELLE_PHASES_H
#define __GRAZELLE_PHASES_H


#include "functionhelper.h"
#include "graphtypes.h"
#include "intrinhelper.h"

#include <stdint.h>


/* -------- ALGORITHM SELECTION -------------------------------------------- */

#if defined(BREADTH_FIRST_SEARCH)

#define perform_edge_pull_phase                 perform_edge_pull_phase_bfs
#define perform_edge_push_phase                 perform_edge_push_phase_bfs
#define perform_vertex_phase                    perform_vertex_phase_bfs

#elif defined(CONNECTED_COMPONENTS)

#define perform_edge_pull_phase                 perform_edge_pull_phase_cc
#define perform_edge_push_phase                 perform_edge_push_phase_cc
#define perform_vertex_phase                    perform_vertex_phase_cc

#else

#define perform_edge_pull_phase                 perform_edge_pull_phase_pr
#define perform_edge_push_phase                 perform_edge_push_phase_pr
#define perform_vertex_phase                    perform_vertex_phase_pr

#endif


/* -------- COMMON PHASE OPERATORS ----------------------------------------- */

// Resets the global variable accumulator.
// Should be called before a phase begins.
void phase_op_reset_global_accum() __WRITTEN_IN_ASSEMBLY__;

// Writes out the global variable accumulators to a reduce buffer.
// The buffer is assumed to be a reduce buffer indexed by thread ID.
void phase_op_write_global_accum_to_buf(uint64_t* reduce_buffer) __WRITTEN_IN_ASSEMBLY__;

// Combines partial values of a global variable, using summation, from the specified reduce buffer.
// Returns the fully-combined result.
uint64_t phase_op_combine_global_var_from_buf(uint64_t* reduce_buffer);


/* -------- EDGE-PULL ENGINE OPERATORS ------------------------------------- */

// Performs a merge to the accumulators based on all the entries in the merge buffer.
// Should only be called by a single thread on each NUMA node, but it does not matter which thread.
// The "count" parameter refers to the "merge_buffer" array; "vertex_accumulators" is indexed based on the contents of "merge_buffer" and is assumed to have enough entries.
// This function is written in C.
void edge_pull_op_merge_with_merge_buffer(mergeaccum_t* merge_buffer, uint64_t count, double* vertex_accumulators);


/* -------- PHASE CONTROL FUNCTIONS ---------------------------------------- */

// Performs the Edge-Pull phase.
// Specify the address and size (measured in 256-bit elements) of the edge vector list.
void perform_edge_pull_phase(const __m256i* edge_list, const uint64_t edge_list_count) __WRITTEN_IN_ASSEMBLY__;

// Performs the Edge-Push phase.
// Specify the address and size (measured in 256-bit elements) of the edge vector list.
void perform_edge_push_phase(const __m256i* edge_list, const uint64_t edge_list_count) __WRITTEN_IN_ASSEMBLY__;

// Performs the Vertex phase.
// Specify the range of vertices to process (as a starting vertex ID and count) and the address of the reduce buffer (optional, depends on the algorithm).
// Reduce buffer is used for global variable propagation from a previous phase and may be NULL if not used.
void perform_vertex_phase(const uint64_t vertex_start, const uint64_t vertex_count, const uint64_t* reduce_buffer) __WRITTEN_IN_ASSEMBLY__;


#endif //__GRAZELLE_PHASES_H
