/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* main.c
*      Program entry point. Causes command-line options to be parsed and
*      orchestrates the execution of the rest of the program.
*****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "benchmark.h"
#include "cmdline.h"
#include "execution.h"
#include "graphdata.h"
#include "numanodes.h"
#include "scheduler.h"
#include "threads.h"
#include "versioninfo.h"


/* -------- FUNCTIONS ------------------------------------------------------ */

// Program entry point.
int main(int argc, char* argv[])
{
    const cmdline_opts_t* cmdline_settings = NULL;
    double time_elapsed;
    uint64_t cycles_elapsed = 0ull;
#if !defined(CONNECTED_COMPONENTS) && !defined(BREADTH_FIRST_SEARCH)
    double test_sum;
#endif
#ifdef EXPERIMENT_ITERATION_PROFILE

#if defined(EXPERIMENT_THRESHOLD_WITHOUT_OUTDEGREES) && defined(EXPERIMENT_THRESHOLD_WITHOUT_COUNT)
#error "Cannot profile iterations with both outdegrees and count disabled for engine selection."
#elif defined(EXPERIMENT_THRESHOLD_WITHOUT_OUTDEGREES)
    char* iteration_profile_frontier_string = "HasInfo Vertices / Total Vertices";
#elif defined(EXPERIMENT_THRESHOLD_WITHOUT_COUNT)
    char* iteration_profile_frontier_string = "HasInfo Edges / Total Edges";
#else
    char* iteration_profile_frontier_string = "HasInfo (Vertices + Edges) / Total Edges";
#endif
    
#endif

#ifdef EXPERIMENT_STR
    printf("Experiments: %s\n", EXPERIMENT_STR);
#endif
    
    
    numanodes_initialize();
    
    cmdline_parse_options_or_die(argc, argv);
    cmdline_settings = cmdline_get_current_settings();
    
    
    execution_init();
    
    benchmark_start();
    cycles_elapsed = benchmark_rdtsc();
    
    graph_data_read_from_file(cmdline_settings->graph_input_filename_gather, cmdline_settings->graph_input_filename_scatter, cmdline_settings->num_numa_nodes, cmdline_settings->numa_nodes);
    
    if (0ull == cmdline_settings->sched_granularity)
    {
        sched_pull_units_per_node = (cmdline_settings->num_threads / cmdline_settings->num_numa_nodes) << 5ull;
    }
    else
    {
        sched_pull_units_per_node = graph_edges_gather_list_vector_count / (uint64_t)cmdline_settings->num_numa_nodes / cmdline_settings->sched_granularity;
        
        if (0ull == sched_pull_units_per_node)
        {
            printf("Unable to set requested scheduler granularity because the graph is too small.\n");
            return 1;
        }
    }
    
    sched_pull_units_total = sched_pull_units_per_node * cmdline_settings->num_numa_nodes;
    printf("Scheduler: total units = %llu, vectors per unit = %llu\n", (long long unsigned int)(sched_pull_units_total), (long long unsigned int)(graph_edges_gather_list_vector_count / sched_pull_units_total));
    
    graph_data_allocate_merge_buffers(cmdline_settings->num_threads, cmdline_settings->num_numa_nodes, cmdline_settings->numa_nodes);

#ifdef EXPERIMENT_ITERATION_STATS
    graph_data_allocate_stats(cmdline_settings->num_threads, cmdline_settings->numa_nodes[0]);
#endif
    
    cycles_elapsed = benchmark_rdtsc() - cycles_elapsed;
    time_elapsed = benchmark_stop();
    printf("Loading graph took %.2lfms.\n", time_elapsed);
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    printf("Not executing application, since this was a modelling experiment.\n");
    return 0;
#endif
    
    printf("Starting execution.\n");
    
#ifdef EXPERIMENT_ITERATION_PROFILE
    fprintf(stderr, "Iteration,Selected Engine,Edge Phase Execution Time (Cycles),%s\n", iteration_profile_frontier_string);
#endif
    
    benchmark_start();
    cycles_elapsed = benchmark_rdtsc();
    
    threads_spawn(cmdline_settings->num_threads, cmdline_settings->num_numa_nodes, cmdline_settings->numa_nodes, 0, execution_impl, NULL);
    
    cycles_elapsed = benchmark_rdtsc() - cycles_elapsed;
    time_elapsed = benchmark_stop();
    
    printf("Execution completed.\n");
    
    printf("\n------------ EXECUTION STATISTICS ------------\n");
    printf("%-25s = %.2lfms\n", "Running Time", time_elapsed);
#if !defined(CONNECTED_COMPONENTS) && !defined(BREADTH_FIRST_SEARCH)
    printf("%-25s = %.0lf Medges/sec\n", "Processing Rate", (double)graph_num_edges * (double)(cmdline_settings->num_iterations) / (double)time_elapsed / 1000.0);
#else
    printf("%-25s = %.0lf Medges/sec\n", "Effective Processing Rate", (double)graph_num_edges * (double)(total_iterations_executed) / (double)time_elapsed / 1000.0);
#endif
    
#if !defined(CONNECTED_COMPONENTS) && !defined(BREADTH_FIRST_SEARCH)
    test_sum = 0.0;
    for (uint64_t i = 0; i < graph_num_vertices; ++i)
    {
        test_sum += graph_vertex_props[i] * (0.0 == graph_vertex_outdegrees[i] ? (double)graph_num_vertices : graph_vertex_outdegrees[i]);
    }
    printf("%-25s = %.10lf\n", "PageRank Sum", test_sum);
#endif
    
    printf("%-25s = %llu\n", "Total Iterations", (long long unsigned int)total_iterations_executed);
    printf("%-25s = %llu\n", "Pull-Based Iterations", (long long unsigned int)total_iterations_used_gather);
    printf("%-25s = %llu\n", "Push-Based Iterations", (long long unsigned int)total_iterations_used_scatter);

    printf("----------------------------------------------\n");
        
#ifdef EXPERIMENT_ITERATION_STATS
    fprintf(stderr, "%s,%s,%s\n", "Iteration", "# Vectors", "Packing Efficiency");
    
    for (uint64_t i = 0; i < total_iterations_executed; ++i)
    {
        const uint64_t stat_iter_num_vectors = graph_stat_num_vectors_per_iteration[i];
        const double stat_iter_packing_efficiency = ((0ull == stat_iter_num_vectors) ? 0.0 : ((double)graph_stat_num_edges_per_iteration[i] / (4.0 * (double)stat_iter_num_vectors)));
        
        fprintf(stderr, "%llu,%llu,%lf\n", (long long unsigned int)(1ull + i), (long long unsigned int)stat_iter_num_vectors, stat_iter_packing_efficiency);
    }
#endif
    
    if (NULL != cmdline_settings->graph_ranks_output_filename)
    {
        graph_data_write_ranks_to_file(cmdline_settings->graph_ranks_output_filename);
    }
    
    execution_cleanup();
    
    return 0;
}
