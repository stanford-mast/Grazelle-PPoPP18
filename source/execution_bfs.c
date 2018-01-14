/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* execution_bfs.c
*      Implementation of the algorithm control flow for Breadth-First Search.
*****************************************************************************/

#include "benchmark.h"
#include "cmdline.h"
#include "execution.h"
#include "graphdata.h"
#include "numanodes.h"
#include "phases.h"
#include "threads.h"

#include <stdint.h>
#include <stdio.h>


/* -------- ALGORITHM PARAMETERS ------------------------------------------- */

#define SEARCH_ROOT                             0ull


/* -------- LOCALS --------------------------------------------------------- */

// Size of the reduce buffer for inter-phase and inter-thread communication. Measured in number of elements.
static uint64_t sz_reduce_buffer;

// Pointer to the reduce buffer itself.
static uint64_t* reduce_buffer;


/* -------- FUNCTIONS ------------------------------------------------------ */
// See "execution.h" for documentation.

void execution_init_bfs()
{
    const cmdline_opts_t* cmdline_settings = cmdline_get_current_settings();
    
    sz_reduce_buffer = cmdline_settings->num_threads + (8 - (cmdline_settings->num_threads % 8));
    reduce_buffer = numanodes_malloc(sizeof(double) * sz_reduce_buffer, cmdline_settings->numa_nodes[0]);
    
    for (uint64_t i = 0; i < sz_reduce_buffer; ++i) reduce_buffer[i] = 0ull;
}

// ---------

void execution_cleanup_bfs()
{
    numanodes_free((void *)reduce_buffer, sizeof(double) * sz_reduce_buffer);
}

// ---------

uint64_t execution_accumulator_bits_per_vertex_bfs()
{
    // one bit per vertex is required in the accumulator, which just stores HasInfo*
    return 1ull;
}

// ---------

uint64_t execution_initialize_frontier_has_info_bfs(const uint64_t base)
{
    const uint64_t top = base + 63ull;
    
    if (SEARCH_ROOT >= base && SEARCH_ROOT <= top)
    {
        // the search root has info
        return (1ull << (SEARCH_ROOT - base));
    }
    else
    {
        // all other vertices do not have info
        return 0ull;
    }
}

// ---------

uint64_t execution_initialize_frontier_wants_info_bfs(const uint64_t base)
{    
    const uint64_t top = base + 63ull;
    
    if (SEARCH_ROOT >= base && SEARCH_ROOT <= top)
    {
        // the search root does not want info
        return (~(1ull << (SEARCH_ROOT - base)));
    }
    else
    {
        // all other vertices want info
        return ~0ull;
    }
}

// ---------

double execution_initialize_vertex_accum_bfs(const uint64_t id)
{
    return 0.0;
}

// ---------

double execution_initialize_vertex_prop_bfs(const uint64_t id)
{
    return (double)-1.0;
}

// ---------

void execution_impl_bfs(void* unused_arg)
{
    uint64_t num_iterations_used_gather = 0ull;
    uint64_t num_iterations_used_scatter = 0ull;
    
    uint64_t ctr = 0ull;
    
#ifdef EXPERIMENT_ITERATION_PROFILE
    uint64_t iteration_time = 0ull;
    double iteration_frontier_comparator = (double)graph_num_edges;
#endif

    uint64_t converge_vote = 0ull;
    
#ifndef EXPERIMENT_THRESHOLD_WITHOUT_OUTDEGREES
    converge_vote += (uint64_t)graph_vertex_outdegrees[SEARCH_ROOT];
#else
#ifdef EXPERIMENT_ITERATION_PROFILE
    iteration_frontier_comparator = (double)graph_num_vertices;
#endif
#endif

#ifndef EXPERIMENT_THRESHOLD_WITHOUT_COUNT
    converge_vote += 1ull;
#endif
    
    while(1)
    {
        ctr += 1ull;
        
#if defined(EXPERIMENT_EDGE_FORCE_PULL)
        const uint8_t use_gather_for_processing = 1;
#elif defined(EXPERIMENT_EDGE_FORCE_PUSH)
        const uint8_t use_gather_for_processing = 0;
#else
#ifdef EXPERIMENT_THRESHOLD_WITHOUT_OUTDEGREES
        // without using outdegrees, the condition is based on the number of vertices
        // converge_vote gets set to the number of vertices that "have info"
        const uint64_t engine_threshold = (graph_num_vertices / 2ull);
#else
        // when using outdegrees, the condition is based on the number of edges
        // converge_vote gets set to the number of vertices that "have info" plus their outdegrees
        // Ligra uses this method, comparing converge_vote to (graph_num_edges / 20)    
        const uint64_t engine_threshold = (graph_num_edges / 5ull);
#endif
        
        // dynamically select an engine (either Push or Pull) depending on some condition
        const uint8_t use_gather_for_processing = (converge_vote > engine_threshold);
#endif
        
        /* Edge Phase */
        
#ifdef EXPERIMENT_ITERATION_PROFILE
        if (0 == threads_get_global_thread_id())
        {
            iteration_time = benchmark_rdtsc();
        }
#endif
        
        // reset the global variable accumulator
        phase_op_reset_global_accum();
        
        if (use_gather_for_processing)
        {
            // if the threshold is met, use the Gather phase to do this round of processing
            num_iterations_used_gather += 1ull;
            
            // perform the Edge-Pull phase
            perform_edge_pull_phase(graph_edges_gather_list_block_bufs_numa[threads_get_thread_group_id()][0], graph_edges_gather_list_block_counts_numa[threads_get_thread_group_id()][0]);
            threads_barrier();
        }
        else
        {
            // otherwise, use the Edge-Push phase to do this round of processing
            num_iterations_used_scatter += 1ull;
            
            // perform the Edge-Push phase
            perform_edge_push_phase(graph_edges_scatter_list_block_bufs_numa[threads_get_thread_group_id()][0], graph_edges_scatter_list_block_counts_numa[threads_get_thread_group_id()][0]);
            threads_barrier();
        }
        
        // each thread would have a partial value for the global variable which represents the number of vertices changed this algorithm iteration
        // therefore, each thread should write the partial value to the reduce buffer
        phase_op_write_global_accum_to_buf(reduce_buffer);
        
        threads_barrier();
        
#ifdef EXPERIMENT_ITERATION_PROFILE
        if (0 == threads_get_global_thread_id())
        {
            iteration_time = benchmark_rdtsc() - iteration_time;
            fprintf(stderr, "%llu,%s,%llu,%.10lf\n", (long long unsigned int)ctr, (use_gather_for_processing ? "Pull" : "Push"), (long long unsigned int)iteration_time, (double)converge_vote / iteration_frontier_comparator);
        }
        
        threads_barrier();
#endif

#ifdef EXPERIMENT_ITERATION_STATS
        if (0 == threads_get_global_thread_id())
        {
            const uint32_t num_threads = threads_get_total_threads();
            uint64_t num_vectors = 0ull;
            uint64_t num_edges = 0ull;
            
            for (uint32_t i = 0; i < num_threads; ++i)
            {
                num_vectors += graph_stat_num_vectors_per_thread[i];
                num_edges += graph_stat_num_edges_per_thread[i];
                
                graph_stat_num_vectors_per_thread[i] = 0ull;
                graph_stat_num_edges_per_thread[i] = 0ull;
            }
            
            graph_stat_num_vectors_per_iteration[ctr - 1ull] = num_vectors;
            graph_stat_num_edges_per_iteration[ctr - 1ull] = num_edges;
        }
        
        threads_barrier();
#endif
        
        /* Termination Check */
        
        // get the number of vertices that were visited this algorithm iteration
        // if that number is 0, the algorithm is complete and the parent values are final
        converge_vote = phase_op_combine_global_var_from_buf(reduce_buffer);
        if (0 == converge_vote)
            break;
        
        
        /* Vertex Phase */
        
        // only the master thread has to do this, but the idea is to swap the accumulators (HasInfo*) and HasInfo itself
        // it is easier to do this by swapping pointers than by moving data around
        if (0 == threads_get_global_thread_id())
        {
            uint64_t* temp = graph_frontier_has_info;
            graph_frontier_has_info = (uint64_t*)graph_vertex_accumulators;
            graph_vertex_accumulators = (double*)temp;
        }
        
        threads_barrier();
        
        // perform the Vertex phase
        // the only job of this phase is to zero out HasInfo* for the next iteration
        perform_vertex_phase(graph_vertex_first_numa[threads_get_thread_group_id()], graph_vertex_count_numa[threads_get_thread_group_id()], NULL);
        
        threads_barrier();
    }
    
    // algorithm complete, record the number of iterations run of each type
    if (0 == threads_get_global_thread_id())
    {
        total_iterations_executed = ctr;
        total_iterations_used_gather = num_iterations_used_gather;
        total_iterations_used_scatter = num_iterations_used_scatter;
    }
}
