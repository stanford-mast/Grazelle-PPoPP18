/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* threads.h
*      Simple threading wrapper for lightweight and consistent implementation
*      of multi-threading.
*****************************************************************************/

#ifndef __GRAZELLE_THREADS_H
#define __GRAZELLE_THREADS_H


#include "functionhelper.h"

#include <stdint.h>


/* -------- TYPE DEFINITIONS ----------------------------------------------- */

// Signature of the starting function of each thread. Must return nothing and accept a single parameter.
typedef void (* threadfunc)(void* arg);


/* -------- FUNCTIONS ------------------------------------------------------ */

// Parallelizes the execution of (func) using (count) threads. Spawns (count - 1) additional worker threads.
// Spreads core bindings of the threads across the specified number and list of NUMA nodes.
// Each thread is considered equal, although the thread with ID 0 executes in the calling thread.
// Each thread receives, as an argument, a pointer to a threadinfo_t structure to give it information about itself and other threads.
// Returns 0 once all threads have exited, or nonzero in case of an error spawning the specified number of threads.
uint32_t threads_spawn(const uint32_t count, const uint32_t num_numa_nodes, const uint32_t* numa_nodes, const uint32_t use_alternate_binding, threadfunc func, void* arg);

// Parallelizes the execution of (func) using (count) threads. Spawns (count) worker threads for this purpose.
// Also creates (num_numa_nodes) master threads and binds one to each NUMA node using the last core reported to exist on that node.
// Spreads core bindings of the threads across the specified number and list of NUMA nodes.
// While the workers are executing (func), the master threads execute (masterfunc). The master threads still participates in all barriers.
// Each thread receives, as an argument, a pointer to a threadinfo_t structure to give it information about itself and other threads.
// When threads are spawned this way, workers are numbered 0 to (count - 1) and are told that there are (count) threads in total.
// The masters are given local and global IDs of (~0) but otherwise receive accurate information, so for example total threads = # worker threads (which is also the case for workers).
// Returns 0 once all threads have exited, or nonzero in case of an error spawning the specified number of threads.
uint32_t threads_spawn_with_separate_masters(const uint32_t count, const uint32_t num_numa_nodes, const uint32_t* numa_nodes, const uint32_t use_alternate_binding, threadfunc func, threadfunc masterfunc, void* arg, void* masterarg);

// Retrieves the current thread's local ID within its group.
const uint32_t threads_get_local_thread_id() __WRITTEN_IN_ASSEMBLY__;

// Retrieves the current thread's global ID.
const uint32_t threads_get_global_thread_id() __WRITTEN_IN_ASSEMBLY__;

// Retrieves the current thread's logical group number.
const uint32_t threads_get_thread_group_id() __WRITTEN_IN_ASSEMBLY__;

// Retrieves the number of threads per logical group.
const uint32_t threads_get_threads_per_group() __WRITTEN_IN_ASSEMBLY__;

// Retrieves the total number of threads globally.
const uint32_t threads_get_total_threads() __WRITTEN_IN_ASSEMBLY__;

// Retrieves the total number of thread groups.
const uint32_t threads_get_total_groups() __WRITTEN_IN_ASSEMBLY__;

// Sets the per-thread 64-bit variable. This variable can be used for any purpose.
void threads_set_per_thread_variable(uint64_t value) __WRITTEN_IN_ASSEMBLY__;

// Retrieves the per-thread 64-bit variable. This variable can be used for any purpose.
uint64_t threads_get_per_thread_variable() __WRITTEN_IN_ASSEMBLY__;

// Provides a barrier that no thread can pass until all threads have reached this point in the execution.
void threads_barrier() __WRITTEN_IN_ASSEMBLY__;

// Same as above, using an alternative symbol name to separate load balancing barriers from merge barriers.
// Used for profiling experiments.
void threads_merge_barrier() __WRITTEN_IN_ASSEMBLY__;

// Provides a barrier that no thread can pass until all threads have reached this point in the execution.
// Returns the amount of wait time (via the `rdtsc' instruction) for the calling thread, useful for measuring time spent waiting at the barrier.
uint64_t threads_timed_barrier() __WRITTEN_IN_ASSEMBLY__;


#endif //__GRAZELLE_THREADS_H
