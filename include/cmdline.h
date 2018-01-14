/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* cmdline.h
*      Declaration of types and functions for parsing command-line arguments.
*****************************************************************************/

#ifndef __GRAZELLE_CMDLINE_H
#define __GRAZELLE_CMDLINE_H


#include <stdint.h>


/* -------- CONSTANTS ------------------------------------------------------ */

// Default values for the command-line settings; any option not specified here has no default
#define CMDLINE_DEFAULT_NUM_THREADS             0
#define CMDLINE_DEFAULT_NUM_ITERATIONS          1
#define CMDLINE_DEFAULT_SCHED_GRANULARITY       0

// Maximum number of NUMA nodes supported at the command line.
#define CMDLINE_MAX_NUM_NUMA_NODES              4


/* -------- TYPE DEFINITIONS ----------------------------------------------- */

// Contains the values for each possible command-line option.
typedef struct cmdline_opts_t
{
    char graph_input_filename_gather[1024];                 // 'i' -> required; filename of the graph input file, gather version, derived from the supplied name by adding "-pull"
    char graph_input_filename_scatter[1024];                // 'i' -> required; filename of the graph input file, scatter version, derived from the supplied name by adding "-push"
    
    char* graph_ranks_output_filename;                      // 'o' -> optional; filename of the output file that should contain ranks for each vertex

    uint32_t num_iterations;                                // 'N' -> optional; number of iterations of the algorithm to execute
    
    uint32_t num_threads;                                   // 'n' -> optional; number of worker threads to use while executing
	uint32_t num_numa_nodes;								// 'u' -> optional; number of NUMA nodes to use, inferred from the list
    uint32_t numa_nodes[CMDLINE_MAX_NUM_NUMA_NODES+1];      // 'u' -> optional; list of NUMA nodes to use
    
    uint64_t sched_granularity;                             // 's' -> optional; override default scheduling granularity behavior
} cmdline_opts_t;


/* -------- FUNCTIONS ------------------------------------------------------ */

// Accepts and parses an argc and argv[] combination, passed from main().
// Fills the command-line options structure, validates it, and returns on success.
// If there is a problem, prints an appropriate message and terminates the program.
void cmdline_parse_options_or_die(int argc, char* argv[]);

// Retrieves, by reference, the current settings that are in effect.
const cmdline_opts_t* const cmdline_get_current_settings();


#endif //__GRAZELLE_CMDLINE_H
