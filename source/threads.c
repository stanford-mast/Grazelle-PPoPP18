/*****************************************************************************
* Grazelle
*      High performance, hardware-optimized graph processing engine.
*      Targets a single machine with one or more x86-based sockets.
*****************************************************************************
* Authored by Samuel Grossman
* Department of Electrical Engineering, Stanford University
* (c) 2015-2018
*****************************************************************************
* threads.c
*      Implementation of some lightweight multithreading operations. The rest
*      are implemented in assembly.
*****************************************************************************/

#include "functionhelper.h"
#include "numanodes.h"
#include "threads.h"
#include "versioninfo.h"

#include <malloc.h>
#include <stdint.h>


/* -------- PLATFORM-SPECIFIC MACROS --------------------------------------- */

#ifdef GRAZELLE_WINDOWS

#include <process.h>
#include <Windows.h>
#define threadfunc_return_type                  void
#define threads_helper_start_thread(func, arg)  { _beginthread(func, 0, arg); }
#define threads_helper_set_affinity_to(core)    { SetThreadIdealProcessor(GetCurrentThread(), (DWORD)core); }
#define threads_helper_exit_thread()            return

#else

#include <pthread.h>
#include <sched.h>
#define threadfunc_return_type                  void *
#define threads_helper_start_thread(func, arg)  { pthread_t __thread_handle; pthread_create(&__thread_handle, NULL, (void *(*)(void*))func, (void *)arg); }
#define threads_helper_set_affinity_to(core)    { cpu_set_t set; CPU_ZERO(&set); CPU_SET(core, &set); sched_setaffinity(0, sizeof(cpu_set_t ), &set); }
#define threads_helper_exit_thread()            return NULL

#endif


/* -------- TYPE DEFINITIONS ----------------------------------------------- */

// Provides each spawned thread with information about itself and the number of other threads that exist.
typedef struct threadinfo_t
{
    uint32_t thread_id;                         // this thread's global identifier
    uint32_t group_id;                          // this thread's group identifier
    uint32_t group_thread_id;                   // this thread's local identifier within its group
    uint32_t total_threads;                     // total number of threads spawned
    uint32_t total_groups;                      // total number of groups, each containing the same number of threads
    uint32_t threads_per_group;                 // number of threads per group
} threadinfo_t;

// Used by the internal thread starting function to control thread execution.
typedef struct threadstartinfo_t
{
    threadfunc func;
    void* arg;
    threadinfo_t info;
    int32_t affinity;
} threadstartinfo_t;


/* -------- INTERNAL FUNCTIONS --------------------------------------------- */

// This assembly function initializes the assembly side of this thread wrapper.
// It is invoked each time threads are spawned.
void threads_init(uint32_t count) __WRITTEN_IN_ASSEMBLY__;

// This assembly function saves the nonvolatile calling context to the specified buffer.
// The buffer must be 64 bytes in size to hold 8 64-bit registers worth of data.
// Called on the master thread during threads_spawn prior to starting its work.
void threads_save_context_to(uint64_t* buf) __WRITTEN_IN_ASSEMBLY__;

// This assembly function restores the nonvolatile calling context from the specified buffer.
// The buffer must be 64 bytes in size to hold 8 64-bit registers worth of data.
// Called on the master thread during threads_spawn after completing its work.
void threads_restore_context_from(uint64_t* buf) __WRITTEN_IN_ASSEMBLY__;

// This assembly function sets the thread information for the current thread, after spawning.
// Called by the thread start function to set thread information that is commonly accessed.
void threads_submit_common_thread_info(uint32_t local_id, uint32_t global_id, uint32_t group_id, uint32_t threads_per_group) __WRITTEN_IN_ASSEMBLY__;

// This assembly function sets thread information for the current thread, after spawning.
// Called by the thread start function to set less-commonly-used thread information.
void threads_submit_other_thread_info(uint32_t total_threads, uint32_t total_groups) __WRITTEN_IN_ASSEMBLY__;

// Internal thread starting function for worker threads.
threadfunc_return_type threads_start_func(const threadstartinfo_t* startinfo)
{
    if (startinfo->affinity >= 0)
    {
        threads_helper_set_affinity_to(startinfo->affinity);
    }
    
    threads_submit_common_thread_info(startinfo->info.group_thread_id, startinfo->info.thread_id, startinfo->info.group_id, startinfo->info.threads_per_group);
    threads_submit_other_thread_info(startinfo->info.total_threads, startinfo->info.total_groups);
    
    threads_barrier();
    startinfo->func(startinfo->arg);
    threads_barrier();
    
    threads_helper_exit_thread();
}


/* -------- FUNCTIONS ------------------------------------------------------ */
// See "threads.h" for documentation.

uint32_t threads_spawn(const uint32_t count, const uint32_t num_numa_nodes, const uint32_t* numa_nodes, const uint32_t use_alternate_binding, threadfunc func, void* arg)
{
    uint32_t count_per_numa_node = count < num_numa_nodes ? 1 : count / num_numa_nodes;
    uint64_t calling_context_buf[8];
    threadstartinfo_t* startinfo = (threadstartinfo_t*)malloc(sizeof(threadstartinfo_t) * count);
    if (NULL == startinfo)
    {
        return 1;
    }
    
    threads_init(count);
    
    for (uint32_t i = 0; i < count; ++i)
    {
        startinfo[i].arg = arg;
        startinfo[i].info.thread_id = i;
        startinfo[i].info.group_id = i / count_per_numa_node;
        startinfo[i].info.group_thread_id = i % count_per_numa_node;
        startinfo[i].info.total_threads = count;
        startinfo[i].info.total_groups = num_numa_nodes;
        startinfo[i].info.threads_per_group = count / num_numa_nodes;
        startinfo[i].func = func;
        
        if (use_alternate_binding)
        {
            startinfo[i].affinity = numanodes_get_nth_processor_on_node(((i & (uint32_t)0x0001) * (numanodes_get_num_processors_on_node(numa_nodes[i / count_per_numa_node]) / 2)) + ((i % count_per_numa_node) / 2), numa_nodes[i / count_per_numa_node]);
        }
        else
        {
            startinfo[i].affinity = numanodes_get_nth_processor_on_node(i % count_per_numa_node, numa_nodes[i / count_per_numa_node]);
        }
    }
    
    for (uint32_t i = 0; i < (count - 1); ++i)
    {
        threads_helper_start_thread(threads_start_func, (void *)&startinfo[i]);
    }
    
    threads_save_context_to(calling_context_buf);
    threads_start_func(&startinfo[count - 1]);
    threads_restore_context_from(calling_context_buf);
    
    // At this point all threads have exited, so clean up and return success.
    free((void *)startinfo);
    return 0;
}

// ---------

uint32_t threads_spawn_with_separate_masters(const uint32_t count, const uint32_t num_numa_nodes, const uint32_t* numa_nodes, const uint32_t use_alternate_binding, threadfunc func, threadfunc masterfunc, void* arg, void* masterarg)
{
    uint32_t count_per_numa_node = count < num_numa_nodes ? 1 : count / num_numa_nodes;
    uint64_t calling_context_buf[8];
    threadstartinfo_t* startinfo = (threadstartinfo_t*)malloc(sizeof(threadstartinfo_t) * count);
    threadstartinfo_t* masterstartinfo = (threadstartinfo_t*)malloc(sizeof(threadstartinfo_t) * num_numa_nodes);
    if (NULL == startinfo || NULL == masterstartinfo)
    {
        return 1;
    }
    
    threads_init(count + num_numa_nodes);
    
    for (uint32_t i = 0; i < num_numa_nodes; ++i)
    {
        masterstartinfo[i].arg = masterarg;
        masterstartinfo[i].info.thread_id = UINT32_MAX;
        masterstartinfo[i].info.group_id = i;
        masterstartinfo[i].info.group_thread_id = UINT32_MAX;
        masterstartinfo[i].info.total_threads = count;
        masterstartinfo[i].info.total_groups = num_numa_nodes;
        masterstartinfo[i].info.threads_per_group = count / num_numa_nodes;
        masterstartinfo[i].func = masterfunc;
        
        masterstartinfo[i].affinity = numanodes_get_nth_processor_on_node(numanodes_get_num_processors_on_node(numa_nodes[i]) - 1, numa_nodes[i]);
    }
    
    for (uint32_t i = 0; i < count; ++i)
    {
        startinfo[i].arg = arg;
        startinfo[i].info.thread_id = i;
        startinfo[i].info.group_id = i / count_per_numa_node;
        startinfo[i].info.group_thread_id = i % count_per_numa_node;
        startinfo[i].info.total_threads = count;
        startinfo[i].info.total_groups = num_numa_nodes;
        startinfo[i].info.threads_per_group = count / num_numa_nodes;
        startinfo[i].func = func;
        
        if (use_alternate_binding)
        {
            startinfo[i].affinity = numanodes_get_nth_processor_on_node(((i & (uint32_t)0x0001) * (numanodes_get_num_processors_on_node(numa_nodes[i / count_per_numa_node]) / 2)) + ((i % count_per_numa_node) / 2), numa_nodes[i / count_per_numa_node]);
        }
        else
        {
            startinfo[i].affinity = numanodes_get_nth_processor_on_node(i % count_per_numa_node, numa_nodes[i / count_per_numa_node]);
        }
    }
    
    for (uint32_t i = 0; i < num_numa_nodes; ++i)
    {
        threads_helper_start_thread(threads_start_func, (void*)&masterstartinfo[i]);
    }
    
    for (uint32_t i = 0; i < (count - 1); ++i)
    {
        threads_helper_start_thread(threads_start_func, (void *)&startinfo[i]);
    }
    
    threads_save_context_to(calling_context_buf);
    threads_start_func(&startinfo[count - 1]);
    threads_restore_context_from(calling_context_buf);
    
    // At this point all threads have exited, including the master threads, so clean up and return success.
    free((void *)startinfo);
    return 0;
}
