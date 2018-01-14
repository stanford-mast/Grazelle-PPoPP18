/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* execution_pr.c
*      Implementation of the algorithm control flow for PageRank.
*****************************************************************************/

#include "cmdline.h"
#include "execution.h"
#include "graphdata.h"
#include "numanodes.h"
#include "phases.h"
#include "scheduler.h"
#include "threads.h"

#include <stdint.h>


/* -------- LOCALS --------------------------------------------------------- */

// Size of the reduce buffer for inter-phase and inter-thread communication. Measured in number of elements.
static uint64_t sz_reduce_buffer;

// Pointer to the reduce buffer itself.
static uint64_t* reduce_buffer;


/* -------- FUNCTIONS ------------------------------------------------------ */
// See "execution.h" for documentation.

void execution_init_pr()
{
    const cmdline_opts_t* cmdline_settings = cmdline_get_current_settings();
    
    sz_reduce_buffer = cmdline_settings->num_threads + (8 - (cmdline_settings->num_threads % 8));
    reduce_buffer = numanodes_malloc(sizeof(double) * sz_reduce_buffer, cmdline_settings->numa_nodes[0]);
    
    for (uint64_t i = 0; i < sz_reduce_buffer; ++i) reduce_buffer[i] = 0;
}

// ---------

void execution_cleanup_pr()
{
    numanodes_free((void *)reduce_buffer, sizeof(double) * sz_reduce_buffer);
}

// ---------

uint64_t execution_accumulator_bits_per_vertex_pr()
{
    // the accumulator stores a full vertex property
    return 8ull * sizeof(double);
}

// ---------

uint64_t execution_initialize_frontier_has_info_pr(const uint64_t base)
{
    return ~0ull;
}

// ---------

uint64_t execution_initialize_frontier_wants_info_pr(const uint64_t base)
{
    return ~0ull;
}

// ---------

double execution_initialize_vertex_accum_pr(const uint64_t id)
{
    return 0.0;
}

// ---------

double execution_initialize_vertex_prop_pr(const uint64_t id)
{
    return (1.0 / (double)graph_num_vertices) / (0.0 == graph_vertex_outdegrees[id] ? (double)graph_num_vertices : graph_vertex_outdegrees[id]);
}

// ---------

void execution_impl_pr(void* unused_arg)
{
    uint64_t num_iterations_used_gather = 0ull;
    uint64_t num_iterations_used_scatter = 0ull;
    
    uint64_t ctr = 0ull;
    
    for (ctr = 0; ctr < cmdline_get_current_settings()->num_iterations; ++ctr)
    {
#ifndef EXPERIMENT_VERTEX_ONLY
        /* Edge Phase */
        
#ifndef EXPERIMENT_EDGE_FORCE_PUSH
        // Pull engine is selected
        num_iterations_used_gather += 1ull;
        
        // reset the global variable accumulator
        phase_op_reset_global_accum();
        
        // perform the Edge-Pull phase
        perform_edge_pull_phase(graph_edges_gather_list_block_bufs_numa[threads_get_thread_group_id()][0], graph_edges_gather_list_block_counts_numa[threads_get_thread_group_id()][0]);
        threads_barrier();
        
#if !defined(EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE)
        // first thread performs the actual merge operation between potentially-overlapping accumulators
        if (0 == threads_get_global_thread_id())
        {
            edge_pull_op_merge_with_merge_buffer(graph_vertex_merge_buffer, sched_pull_units_total, graph_vertex_accumulators);
        }
#endif
        
        // now that the Edge-Pull phase is over, each thread must write its partial PageRank sum to the reduce buffer
        // then, during the combine phase initialization, they will all compute the constant PageRank offset to apply to each vertex's rank
        phase_op_write_global_accum_to_buf(reduce_buffer);
        
#if !defined(EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE)
        threads_merge_barrier();
#else
        threads_barrier();
#endif
#else
        // Push engine is selected (in PageRank, this only happens if an experiment forces its use)
        num_iterations_used_scatter += 1ull;
        
        // reset the global variable accumulator
        phase_op_reset_global_accum();
        
        // perform the Edge-Push phase
        perform_edge_push_phase(graph_edges_scatter_list_block_bufs_numa[threads_get_thread_group_id()][0], graph_edges_scatter_list_block_counts_numa[threads_get_thread_group_id()][0]);
        threads_barrier();
        
        // now that the Scatter phase is over, each thread must write its partial PageRank sum to the reduce buffer
        // then, during the combine phase initialization, they will all compute the constant PageRank offset to apply to each vertex's rank
        phase_op_write_global_accum_to_buf(reduce_buffer);
        
        threads_barrier();
#endif
        
#endif
#ifndef EXPERIMENT_EDGE_ONLY
        /* Vertex Phase */
        
        // perform the Vertex phase
        perform_vertex_phase(graph_vertex_first_numa[threads_get_thread_group_id()], graph_vertex_count_numa[threads_get_thread_group_id()], reduce_buffer);
        
        threads_barrier();
#endif
    }
    
    // algorithm complete, record the number of iterations run of each type
    if (0 == threads_get_global_thread_id())
    {
        total_iterations_executed = ctr;
        total_iterations_used_gather = num_iterations_used_gather;
        total_iterations_used_scatter = num_iterations_used_scatter;
    }
}
