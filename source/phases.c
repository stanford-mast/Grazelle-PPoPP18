/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* phases.c
*      Implementation of operations used throughout both phases of execution.
*      Note that most of the implementation is in assembly.
*****************************************************************************/

#include "execution.h"
#include "floathelper.h"
#include "graphtypes.h"
#include "graphdata.h"
#include "threads.h"

#include <stdint.h>


/* -------- FUNCTIONS ------------------------------------------------------ */
// See "phases.h" for documentation.

uint64_t phase_op_combine_global_var_from_buf(uint64_t* reduce_buffer)
{
    uint64_t value = 0ull;
    
    for (uint32_t i = 0; i < threads_get_total_threads(); ++i)
    {
        value += reduce_buffer[i];
    }
    
    return value;
}

// --------

void edge_pull_op_merge_with_merge_buffer(mergeaccum_t* merge_buffer, uint64_t count, double* vertex_accumulators)
{
    uint64_t i = 0ull, j = 0ull;
    double proposed_value = 0.0;
    
    while (i < count && !(merge_buffer[i].initial_vertex_id & 0x8000000000000000ull))
    {
        // proposed value is initially the value of the current record we are trying to merge
        proposed_value = merge_buffer[i].final_partial_value;
        
        // if other cores were collaborating on the same vertex, merge their partial values
        // do this by checking if their final vertex ID is the same
        // when this loop terminates, proposed_value will have the merged result of those other threads along with the starting thread, and j >= (i + 1)
        for (j = (i + 1ull); j < count && merge_buffer[j].final_vertex_id == merge_buffer[i].final_vertex_id; ++j)
        {
            proposed_value = SCALAR_REDUCE_OP(proposed_value, merge_buffer[j].final_partial_value);
        }
        
        // final check: if a thread working on a different final vertex shows up, handle overlap between its first vertex
        // in this case, we'll merge with whatever is already in the accumulators, instead of just writing, otherwise we are ready to write
        if (j < count && merge_buffer[j].initial_vertex_id == merge_buffer[i].final_vertex_id)
        {
            proposed_value = SCALAR_REDUCE_OP(proposed_value, vertex_accumulators[merge_buffer[j].initial_vertex_id]);
        }
        
        // final write to the accumulator and advance to the next merge target
        vertex_accumulators[merge_buffer[i].final_vertex_id] = proposed_value;
        i = j;
    }
}
