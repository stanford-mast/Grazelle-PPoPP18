/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* numanodes.h
*      Declaration of functions that support NUMA awareness.
*****************************************************************************/

#ifndef __GRAZELLE_NUMANODES_H
#define __GRAZELLE_NUMANODES_H


#include "versioninfo.h"

#include <stddef.h>
#include <stdint.h>


/* -------- FUNCTIONS ------------------------------------------------------ */

// Initializes the NUMA awareness subsystem.
void numanodes_initialize();

// Returns the number of NUMA nodes in the system.
uint32_t numanodes_get_num_nodes();

// Returns the number of processors in the system.
uint32_t numanodes_get_num_processors();

// Returns the NUMA node number of a given processor.
// If an invalid processor is specified, the return value is UINT32_MAX.
uint32_t numanodes_get_processor_node(uint32_t processor);

// Returns the number of processors on a given NUMA node.
// If an invalid NUMA node is specified, the return value is UINT32_MAX.
uint32_t numanodes_get_num_processors_on_node(uint32_t node);

// Returns the ID of the nth processor on the specified NUMA node, counting from 0.
// If an invalid processor or node is specified, the return value is UINT32_MAX.
uint32_t numanodes_get_nth_processor_on_node(uint32_t n, uint32_t node);

// Allocates a memory buffer on the specified NUMA node, counting from 0.
// Returns NULL on failure.
void* numanodes_malloc(size_t size, uint32_t node);

// Allocates a memory buffer on the local NUMA node (i.e. whatever node calls this function).
// Returns NULL on failure.
void* numanodes_malloc_local(size_t size);

// Frees memory allocated using this subsystem.
void numanodes_free(void* mem, size_t size);

// Moves the specified memory buffer to the specified NUMA node.
// Typically this would be called on all or part of a buffer already allocated using numanodes_malloc.
// Currently this is a no-op on Windows.
void numanodes_tonode_buffer(void* mem, size_t size, uint32_t node);


#endif //__GRAZELLE_NUMANODES_H
