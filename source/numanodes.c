/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* numanodes.c
*      Implementation of functions that support NUMA awareness.
*****************************************************************************/

#include "numanodes.h"
#include "versioninfo.h"

#include <malloc.h>
#include <stdint.h>
#include <stdio.h>

#ifdef GRAZELLE_WINDOWS
#include <Windows.h>
#else
#include <numa.h>
#endif


/* -------- LOCALS --------------------------------------------------------- */

// Number of NUMA nodes in the system.
static uint32_t numanodes_num_nodes = 0;

// Number of processors in the system.
static uint32_t numanodes_num_processors = 0;

// Array of lists of processors on each NUMA node.
static uint32_t** numanodes_node_processors = NULL;

// Array of counts of processors on each NUMA node.
static uint32_t* numanodes_node_counts = NULL;


/* -------- INTERNAL FUNCTIONS --------------------------------------------- */

// Retrieves the number of NUMA nodes and sets the local variable accordingly.
void numanodes_retrieve_num_nodes()
{
#ifdef GRAZELLE_WINDOWS
    ULONG node_count;
    if (0 == GetNumaHighestNodeNumber(&node_count));
    {
        node_count = 1;
    }
#else
    int node_count = numa_num_configured_nodes();
#endif
    
    numanodes_num_nodes = (uint32_t)node_count;
}

// Retrieves the number of processors and sets the local variable accordingly.
void numanodes_retrieve_num_processors()
{
#ifdef GRAZELLE_WINDOWS
    uint32_t processor_count = 0;
    DWORD_PTR processor_mask = 0;
    DWORD_PTR system_mask = 0;
    
    if (0 == GetProcessAffinityMask(GetCurrentProcess(), &processor_mask, &system_mask))
    {
        processor_count = 1;
    }
    else
    {
        for (uint32_t i = 0; i < (8 * sizeof(DWORD_PTR)); ++i)
        {
            if (processor_mask & (DWORD_PTR)0x0001)
            {
                processor_count += 1;
            }
            
            processor_mask >>= 1;
        }
    }
#else
    uint32_t processor_count = numa_num_configured_cpus();
#endif
    
    numanodes_num_processors = processor_count;
}


/* -------- FUNCTIONS ------------------------------------------------------ */
// See "numanodes.h" for documentation.

void numanodes_initialize()
{
    // retrieve information from the system
    numanodes_retrieve_num_nodes();
    numanodes_retrieve_num_processors();
    
    // fill in the relevant data structures for this subsystem
    numanodes_node_processors = (uint32_t**)malloc(sizeof(uint32_t*) * numanodes_num_nodes);
    numanodes_node_counts = (uint32_t*)malloc(sizeof(uint32_t) * numanodes_num_nodes);
    
    for (uint32_t i = 0; i < numanodes_num_nodes; ++i)
	{
		numanodes_node_processors[i] = (uint32_t*)malloc(sizeof(uint32_t) * numanodes_num_processors);
		numanodes_node_counts[i] = 0;
	}
	
    for (uint32_t i = 0; i < numanodes_num_processors; ++i)
	{
		uint32_t numa_node = numanodes_get_processor_node(i);
		numanodes_node_processors[numa_node][numanodes_node_counts[numa_node]] = i;
		numanodes_node_counts[numa_node] += 1;
	}
}

// ---------

uint32_t numanodes_get_num_nodes()
{
    return numanodes_num_nodes;
}

// ---------

uint32_t numanodes_get_num_processors()
{
    return numanodes_num_processors;
}

// ---------

uint32_t numanodes_get_processor_node(uint32_t processor)
{
#ifdef GRAZELLE_WINDOWS
    UCHAR numa_node;
    if (0 == GetNumaProcessorNode((UCHAR)processor, &numa_node))
    {
        return UINT32_MAX;
    }
#else
    int numa_node = numa_node_of_cpu((int)processor);
    if (numa_node < 0)
    {
        return UINT32_MAX;
    }
#endif
    
    return (uint32_t)numa_node;
}

// ---------

uint32_t numanodes_get_num_processors_on_node(uint32_t node)
{
    if (node >= numanodes_num_nodes)
    {
        return UINT32_MAX;
    }
    
    return numanodes_node_counts[node];
}

// ---------

uint32_t numanodes_get_nth_processor_on_node(uint32_t n, uint32_t node)
{
    if (node >= numanodes_num_nodes || n >= numanodes_node_counts[node])
    {
        return UINT32_MAX;
    }
    
    return numanodes_node_processors[node][n];
}

// ---------

void* numanodes_malloc(size_t size, uint32_t node)
{
    void* mem = 0;
    
#ifdef GRAZELLE_WINDOWS
    mem = (void*)VirtualAllocExNuma(GetCurrentProcess(), NULL, (SIZE_T)(size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE, node);
#else
    mem = numa_alloc_onnode(size, node);
#endif
    
    return mem;
}

// ---------

void numanodes_free(void* mem, size_t size)
{
#ifdef GRAZELLE_WINDOWS
    VirtualFree((LPVOID)mem, (SIZE_T)0, MEM_RELEASE);
#else
    numa_free(mem, size);
#endif
}

// ---------

void numanodes_tonode_buffer(void* mem, size_t size, uint32_t node)
{
#ifdef GRAZELLE_LINUX
    numa_tonode_memory((void *)((size_t)mem & (size_t)0xfffffffffffff000ull), size + (4096 - (size % 4096)), (int)node);
#endif
}
