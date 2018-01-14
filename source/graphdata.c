/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* graphdata.c
*      Implementation of operations used to represent a graph in memory,
*      including reading it from a properly-formatted file and exporting it
*      back.
*****************************************************************************/

#include "allochelper.h"
#include "execution.h"
#include "floathelper.h"
#include "intrinhelper.h"
#include "numanodes.h"
#include "phases.h"
#include "scheduler.h"
#include "threads.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


/* -------- DATA STRUCTURES ------------------------------------------------ */
// See "graphdata.h" for documentation.

uint64_t graph_num_vertices = 0ull;
uint64_t graph_num_edges = 0ull;
double* graph_vertex_props = NULL;
double* graph_vertex_accumulators = NULL;
double* graph_vertex_outdegrees = NULL;
uint64_t* graph_frontier_has_info = NULL;
uint64_t* graph_frontier_wants_info = NULL;
uint64_t graph_edges_gather_list_vector_count = 0ull;
uint64_t graph_edges_scatter_list_vector_count = 0ull;
uint64_t graph_edges_gather_list_num_blocks = 0ull;
uint64_t graph_edges_scatter_list_num_blocks = 0ull;
uint64_t* graph_edges_gather_list_block_first_dest_vertex = NULL;
uint64_t* graph_edges_scatter_list_block_first_source_vertex = NULL;
uint64_t* graph_edges_gather_list_block_last_dest_vertex = NULL;
uint64_t* graph_edges_scatter_list_block_last_source_vertex = NULL;
mergeaccum_t* graph_vertex_merge_buffer = NULL;
mergeaccum_t** graph_vertex_merge_buffer_baseptr_numa = NULL;
__m256i*** graph_edges_gather_list_block_bufs_numa = NULL;
__m256i*** graph_edges_scatter_list_block_bufs_numa = NULL;
uint64_t** graph_edges_gather_list_block_counts_numa = NULL;
uint64_t** graph_edges_scatter_list_block_counts_numa = NULL;
uint64_t* graph_vertex_first_numa = NULL;
uint64_t* graph_vertex_last_numa = NULL;
uint64_t* graph_vertex_count_numa = NULL;
uint64_t** graph_vertex_gather_index_numa = NULL;
uint64_t* graph_vertex_gather_index_start_numa = NULL;
uint64_t* graph_vertex_gather_index_end_numa = NULL;
uint64_t** graph_vertex_scatter_index_numa = NULL;
uint64_t* graph_vertex_scatter_index_start_numa = NULL;
uint64_t* graph_vertex_scatter_index_end_numa = NULL;
uint64_t** graph_scheduler_dynamic_counter_numa = NULL;
uint64_t* graph_stat_num_vectors_per_thread = NULL;
uint64_t* graph_stat_num_edges_per_thread = NULL;
uint64_t* graph_stat_num_vectors_per_iteration = NULL;
uint64_t* graph_stat_num_edges_per_iteration = NULL;


/* -------- LOCALS --------------------------------------------------------- */

// Number of NUMA nodes for which the graph data structures are optimized.
static uint32_t graph_num_numa_nodes = 0;

// Buffers for reading in the graph from a file, used only during ingress.
static __m256i* graph_edge_list_block_bufs[2] = { NULL, NULL };

// Buffers for storing block counts as a graph is read in from a file, used only during ingress.
static uint64_t* graph_edge_list_block_counts = NULL;

// Number of vectors in the edge list, used only during ingress.
static uint64_t graph_edge_list_vector_count = 0ull;

// Number of blocks in the edge list, used only during ingress.
static uint64_t graph_edge_list_num_blocks = 0ull;

// First shared-encoded vertex in each block of the edge list, used only during ingress.
static uint64_t* graph_edge_list_block_first_shared_vertex = NULL;

// Last shared-encoded vertex in each block of the edge list, used only during ingress.
static uint64_t* graph_edge_list_block_last_shared_vertex = NULL;

// Temporary file handle for reading from files
static FILE* graph_read_file = NULL;

// Temporary buffer for reading from files, plus size and position information
static uint64_t* graph_edges_read_buffer[2] = { NULL, NULL };
static uint64_t graph_edges_read_buffer_max_count = 1024ull*1024ull*1024ull/sizeof(uint64_t);
static uint64_t graph_edges_read_buffer_count[2] = { 0ull, 0ull };

#ifdef EXPERIMENT_MODEL_LONG_VECTORS
// Model for higher vector lengths
uint64_t graph_edges_num_vectors_vl8 = 0ull;
uint64_t graph_edges_num_vectors_vl16 = 0ull;
#endif


/* -------- MACROS --------------------------------------------------------- */

// Combines, gets, and returns the shared (spread-encoding) vertex ID from its piecewise representation, given an edge vector.
#define graph_macro_get_shared_vertex(__m256i_grecord)                                              \
    (                                                                                               \
        ((_mm256_extract_epi64(__m256i_grecord, 0) & 0x7fff000000000000ull) >> 48) |                \
        ((_mm256_extract_epi64(__m256i_grecord, 1) & 0x7fff000000000000ull) >> 33) |                \
        ((_mm256_extract_epi64(__m256i_grecord, 2) & 0x7fff000000000000ull) >> 18) |                \
        ((_mm256_extract_epi64(__m256i_grecord, 3) & 0x0007000000000000ull) >>  3)                  \
    )

// Gets and returns the unused 12-bit field from an edge vector.
#define graph_macro_get_unused_field(__m256i_grecord)                                               \
    (                                                                                               \
        (_mm256_extract_epi64(__m256i_grecord, 3)  & 0x7ff8000000000000ull) >> 51                   \
    )

// Sets the unused 12-bit field in an edge vector and returns the resulting element.
// Parameters specify the original edge vector and the 12-bit value to use (expressed as a uint64_t).
#define graph_macro_set_unused_field(__m256i_grecord, uint64_val12)                                 \
    _mm256_insert_epi64(                                                                            \
        __m256i_grecord,                                                                            \
        (_mm256_extract_epi64(__m256i_grecord, 3)  & 0x8007ffffffffffffull) | ((((uint64_t)uint64_val12) & 0x0000000000000fffull) << 51), \
        3                                                                                           \
    )


/* -------- INTERNAL FUNCTIONS --------------------------------------------- */

// Composes an edge vector, given a shared vertex ID and individual vertex IDs.
// Writes the edge vector to the specified buffer.
// If block storage is enabled and the block is filled, writes it to the block storage device, with a base offset provided.
void graph_helper_write_edge_vector(uint64_t shared_vertex_id, uint64_t* individual_vertex_ids, uint64_t individual_vertex_id_count, uint64_t io_block_offset)
{
    // compose the destination ID by splitting it into pieces
    uint64_t edge_shared_vertex_pieces[4] = {
        (shared_vertex_id & 0x0000000000007fffull) >> 0,    /* bits 14:0  */
        (shared_vertex_id & 0x000000003fff8000ull) >> 15,   /* bits 29:15 */
        (shared_vertex_id & 0x00001fffc0000000ull) >> 30,   /* bits 44:30 */
        (shared_vertex_id & 0x0000e00000000000ull) >> 45    /* bits 47:45 */
    };

    // create and write the in-edge list record
    // upper bit is the "valid" bit, the next 15 bits are parts of the destination vertex ID as pieced out above, and the lower 48 bits are source vertex IDs
    // when gathering, the destination vertex ID will be recovered from this piecewise representation, the "valid" bit is a mask, and the lower 48 bits are used as gather indices
    graph_edge_list_block_bufs[graph_edge_list_num_blocks & 0x0000000000000001ull][graph_edge_list_block_counts[graph_edge_list_num_blocks]] = _mm256_set_epi64x(
        ((individual_vertex_id_count > 3 ? 1ull : 0ull) << 63) | (edge_shared_vertex_pieces[3] << 48) | (individual_vertex_ids[3]),
        ((individual_vertex_id_count > 2 ? 1ull : 0ull) << 63) | (edge_shared_vertex_pieces[2] << 48) | (individual_vertex_ids[2]),
        ((individual_vertex_id_count > 1 ? 1ull : 0ull) << 63) | (edge_shared_vertex_pieces[1] << 48) | (individual_vertex_ids[1]),
        ((individual_vertex_id_count > 0 ? 1ull : 0ull) << 63) | (edge_shared_vertex_pieces[0] << 48) | (individual_vertex_ids[0])
        );

    // update the block index, as appropriate
    if (0 == graph_edge_list_block_counts[graph_edge_list_num_blocks])
    {
        graph_edge_list_block_first_shared_vertex[graph_edge_list_num_blocks] = shared_vertex_id;
    }
    graph_edge_list_block_last_shared_vertex[graph_edge_list_num_blocks] = shared_vertex_id;

    // increment the block and vector counts
    graph_edge_list_block_counts[graph_edge_list_num_blocks] += 1;
    graph_edge_list_vector_count += 1;
}

// Finalizes the edge vector list by committing the last set of records, potentially causing a write to block storage.
void graph_helper_write_final_edge_vector(uint64_t vertex_common_id, uint64_t* vertex_individual_ids, uint64_t vertex_individual_id_count, uint64_t io_block_offset)
{
    graph_helper_write_edge_vector(vertex_common_id, vertex_individual_ids, vertex_individual_id_count, io_block_offset);
    graph_edge_list_num_blocks = 1;
}

// Writes a block of edge in-edge list records, in edge list form, to the specified stream (most likely a file).
void graph_helper_write_edges_to_file(FILE* graphfile, __m256i* records, uint64_t record_count)
{
    for (uint64_t i = 0; i < record_count; ++i)
    {
        uint64_t edge_dest = graph_macro_get_shared_vertex(records[i]);
        uint64_t edge_source = _mm256_extract_epi64(records[i], 0) & 0x0000ffffffffffffull;

        // output the edge only if it is valid, per the "valid" bit
        if (_mm256_extract_epi64(records[i], 0) & 0x8000000000000000ull)
        {
            fprintf(graphfile, "%llu %llu\n", (long long unsigned int)edge_source, (long long unsigned int)edge_dest);
        }

        // same operation, next element
        // unroll this loop because the _mm256_extract_epi64 requires the index to be a constant
        edge_source = _mm256_extract_epi64(records[i], 1) & 0x0000ffffffffffffull;
        if (_mm256_extract_epi64(records[i], 1) & 0x8000000000000000ull)
        {
            fprintf(graphfile, "%llu %llu\n", (long long unsigned int)edge_source, (long long unsigned int)edge_dest);
        }

        edge_source = _mm256_extract_epi64(records[i], 2) & 0x0000ffffffffffffull;
        if (_mm256_extract_epi64(records[i], 2) & 0x8000000000000000ull)
        {
            fprintf(graphfile, "%llu %llu\n", (long long unsigned int)edge_source, (long long unsigned int)edge_dest);
        }

        edge_source = _mm256_extract_epi64(records[i], 3) & 0x0000ffffffffffffull;
        if (_mm256_extract_epi64(records[i], 3) & 0x8000000000000000ull)
        {
            fprintf(graphfile, "%llu %llu\n", (long long unsigned int)edge_source, (long long unsigned int)edge_dest);
        }
    }
}

// Sets up the vertex index for a provided buffer and edge list
void graph_helper_create_vertex_index(__m256i* const edge_list_buf, const uint64_t edge_list_count, uint64_t* const vertex_index_buf, const uint64_t vertex_buf_count, uint64_t* const vertex_index_start, uint64_t* const vertex_index_end)
{
    uint64_t last_vertex_indexed = 0ull;
    uint64_t current_vertex_id = (uint64_t)graph_macro_get_shared_vertex(edge_list_buf[0]);
    
    // for any vertices in the index that appear before the first edge in the list, indicate their invalidity
    for (uint64_t i = 0ull; i < current_vertex_id; ++i)
    {
        vertex_index_buf[i] = 0x7fffffffffffffffull;
    }
    
    // handle the first element in the edge list separately so that the loop logic can work for the rest of them
    vertex_index_buf[current_vertex_id] = 0ull;
    last_vertex_indexed = current_vertex_id;
    
    // record the starting vertex that is valid for this index
    *vertex_index_start = current_vertex_id;
    
    // stream through the rest of the edge list and mark vertices as appropriate
    // if present in the edge list, set their edge list position in the index, otherwise indicate invalidity
    for(uint64_t i = 1ull; i < edge_list_count; ++i)
    {
        current_vertex_id = (uint64_t)graph_macro_get_shared_vertex(edge_list_buf[i]);
        
        if (last_vertex_indexed != current_vertex_id)
        {
            // mark vertices as invalid if they were skipped
            while (last_vertex_indexed < (current_vertex_id - 1))
            {
                last_vertex_indexed += 1ull;
                vertex_index_buf[last_vertex_indexed] = 0x7fffffffffffffffull;
            }
            
            // add the vertex to the index
            vertex_index_buf[current_vertex_id] = i;
            last_vertex_indexed = current_vertex_id;
        }
    }
    
    // record the final vertex that is valid for this index
    *vertex_index_end = current_vertex_id;
    
    // initialize the extra parts of the vertex index to have the top-most bit set
    // this is for vertices that have places in the index but are beyond the highest vertex in the edge list
    for (uint64_t i = current_vertex_id + 1ull; i < vertex_buf_count; ++i)
    {
        vertex_index_buf[i] = 0xffffffffffffffffull;
    }
}

// Allocates the vertex-related arrays
void graph_helper_create_vertex_info(const uint32_t base_numa_node)
{
    // allocate the vertex properties and accumulators
    graph_vertex_props = (double*)numanodes_malloc(sizeof(double) * (graph_num_vertices + 8ull), base_numa_node);
    graph_vertex_accumulators = (double*)numanodes_malloc(sizeof(double) * (graph_num_vertices + 8ull), base_numa_node);
    graph_vertex_outdegrees = (double*)numanodes_malloc(sizeof(double) * (graph_num_vertices + 8ull), base_numa_node);
}

// Initializes vertex properties and accumulators
void graph_helper_initialize_vertex_info()
{
    // initialize vertex properties and accumulators
    for (uint64_t i = 0ull; i < graph_num_vertices; ++i)
    {
        graph_vertex_props[i] = execution_initialize_vertex_prop(i);
        graph_vertex_accumulators[i] = execution_initialize_vertex_accum(i);
    }
}

// Allocates and initializes frontiers of both types
void graph_helper_create_and_initialize_frontiers(const uint32_t* numa_nodes)
{
    // 64 vertices per array position (1 bit per vertex)
    // number of elements = (number of vertices / 64) + (1 if there are any leftovers, 0 otherwise)
    uint64_t frontier_count = (graph_num_vertices >> 6ull) + (graph_num_vertices & 63ull ? 1ull : 0ull);
    
    // allocate the frontiers
    graph_frontier_has_info = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * frontier_count, numa_nodes[0]);
    graph_frontier_wants_info = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * frontier_count, numa_nodes[0]);
    
    // NUMA-ize the frontiers
    for (uint32_t i = 1; i < graph_num_numa_nodes; ++i)
    {
        uint64_t first_frontier_element = graph_vertex_first_numa[i] >> 6ull;
        uint64_t frontier_element_count = graph_vertex_count_numa[i] >> 6ull;
        
        if ((graph_num_numa_nodes - 1ull == i) && !(graph_num_vertices & 63ull))
            frontier_element_count += 1ull;
        
        numanodes_tonode_buffer(&graph_frontier_has_info[first_frontier_element], frontier_element_count << 3ull, numa_nodes[i]);
        numanodes_tonode_buffer(&graph_frontier_wants_info[first_frontier_element], frontier_element_count << 3ull, numa_nodes[i]);
    }
   
    // initialize the frontiers
    for (uint64_t i = 0ull; i < frontier_count; ++i)
    {
        graph_frontier_has_info[i] = execution_initialize_frontier_has_info(i << 6ull);
        graph_frontier_wants_info[i] = execution_initialize_frontier_wants_info(i << 6ull);
    }
}

// Generates the NUMA-aware data structures for the out-edge list, given the standard data structures that have already been filled
void graph_helper_numaize_scatter(const uint32_t* numa_nodes)
{
    // allocate the edge list block buffer pointer containers
    graph_edges_scatter_list_block_bufs_numa = (__m256i***)numanodes_malloc(sizeof(__m256i**) * graph_num_numa_nodes, numa_nodes[0]);
    graph_edges_scatter_list_block_counts_numa = (uint64_t**)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    
    // allocate the vertex index pointers
    graph_vertex_scatter_index_numa = (uint64_t**)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    graph_vertex_scatter_index_start_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    graph_vertex_scatter_index_end_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    
    // for each NUMA node, create its set of buffer pointers and counts
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        graph_edges_scatter_list_block_bufs_numa[i] = (__m256i**)numanodes_malloc(sizeof(__m256i*) * 2, numa_nodes[i]);
        graph_edges_scatter_list_block_counts_numa[i] = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * graph_edges_scatter_list_num_blocks, numa_nodes[i]);
    }
    
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        // assign edges to each NUMA node by dividing the number of edge vectors equally
        uint64_t start_edge_record = graph_edge_list_block_counts[0] * i / graph_num_numa_nodes;
        uint64_t end_edge_record = (graph_edge_list_block_counts[0] * (i + 1) / graph_num_numa_nodes) - 1ull;

        // allocate and initialize each NUMA node's edge list
        if (i > 0)
        {
            graph_edges_scatter_list_block_bufs_numa[i][0] = (__m256i*)numanodes_malloc(sizeof(__m256i) * ((graph_num_edges / graph_num_numa_nodes) + graph_num_numa_nodes), numa_nodes[i]);
            graph_edges_scatter_list_block_bufs_numa[i][1] = graph_edges_scatter_list_block_bufs_numa[i][0];
            
            memcpy(graph_edges_scatter_list_block_bufs_numa[i][0], &graph_edge_list_block_bufs[0][start_edge_record], sizeof(__m256i) * (end_edge_record - start_edge_record + 1ull));
        }
        else
        {
            graph_edges_scatter_list_block_bufs_numa[i][0] = graph_edge_list_block_bufs[0];
            graph_edges_scatter_list_block_bufs_numa[i][1] = graph_edges_scatter_list_block_bufs_numa[i][0];
        }
        
        
        graph_edges_scatter_list_block_counts_numa[i][0] = end_edge_record - start_edge_record + 1ull;
    }
    
    // allocate and initialize per-node vertex indices
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        graph_vertex_scatter_index_numa[i] = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * (graph_num_vertices + (8ull * sizeof(uint64_t))), numa_nodes[i]);
        graph_helper_create_vertex_index(graph_edges_scatter_list_block_bufs_numa[i][0], graph_edges_scatter_list_block_counts_numa[i][0], graph_vertex_scatter_index_numa[i], graph_num_vertices + (8ull * sizeof(uint64_t)), &graph_vertex_scatter_index_start_numa[i], &graph_vertex_scatter_index_end_numa[i]);
    }
}

// Generates the NUMA-aware data structures for the in-edge list, given the standard data structures that have already been filled
void graph_helper_numaize_gather(const uint32_t* numa_nodes)
{
    // allocate the edge list block buffer pointer containers
    graph_edges_gather_list_block_bufs_numa = (__m256i***)numanodes_malloc(sizeof(__m256i**) * graph_num_numa_nodes, numa_nodes[0]);
    graph_edges_gather_list_block_counts_numa = (uint64_t**)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    
    // allocate the vertex index pointers
    graph_vertex_gather_index_numa = (uint64_t**)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    graph_vertex_gather_index_start_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    graph_vertex_gather_index_end_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    
    // for each NUMA node, create its set of buffer pointers and counts
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        graph_edges_gather_list_block_bufs_numa[i] = (__m256i**)numanodes_malloc(sizeof(__m256i*) * 2, numa_nodes[i]);
        graph_edges_gather_list_block_counts_numa[i] = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * graph_edges_gather_list_num_blocks, numa_nodes[i]);
    }
    
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        // assign edges to each NUMA node by dividing the number of edge vectors equally
        uint64_t start_edge_record = graph_edge_list_block_counts[0] * i / graph_num_numa_nodes;
        uint64_t end_edge_record = (graph_edge_list_block_counts[0] * (i + 1) / graph_num_numa_nodes) - 1ull;

        // allocate and initialize each NUMA node's edge list
        graph_edges_gather_list_block_bufs_numa[i][0] = (__m256i*)numanodes_malloc(sizeof(__m256i) * ((graph_num_edges / graph_num_numa_nodes) + graph_num_numa_nodes), numa_nodes[i]);
        graph_edges_gather_list_block_bufs_numa[i][1] = graph_edges_gather_list_block_bufs_numa[i][0];
        memcpy(graph_edges_gather_list_block_bufs_numa[i][0], &graph_edge_list_block_bufs[0][start_edge_record], sizeof(__m256i) * (end_edge_record - start_edge_record + 1ull));
        
        graph_edges_gather_list_block_counts_numa[i][0] = end_edge_record - start_edge_record + 1ull;
    }
    
    // allocate and initialize per-node vertex indices
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        graph_vertex_gather_index_numa[i] = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * (graph_num_vertices + (8ull * sizeof(uint64_t))), numa_nodes[i]);
        graph_helper_create_vertex_index(graph_edges_gather_list_block_bufs_numa[i][0], graph_edges_gather_list_block_counts_numa[i][0], graph_vertex_gather_index_numa[i], graph_num_vertices + (8ull * sizeof(uint64_t)), &graph_vertex_gather_index_start_numa[i], &graph_vertex_gather_index_end_numa[i]);
    }
}

// Generates the NUMA-aware data structures for vertices, given the standard data structures that have already been filled
void graph_helper_numaize_vertices(const uint32_t* numa_nodes)
{
#ifdef EXPERIMENT_ASSIGN_VERTICES_BY_PUSH
    __m256i*** block_bufs_numa = graph_edges_scatter_list_block_bufs_numa;
    uint64_t** block_counts_numa = graph_edges_scatter_list_block_counts_numa;
    const char block_assign_engine[] = "out-edge";
#else
    __m256i*** block_bufs_numa = graph_edges_gather_list_block_bufs_numa;
    uint64_t** block_counts_numa = graph_edges_gather_list_block_counts_numa;
    const char block_assign_engine[] = "in-edge";    
#endif
    
    // create some placeholders for the non-NUMA versions of vertices and accumulators
    double* nonnuma_graph_vertex_props = graph_vertex_props;
    double* nonnuma_graph_vertex_accumulators = graph_vertex_accumulators;
    double* nonnuma_graph_vertex_outdegrees = graph_vertex_outdegrees;
    
    // allocate the vertex assignment records
    graph_vertex_first_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * graph_num_numa_nodes, numa_nodes[0]);
    graph_vertex_last_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * graph_num_numa_nodes, numa_nodes[0]);
    graph_vertex_count_numa = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * graph_num_numa_nodes, numa_nodes[0]);
    
    printf("Vertices:  assigned using %s list\n", block_assign_engine);
    
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        // assign vertices to each NUMA node based on the range of destination vertices in each node's part of the edge list
        // modify the boundaries between nodes to be on an 512-vertex block boundary, as 512 is the combine phase effective vector length when dealing with frontiers
        if (i > 0)
        {
            graph_vertex_first_numa[i] = graph_vertex_last_numa[i - 1] + 1ull;
        }
        else
        {
            graph_vertex_first_numa[i] = 0ull;
        }
        
        if (i < (graph_num_numa_nodes - 1ull))
        {
            graph_vertex_last_numa[i] = graph_macro_get_shared_vertex(block_bufs_numa[i][0][block_counts_numa[i][0] - 1ull]);
            graph_vertex_last_numa[i] += 511ull - (graph_vertex_last_numa[i] & 511ull);
        }
        else
        {
            graph_vertex_last_numa[i] = graph_num_vertices - 1ull;
        }
        
        graph_vertex_count_numa[i] = graph_vertex_last_numa[i] - graph_vertex_first_numa[i] + 1ull;
        
        printf("Vertices:  node %u gets %llu vertices (%llu to %llu, %.2lf%% of total)\n",
            numa_nodes[i],
            (long long unsigned int)graph_vertex_count_numa[i],
            (long long unsigned int)graph_vertex_first_numa[i],
            (long long unsigned int)graph_vertex_last_numa[i],
            (double)graph_vertex_count_numa[i] / (double)graph_num_vertices * 100.0
        );
    }
    
    // allocate the vertex properties and accumulators
    graph_vertex_props = (double*)numanodes_malloc(sizeof(double) * (graph_num_vertices + 8), numa_nodes[0]);
    graph_vertex_accumulators = (double*)numanodes_malloc(sizeof(double) * (graph_num_vertices + 8), numa_nodes[0]);
    graph_vertex_outdegrees = (double*)numanodes_malloc(sizeof(double) * (graph_num_vertices + 8), numa_nodes[0]);
    
    // NUMA-ize the properties and outdegree arrays
    for (uint32_t i = 1; i < graph_num_numa_nodes; ++i)
    {
        numanodes_tonode_buffer(&graph_vertex_props[graph_vertex_first_numa[i]], sizeof(double) * graph_vertex_count_numa[i], numa_nodes[i]);
        numanodes_tonode_buffer(&graph_vertex_outdegrees[graph_vertex_first_numa[i]], sizeof(double) * graph_vertex_count_numa[i], numa_nodes[i]);
    }
    
    // NUMA-ize the accumulators
    for (uint32_t i = 1; i < graph_num_numa_nodes; ++i)
    {
#if defined(BREADTH_FIRST_SEARCH) || defined(CONNECTED_COMPONENTS)
        uint64_t first_accumulator_element = graph_vertex_first_numa[i] >> 6ull;
        uint64_t accumulator_element_count = graph_vertex_count_numa[i] >> 6ull;
        
        if ((graph_num_numa_nodes - 1ull == i) && !(graph_num_vertices & 63ull))
            accumulator_element_count += 1ull;
        
        numanodes_tonode_buffer(&graph_vertex_accumulators[first_accumulator_element], accumulator_element_count << 3ull, numa_nodes[i]);
#else
        numanodes_tonode_buffer(&graph_vertex_accumulators[graph_vertex_first_numa[i]], sizeof(double) * graph_vertex_count_numa[i], numa_nodes[i]);
#endif
    }
    
    // initialize the NUMA-ized properties and accumulator arrays by copying from the non-NUMA versions
    memcpy((void*)graph_vertex_props, (void*)nonnuma_graph_vertex_props, sizeof(double) * (graph_num_vertices + 8));
    memcpy((void*)graph_vertex_accumulators, (void*)nonnuma_graph_vertex_accumulators, sizeof(double) * (graph_num_vertices + 8));
    memcpy((void*)graph_vertex_outdegrees, (void*)nonnuma_graph_vertex_outdegrees, sizeof(double) * (graph_num_vertices + 8));
    
    // free the non-NUMA versions of the properties and accumulator arrays
    numanodes_free((void*)nonnuma_graph_vertex_props, sizeof(double) * (graph_num_vertices + 8));
    numanodes_free((void*)nonnuma_graph_vertex_accumulators, sizeof(double) * (graph_num_vertices + 8));
    numanodes_free((void*)nonnuma_graph_vertex_outdegrees, sizeof(double) * (graph_num_vertices + 8));
}

// Closes an open graph file and frees any temporary data structures.
void graph_helper_close_graph_file()
{
    if (NULL != graph_read_file)
    {
        fclose(graph_read_file);
        graph_read_file = NULL;
    }
    
    for (uint32_t i = 0; i < sizeof(graph_edges_read_buffer)/sizeof(uint64_t*); ++i)
    {
        if (NULL != graph_edges_read_buffer[i])
        {
            free_aligned_mem((void *)graph_edges_read_buffer[i]);
            graph_edges_read_buffer[i] = NULL;
            graph_edges_read_buffer_count[i] = 0ull;
        }
    }
}

// Opens and reads the number of edges and vertices from a file that represents a graph.
// Sets graph_read_file based on the results of this operation.
void graph_helper_open_file_and_extract_graph_info(const char* filename)
{
    // file is encoded as binary
    graph_read_file = fopen(filename, "rb");
    
    if (NULL != graph_read_file)
    {
        uint64_t graph_info[2];
        
        // extract the number of vertices and edges
        if (2 != fread((void *)graph_info, sizeof(uint64_t), 2, graph_read_file))
        {
            graph_helper_close_graph_file();
        }
        
        graph_num_vertices = graph_info[0];
        graph_num_edges = graph_info[1];
    }
    
    // if not already allocated, create the temporary read buffer
    if (NULL != graph_read_file)
    {
        for (uint32_t i = 0; i < sizeof(graph_edges_read_buffer)/sizeof(uint64_t*); ++i)
        {
            if (NULL == graph_edges_read_buffer[i])
            {
                graph_edges_read_buffer[i] = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t) * graph_edges_read_buffer_max_count, 64);
            
                if (NULL == graph_edges_read_buffer[i])
                {
                    graph_helper_close_graph_file();
                }
            }
        }
    }
}

// Fills the temporary read buffer with edges read from an open graph file.
void graph_helper_fill_edge_read_buffer_from_file(uint32_t bufidx)
{
    graph_edges_read_buffer_count[bufidx] = fread((void*)graph_edges_read_buffer[bufidx], sizeof(uint64_t), graph_edges_read_buffer_max_count, graph_read_file);
}

// Retrieves the next edge from a temporary edge reading buffer.
// Places the source and destination vertex information into the specified locations.
// Returns zero on failure (no edges left in buffer), the next position on success.
uint64_t graph_helper_retrieve_next_edge_from_buf(uint64_t* out_edge_source, uint64_t* out_edge_dest, uint64_t posidx, uint32_t bufidx)
{
    // check for remaining edges in the buffer
    if (posidx >= graph_edges_read_buffer_count[bufidx])
    {
        return 0;
    }
    
    *out_edge_source = graph_edges_read_buffer[bufidx][posidx+0ull];
    *out_edge_dest = graph_edges_read_buffer[bufidx][posidx+1ull];
    
    return (posidx + 2ull);
}

// Thread control function for building the edge vector lists.
// Producer: reads from the file into a buffer.
void graph_helper_edge_vector_list_file_buf_producer()
{
    uint32_t bufidx = 0;
    
    while(1)
    {
        graph_helper_fill_edge_read_buffer_from_file(bufidx);
        
        if (graph_edges_read_buffer_count[bufidx] < 2ull)
        {
            break;
        }
        
        bufidx = (bufidx + 1) & 1;
        
        threads_barrier();
    }
    
    threads_barrier();
}

// Thread control function for building the the in-edge list.
// Consumer: reads from buffers into data structures.
void graph_helper_gather_list_file_buf_consumer_edge_list()
{
    uint32_t bufidx = 0;
    uint64_t posidx = 0ull;
    
    // stash for holding information while building in-edge list records
    uint64_t graph_edges_gather_stash_srcids[4] = { 0ull, 0ull, 0ull, 0ull };
    uint64_t graph_edges_gather_stash_dstid = 0ull;
    uint32_t graph_edges_gather_stash_count = 0;
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    // models for higher vector lengths
    uint32_t graph_edges_gather_stash_count_vl8 = 0;
    uint64_t graph_edges_gather_stash_dstid_vl8 = 0ull;
    uint32_t graph_edges_gather_stash_count_vl16 = 0;
    uint64_t graph_edges_gather_stash_dstid_vl16 = 0ull;
#endif

    // information about the current record that has been read from the file
    uint64_t edge_source;
    uint64_t edge_dest;
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    graph_edges_num_vectors_vl8 = 0ull;
    graph_edges_num_vectors_vl16 = 0ull;
#endif
    
    while(1)
    {
        threads_barrier();
        
        if (graph_edges_read_buffer_count[bufidx] < 2ull)
        {
            break;
        }
        
        posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        while (0 != posidx)
        {
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
            // model this for vector length 8
            if ((0 != graph_edges_gather_stash_count_vl8 && graph_edges_gather_stash_dstid_vl8 != edge_dest) || (8 == graph_edges_gather_stash_count_vl8))
            {
                graph_edges_num_vectors_vl8 += 1;
                graph_edges_gather_stash_count_vl8 = 0;
            }
            graph_edges_gather_stash_dstid_vl8 = edge_dest;
            graph_edges_gather_stash_count_vl8 += 1;
            
            // model this for vector length 16
            if ((0 != graph_edges_gather_stash_count_vl16 && graph_edges_gather_stash_dstid_vl16 != edge_dest) || (16 == graph_edges_gather_stash_count_vl16))
            {
                graph_edges_num_vectors_vl16 += 1;
                graph_edges_gather_stash_count_vl16 = 0;
            }
            graph_edges_gather_stash_dstid_vl16 = edge_dest;
            graph_edges_gather_stash_count_vl16 += 1;
#endif
            
            // if stash is not empty and the just-read destination is different, or if the stash is full, flush the stash
            // note that the stash size is 4 to correspond to the number of packed doubles that fit into a 256-bit AVX register
            if ((0 != graph_edges_gather_stash_count && graph_edges_gather_stash_dstid != edge_dest) || (4 == graph_edges_gather_stash_count))
            {
                // compose a record and write out the stash
                graph_helper_write_edge_vector(graph_edges_gather_stash_dstid, graph_edges_gather_stash_srcids, graph_edges_gather_stash_count, 0ull);
                
                // reinitialize the stash to empty
                graph_edges_gather_stash_count = 0;
            }
            
            // add the new vertex into the stash
            graph_edges_gather_stash_dstid = edge_dest;
            graph_edges_gather_stash_srcids[graph_edges_gather_stash_count] = edge_source;
            graph_edges_gather_stash_count += 1;
            
            posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        }
        
        bufidx = (bufidx + 1) & 1;
    }
    
    // perform one final commit of the stash now that the last edge has been added to it
    graph_helper_write_final_edge_vector(graph_edges_gather_stash_dstid, graph_edges_gather_stash_srcids, graph_edges_gather_stash_count, 0ull);
}

// Thread control function for building the the out-edge list.
// Consumer: reads from buffers into data structures.
void graph_helper_scatter_list_file_buf_consumer_edge_list()
{
    uint32_t bufidx = 0;
    uint64_t posidx = 0ull;
    
    // stash for holding information while building out-edge list records
    uint64_t graph_edges_scatter_stash_dstids[4] = { 0ull, 0ull, 0ull, 0ull };
    uint64_t graph_edges_scatter_stash_srcid = 0ull;
    uint32_t graph_edges_scatter_stash_count = 0;
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    // models for higher vector lengths
    uint32_t graph_edges_scatter_stash_count_vl8 = 0;
    uint64_t graph_edges_scatter_stash_srcid_vl8 = 0ull;
    uint32_t graph_edges_scatter_stash_count_vl16 = 0;
    uint64_t graph_edges_scatter_stash_srcid_vl16 = 0ull;
#endif

    // information about the current record that has been read from the file
    uint64_t edge_source;
    uint64_t edge_dest;
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    graph_edges_num_vectors_vl8 = 0ull;
    graph_edges_num_vectors_vl16 = 0ull;
#endif
    
    while(1)
    {
        threads_barrier();
        
        if (graph_edges_read_buffer_count[bufidx] < 2ull)
        {
            break;
        }
        
        posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        while (0 != posidx)
        {
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
            // model this for vector length 8
            if ((0 != graph_edges_scatter_stash_count_vl8 && graph_edges_scatter_stash_srcid_vl8 != edge_source) || (8 == graph_edges_scatter_stash_count_vl8))
            {
                graph_edges_num_vectors_vl8 += 1;
                graph_edges_scatter_stash_count_vl8 = 0;
            }
            graph_edges_scatter_stash_srcid_vl8 = edge_source;
            graph_edges_scatter_stash_count_vl8 += 1;
            
            // model this for vector length 16
            if ((0 != graph_edges_scatter_stash_count_vl16 && graph_edges_scatter_stash_srcid_vl16 != edge_source) || (16 == graph_edges_scatter_stash_count_vl16))
            {
                graph_edges_num_vectors_vl16 += 1;
                graph_edges_scatter_stash_count_vl16 = 0;
            }
            graph_edges_scatter_stash_srcid_vl16 = edge_source;
            graph_edges_scatter_stash_count_vl16 += 1;
#endif
            
            // if stash is not empty and the just-read source is different, or if the stash is full, flush the stash
            // note that the stash size is 4 to correspond to the number of packed doubles that fit into a 256-bit AVX register
            if ((0 != graph_edges_scatter_stash_count && graph_edges_scatter_stash_srcid != edge_source) || (4 == graph_edges_scatter_stash_count))
            {
                // compose a record and write out the stash
                graph_helper_write_edge_vector(graph_edges_scatter_stash_srcid, graph_edges_scatter_stash_dstids, graph_edges_scatter_stash_count, graph_edges_gather_list_num_blocks);
                
                // reinitialize the stash to empty
                graph_edges_scatter_stash_count = 0;
            }
            
            // add the new vertex into the stash
            graph_edges_scatter_stash_srcid = edge_source;
            graph_edges_scatter_stash_dstids[graph_edges_scatter_stash_count] = edge_dest;
            graph_edges_scatter_stash_count += 1;
            
            posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        }
        
        bufidx = (bufidx + 1) & 1;
    }
    
    // perform one final commit of the stash now that the last edge has been added to it
    graph_helper_write_final_edge_vector(graph_edges_scatter_stash_srcid, graph_edges_scatter_stash_dstids, graph_edges_scatter_stash_count, 0ull);
}

// Thread control function for building the the in-edge list.
// Consumer: reads from buffers and initializes graph properties.
void graph_helper_gather_list_file_buf_consumer_property_init()
{
    uint32_t bufidx = 0;
    uint64_t posidx = 0ull;

    // information about the current record that has been read from the file
    uint64_t edge_source;
    uint64_t edge_dest;
    
    while(1)
    {
        threads_barrier();
        
        if (graph_edges_read_buffer_count[bufidx] < 2ull)
        {
            break;
        }
        
        posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        while (0 != posidx)
        {
            // give each source vertex outdegree credit for the edge
            graph_vertex_outdegrees[edge_source] += 1.0;
            
            posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        }
        
        bufidx = (bufidx + 1) & 1;
    }
}

// Thread control function for building the the in-edge list.
// Consumer: reads from buffers and initializes graph properties.
void graph_helper_scatter_list_file_buf_consumer_property_init()
{
    uint32_t bufidx = 0;
    uint64_t posidx = 0ull;

    // information about the current record that has been read from the file
    uint64_t edge_source;
    uint64_t edge_dest;
    
    while(1)
    {
        threads_barrier();
        
        if (graph_edges_read_buffer_count[bufidx] < 2ull)
        {
            break;
        }
        
        posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        while (0 != posidx)
        {
            // no properties to initialize during the out-edge list construction
            
            posidx = graph_helper_retrieve_next_edge_from_buf(&edge_source, &edge_dest, posidx, bufidx);
        }
        
        bufidx = (bufidx + 1) & 1;
    }
}

// Master control function for building the in-edge list.
void graph_helper_multithread_control_build_gather_list(void* arg)
{
    switch (threads_get_local_thread_id())
    {
        case 0:
            graph_helper_edge_vector_list_file_buf_producer();
            break;
        
        case 1:
            graph_helper_gather_list_file_buf_consumer_edge_list();
            break;
        
        case 2:
            graph_helper_gather_list_file_buf_consumer_property_init();
            break;
        
        default:
            break;
    }
}

// Master control function for building the out-edge list.
void graph_helper_multithread_control_build_scatter_list(void* arg)
{
    switch (threads_get_local_thread_id())
    {
        case 0:
            graph_helper_edge_vector_list_file_buf_producer();
            break;
        
        case 1:
            graph_helper_scatter_list_file_buf_consumer_edge_list();
            break;
        
        case 2:
            graph_helper_scatter_list_file_buf_consumer_property_init();
            break;
        
        default:
            break;
    }
}

// Builds the in-edge list, calling the other helper functions as needed.
// Uses three threads: one to read from the file into a buffer, one to read from the buffer into the in-edge list, and one to initialize other graph properties.
void graph_helper_build_gather_list(uint32_t on_numa_node)
{
    uint32_t numa_nodes[] = {on_numa_node};
    
    // spawn three threads to create the in-edge list: one to read from the file, one to set up the in-edge list, and one to initialize graph properties
    threads_spawn(3, 1, numa_nodes, 0, &graph_helper_multithread_control_build_gather_list, NULL);
    
    // copy over common edge-list-building values that are specific to the in-edge list
    graph_edges_gather_list_vector_count = graph_edge_list_vector_count;
    graph_edges_gather_list_num_blocks = graph_edge_list_num_blocks;
    graph_edges_gather_list_block_first_dest_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t) * graph_edges_gather_list_num_blocks, 64);
    graph_edges_gather_list_block_last_dest_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t) * graph_edges_gather_list_num_blocks, 64);
    memcpy((void*)graph_edges_gather_list_block_first_dest_vertex, (void*)graph_edge_list_block_first_shared_vertex, sizeof(uint64_t*) * graph_edges_gather_list_num_blocks);
    memcpy((void*)graph_edges_gather_list_block_last_dest_vertex, (void*)graph_edge_list_block_last_shared_vertex, sizeof(uint64_t*) * graph_edges_gather_list_num_blocks);
    
    // output the total number of blocks in the in-edge list
    printf("In-edges:  created %llu vectors, efficiency = %.1lf%%\n", (long long unsigned int)graph_edges_gather_list_vector_count, (double)graph_num_edges / (double)graph_edges_gather_list_vector_count / 4.0 * 100.0);
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    // output model results for longer vector lengths
    printf("In-edges:  VL8: created %llu vectors, efficiency = %.1lf%%\n", (long long unsigned int)graph_edges_num_vectors_vl8, (double)graph_num_edges / (double)graph_edges_num_vectors_vl8 / 8.0 * 100.0);
    printf("In-edges:  VL16: created %llu vectors, efficiency = %.1lf%%\n", (long long unsigned int)graph_edges_num_vectors_vl16, (double)graph_num_edges / (double)graph_edges_num_vectors_vl16 / 16.0 * 100.0);
#endif
}

// Builds the out-edge list, calling the other helper functions as needed.
// Uses three threads: one to read from the file into a buffer, one to read from the buffer into the in-edge list, and one to initialize other graph properties.
void graph_helper_build_scatter_list(uint32_t on_numa_node)
{
    uint32_t numa_nodes[] = {on_numa_node};
    
    // spawn three threads to create the in-edge list: one to read from the file, one to set up the in-edge list, and one to initialize graph properties
    threads_spawn(3, 1, numa_nodes, 0, &graph_helper_multithread_control_build_scatter_list, NULL);
    
    // copy over common edge-list-building values that are specific to the in-edge list
    graph_edges_scatter_list_vector_count = graph_edge_list_vector_count;
    graph_edges_scatter_list_num_blocks = graph_edge_list_num_blocks;
    graph_edges_scatter_list_block_first_source_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t) * graph_edges_scatter_list_num_blocks, 64);
    graph_edges_scatter_list_block_last_source_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t) * graph_edges_scatter_list_num_blocks, 64);
    memcpy((void*)graph_edges_scatter_list_block_first_source_vertex, (void*)graph_edge_list_block_first_shared_vertex, sizeof(uint64_t*) * graph_edges_scatter_list_num_blocks);
    memcpy((void*)graph_edges_scatter_list_block_last_source_vertex, (void*)graph_edge_list_block_last_shared_vertex, sizeof(uint64_t*) * graph_edges_scatter_list_num_blocks);
    
    // output the total number of blocks in the out-edge list
    printf("Out-edges: created %llu vectors, efficiency = %.1lf%%\n", (long long unsigned int)graph_edges_scatter_list_vector_count, (double)graph_num_edges / (double)graph_edges_scatter_list_vector_count / 4.0 * 100.0);
    
#ifdef EXPERIMENT_MODEL_LONG_VECTORS
    // output model results for longer vector lengths
    printf("Out-edges: VL8: created %llu vectors, efficiency = %.1lf%%\n", (long long unsigned int)graph_edges_num_vectors_vl8, (double)graph_num_edges / (double)graph_edges_num_vectors_vl8 / 8.0 * 100.0);
    printf("Out-edges: VL16: created %llu vectors, efficiency = %.1lf%%\n", (long long unsigned int)graph_edges_num_vectors_vl16, (double)graph_num_edges / (double)graph_edges_num_vectors_vl16 / 16.0 * 100.0);
#endif
}


/* -------- FUNCTIONS ------------------------------------------------------ */
// See "graphdata.h" for documentation.

void graph_data_read_from_file(const char* filename_gather, const char* filename_scatter, const uint32_t num_numa_nodes, const uint32_t* numa_nodes)
{
    graph_num_numa_nodes = num_numa_nodes;

    // open the in-edge list file and extract the number of vertices and edges
    graph_helper_open_file_and_extract_graph_info(filename_gather);
    if (NULL == graph_read_file)
    {
        fprintf(stderr, "Error: unable to read file \"%s\"\n", filename_gather);
        exit(255);
    }
    
    // create vertex-related structures
    graph_helper_create_vertex_info(numa_nodes[0]);
    
    // slight hack to ensure we can allocate enough memory for larger graphs
    if (graph_num_edges > 1000000000ull)
    {
        graph_edge_list_block_bufs[0] = (__m256i*)numanodes_malloc(sizeof(__m256i) * graph_num_edges / 2ull, numa_nodes[0]);
    }
    else
    {
        graph_edge_list_block_bufs[0] = (__m256i*)numanodes_malloc(sizeof(__m256i) * graph_num_edges, numa_nodes[0]);
    }
    
    graph_edge_list_block_bufs[1] = graph_edge_list_block_bufs[0];

    // initialize ingress data structures
    graph_edge_list_vector_count = 0ull;
    graph_edge_list_num_blocks = 0ull;
    graph_edge_list_block_counts = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t), 64);
    graph_edge_list_block_first_shared_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t), 64);
    graph_edge_list_block_last_shared_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t), 64);
    memset((void*)graph_edge_list_block_counts, 0, sizeof(uint64_t));
    memset((void*)graph_edge_list_block_first_shared_vertex, 0, sizeof(uint64_t));
    memset((void*)graph_edge_list_block_last_shared_vertex, 0, sizeof(uint64_t));

    // build the in-edge list and then close the current file
    graph_helper_build_gather_list(numa_nodes[0]);
    graph_helper_close_graph_file();

    // initialize vertex-related data structures
    graph_helper_initialize_vertex_info();
    
    // create NUMA-aware data structures for the in-edge list
    graph_helper_numaize_gather(numa_nodes);
    
    // now that the number of vectors is known, it is not necessary to keep such a large buffer around for ingress
    // add around 10% slack just in case the out-edge list ends up being slightly bigger
    numanodes_free((void*)graph_edge_list_block_bufs[0], sizeof(__m256i) * graph_num_edges / 2ull);
    graph_edge_list_block_bufs[0] = (__m256i*)numanodes_malloc(sizeof(__m256i) * graph_edges_gather_list_vector_count * 11ull / 10ull, numa_nodes[0]);
    graph_edge_list_block_bufs[1] = graph_edge_list_block_bufs[0];
    
#if !defined(EXPERIMENT_EDGE_FORCE_PULL) || defined(EXPERIMENT_ASSIGN_VERTICES_BY_PUSH)
    // open the out-edge list file, it does not matter that this also extracts the number of vertices and edges
    graph_helper_open_file_and_extract_graph_info(filename_scatter);
    if (NULL == graph_read_file)
    {
        fprintf(stderr, "Error: unable to read file \"%s\"\n", filename_scatter);
        exit(255);
    }
    
    // initialize ingress data structures
    graph_edge_list_vector_count = 0ull;
    graph_edge_list_num_blocks = 0ull;
    graph_edge_list_block_counts = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t), 64);
    graph_edge_list_block_first_shared_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t), 64);
    graph_edge_list_block_last_shared_vertex = (uint64_t*)alloc_aligned_mem(sizeof(uint64_t), 64);
    memset((void*)graph_edge_list_block_counts, 0, sizeof(uint64_t));
    memset((void*)graph_edge_list_block_first_shared_vertex, 0, sizeof(uint64_t));
    memset((void*)graph_edge_list_block_last_shared_vertex, 0, sizeof(uint64_t));
    
    // build the out-edge list and then close the current file
    graph_helper_build_scatter_list(numa_nodes[0]);
    graph_helper_close_graph_file();
    
    // create NUMA-aware data structures for the out-edge list
    graph_helper_numaize_scatter(numa_nodes);
#endif

    // create NUMA-aware data structures for vertices
    graph_helper_numaize_vertices(numa_nodes);
    
    // create and initialize the frontiers
    graph_helper_create_and_initialize_frontiers(numa_nodes);
    
    // initialize the dynamic scheduling counters, one per node
    graph_scheduler_dynamic_counter_numa = (uint64_t**)numanodes_malloc(sizeof(uint64_t*) * graph_num_numa_nodes, numa_nodes[0]);
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        graph_scheduler_dynamic_counter_numa[i] = (uint64_t*)numanodes_malloc(sizeof(uint64_t*), numa_nodes[i]);
    }
}

// ---------

void graph_data_allocate_merge_buffers(const uint64_t num_threads, const uint32_t num_numa_nodes, const uint32_t* numa_nodes)
{
    const uint64_t num_blocks_per_node = sched_pull_units_per_node;
    const uint64_t num_blocks = sched_pull_units_total;
    
    graph_vertex_merge_buffer = (mergeaccum_t*)numanodes_malloc(sizeof(mergeaccum_t) * num_blocks, numa_nodes[0]);
    graph_vertex_merge_buffer_baseptr_numa = (mergeaccum_t**)numanodes_malloc(sizeof(mergeaccum_t*) * num_numa_nodes, numa_nodes[0]);
    
    for (uint64_t i = 0; i < num_blocks; ++i)
    {
        graph_vertex_merge_buffer[i].initial_vertex_id = ~0ull;
        graph_vertex_merge_buffer[i].final_vertex_id = ~0ull;
        graph_vertex_merge_buffer[i].final_partial_value = 0.0;
    }
    
    for (uint64_t i = 0; i < num_numa_nodes; ++i)
    {
        graph_vertex_merge_buffer_baseptr_numa[i] = &graph_vertex_merge_buffer[i * num_blocks_per_node];
    }
}

// ---------

void graph_data_allocate_stats(const uint64_t num_threads, const uint32_t numa_node)
{
    graph_stat_num_vectors_per_thread = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * 2ull * num_threads, numa_node);
    graph_stat_num_edges_per_thread = &graph_stat_num_vectors_per_thread[num_threads];
    
    graph_stat_num_vectors_per_iteration = (uint64_t*)numanodes_malloc(sizeof(uint64_t) * 20000ull, numa_node);
    graph_stat_num_edges_per_iteration = &graph_stat_num_vectors_per_iteration[10000];
    
    for (uint64_t i = 0; i < num_threads; ++i)
    {
        graph_stat_num_vectors_per_thread[i] = 0ull;
        graph_stat_num_edges_per_thread[i] = 0ull;
    }
    
    for (uint64_t i = 0; i < 10000ull; ++i)
    {
        graph_stat_num_vectors_per_iteration[i] = 0ull;
        graph_stat_num_edges_per_iteration[i] = 0ull;
    }
}

// ---------

void graph_data_write_to_file(const char* filename)
{
    // open the file for writing
    FILE* graphfile = fopen(filename, "w");
    if (NULL == graphfile) return;

    // write out the number of vertices and edges
    fprintf(graphfile, "%llu\n%llu\n", (long long unsigned int)graph_num_vertices, (long long unsigned int)graph_num_edges);

    // iterate over the in-edge list, as split up across all NUMA nodes, and write out each edge
    for (uint32_t i = 0; i < graph_num_numa_nodes; ++i)
    {
        graph_helper_write_edges_to_file(graphfile, graph_edges_gather_list_block_bufs_numa[i][0], graph_edges_gather_list_block_counts_numa[i][0]);
    }

    fclose(graphfile);
}

// ---------

void graph_data_write_ranks_to_file(char* filename)
{
    // open the file for writing
    FILE* graphfile = fopen(filename, "w");
    if (NULL == graphfile) return;

    // iterate through the rank list and write out each vertex number and rank
    for (uint64_t i = 0; i < graph_num_vertices; ++i)
    {
#if !defined(CONNECTED_COMPONENTS) && !defined(BREADTH_FIRST_SEARCH)
        double vertex_prop = graph_vertex_props[i] * (0.0 == graph_vertex_outdegrees[i] ? (double)graph_num_vertices : graph_vertex_outdegrees[i]);
        fprintf(graphfile, "%llu %.5le\n", (long long unsigned int)i, vertex_prop);
#else
        double vertex_prop = graph_vertex_props[i];
        fprintf(graphfile, "%llu %.0lf\n", (long long unsigned int)i, vertex_prop);
#endif
    }

    fclose(graphfile);
}

// ---------

void graph_data_clear()
{
    // TODO: This needs to be implemented properly for NUMA.
    return;
}
