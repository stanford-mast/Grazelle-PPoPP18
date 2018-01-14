/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* graphdata.h
*      Declaration of operations used to represent a graph in memory,
*      including reading it from a properly-formatted file and exporting it
*      back.
*****************************************************************************/

#ifndef __GRAZELLE_GRAPHDATA_H
#define __GRAZELLE_GRAPHDATA_H


#include "graphtypes.h"
#include "intrinhelper.h"
#include "versioninfo.h"

#include <stdint.h>


/* -------- DATA STRUCTURES ------------------------------------------------ */

// Number of vertices in the graph
extern uint64_t graph_num_vertices;

// Number of edges in the graph
extern uint64_t graph_num_edges;

// Collection of vertex ranks
extern double* graph_vertex_props;

// Collection of vertex accumulators, used between the gather and combine phases
extern double* graph_vertex_accumulators;

// Collection of vertex outdegrees
extern double* graph_vertex_outdegrees;

// Graph frontier for "has_info", one bit per vertex.
extern uint64_t* graph_frontier_has_info;

// Graph frontier for "wants_info", one bit per vertex.
extern uint64_t* graph_frontier_wants_info;

// Number of vectors in the edge gather list
extern uint64_t graph_edges_gather_list_vector_count;

// Number of vectors in the edge scatter list
extern uint64_t graph_edges_scatter_list_vector_count;

// Vertex merge buffers
extern mergeaccum_t* graph_vertex_merge_buffer;

// Per-NUMA-node base pointers into the merge buffers
extern mergeaccum_t** graph_vertex_merge_buffer_baseptr_numa;

// Edge gather list block buffer pointers, NUMA-aware
extern __m256i*** graph_edges_gather_list_block_bufs_numa;

// Edge scatter list block buffer pointers, NUMA-aware
extern __m256i*** graph_edges_scatter_list_block_bufs_numa;

// Record count for each block in the edge gather list, NUMA-aware
extern uint64_t** graph_edges_gather_list_block_counts_numa;

// Record count for each block in the edge scatter list, NUMA-aware
extern uint64_t** graph_edges_scatter_list_block_counts_numa;

// First destination vertex assignments for each NUMA node
extern uint64_t* graph_vertex_first_numa;

// Last destination vertex assignments for each NUMA node
extern uint64_t* graph_vertex_last_numa;

// Number of vertex assignments for each NUMA node
extern uint64_t* graph_vertex_count_numa;

// Vertex index, points to the starting vector index for each vertex in the edge gather list, one index per NUMA node.
extern uint64_t** graph_vertex_gather_index_numa;

// Vertex index start information, contains the first (lowest ID) valid vertex in the index for each NUMA node for the edge gather list.
extern uint64_t* graph_vertex_gather_index_start_numa;

// Vertex index end information, contains the last (highest ID) valid vertex in the index for each NUMA node for the edge gather list.
extern uint64_t* graph_vertex_gather_index_end_numa;

// Vertex index, points to the starting vector index for each vertex in the edge scatter list, one index per NUMA node.
extern uint64_t** graph_vertex_scatter_index_numa;

// Vertex index start information, contains the first (lowest ID) valid vertex in the index for each NUMA node for the edge scatter list.
extern uint64_t* graph_vertex_scatter_index_start_numa;

// Vertex index end information, contains the last (highest ID) valid vertex in the index for each NUMA node for the edge scatter list.
extern uint64_t* graph_vertex_scatter_index_end_numa;

// Dynamic scheduler counter pointers, one per NUMA node. Used to implement per-node local dynamic scheduling during the Processing phase.
extern uint64_t** graph_scheduler_dynamic_counter_numa;

// Statistics array, holds the number of edge vectors encountered in the current iteration, one entry per thread.
extern uint64_t* graph_stat_num_vectors_per_thread;

// Statistics array, holds the number of edges in total encountered per iteration, one entry per thread.
extern uint64_t* graph_stat_num_edges_per_thread;

// Statistics array, holds the number of edge vectors encountered per iteration.
extern uint64_t* graph_stat_num_vectors_per_iteration;

// Statistics array, holds the number of edges in total encountered per iteration.
extern uint64_t* graph_stat_num_edges_per_iteration;


/* -------- FUNCTIONS ------------------------------------------------------ */

// Reads graph data from a properly-formatted file and fills in the graph data structures.
// A file name is required.
void graph_data_read_from_file(const char* filename_gather, const char* filename_scatter, const uint32_t num_numa_nodes, const uint32_t* numa_nodes);

// Allocates accumulators for the currently-loaded graph.
void graph_data_allocate_accumulators(const uint64_t num_threads, const uint32_t num_numa_nodes, const uint32_t* numa_nodes);

// Allocates merge buffers for the currently-loaded graph.
void graph_data_allocate_merge_buffers(const uint64_t num_threads, const uint32_t num_numa_nodes, const uint32_t* numa_nodes);

// Allocates all statistics arrays.
// Useful only if running an experiment that collects statistics.
void graph_data_allocate_stats(const uint64_t num_threads, const uint32_t numa_node);

// Writes graph data to a text file.
// A file name is required.
void graph_data_write_to_file(const char* filename);

// Writes vertex ranks to a text file.
// A file name is required. This file will be overwritten if it exists.
void graph_data_write_ranks_to_file(const char* filename);

// Clears out the current graph.
void graph_data_clear();


#endif //__GRAZELLE_GRAPHDATA_H
