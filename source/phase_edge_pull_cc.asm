;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_edge_pull_cc.asm
;      Implementation of the Edge-Pull phase for Connected Components.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

INCLUDE constants.inc
INCLUDE phasehelpers.inc
INCLUDE registers.inc
INCLUDE scheduler_pull.inc
INCLUDE threadhelpers.inc


_TEXT                                       SEGMENT


; --------- MACROS ------------------------------------------------------------

; Initializes required state for the Edge-Pull phase.
edge_pull_op_initialize                        MACRO
    ; initialize the work scheduler
    scheduler_phase_init
    
    ; perform common initialization tasks
    phase_helper_set_base_addrs
    phase_helper_set_graph_info
    
    ; place the address of the weak frontier type and the outdegree array into the address stash
    mov                     rax,                    QWORD PTR [graph_vertex_outdegrees]
    mov                     rcx,                    QWORD PTR [graph_frontier_has_info]
    vextracti128            xmm0,                   ymm_addrstash,          0
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    vpinsrq                 xmm0,                   xmm0,                   rcx,                    1
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   0
    
    ; initialize the bitwise-AND masks used throughout this phase
    vmovapd                 ymm_vid_and_mask,       YMMWORD PTR [const_vid_and_mask]
    vmovapd                 ymm_elist_and_mask,     YMMWORD PTR [const_edge_list_and_mask]
    vmovapd                 ymm_emask_and_mask,     YMMWORD PTR [const_edge_mask_and_mask]
ENDM

; Performs an iteration of the Edge-Pull phase at the specified index.
edge_pull_op_iteration_at_index                MACRO
    ; load the edge list element at the specified index
    shl                     rcx,                    5
    add                     rcx,                    r_edgelist
IFDEF EXPERIMENT_WITHOUT_VECTORS
    ; non-vectorized load of 4 scalar elements
    mov                     rax,                    QWORD PTR [rcx]
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    mov                     rax,                    QWORD PTR [rcx+8]
    vpinsrq                 xmm0,                   xmm0,                   rax,                    1
    vinserti128             ymm_edgevec,            ymm_edgevec,            xmm0,                   0
    
    mov                     rax,                    QWORD PTR [rcx+16]
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    mov                     rax,                    QWORD PTR [rcx+24]
    vpinsrq                 xmm0,                   xmm0,                   rax,                    1
    vinserti128             ymm_edgevec,            ymm_edgevec,            xmm0,                   1
ELSE
    ; vectorized load
    vmovapd                 ymm_edgevec,            YMMWORD PTR [rcx]
ENDIF
    
IFNDEF EXPERIMENT_WITHOUT_PREFETCH
    ; prefetch the next several edge list elements whenever the processor has some free time
    ; this will produce a stream of far-ahead prefetches every iteration of the Edge-Pull phase
    ; the "sweet spot" number of cache lines ahead to prefetch was determined experimentally
    prefetchnta             BYTE PTR [rcx+256]
ENDIF

IFDEF EXPERIMENT_ITERATION_STATS
    phase_helper_iteration_stats
ENDIF
    
    ; perform the bitwise AND operations with the required masks
    vandpd                  ymm_elist,              ymm_edgevec,            ymm_elist_and_mask
    vandpd                  ymm_emask,              ymm_edgevec,            ymm_emask_and_mask
    vandpd                  ymm_edgevec,            ymm_edgevec,            ymm_vid_and_mask
    
    ; initialize the gather result register
    ; CC uses +INFINITY
    vmovapd                 ymm_gresult,            ymm_infinity
    
    ; extract the destination vertex ID by extracting individual 16-bit words as needed, shifting, and bitwise-ORing
    vextracti128            xmm1,                   ymm_edgevec,            1
    vpextrw                 r8,                     xmm1,                   7
    shl                     r8,                     45
    vpextrw                 rcx,                    xmm1,                   3
    shl                     rcx,                    30
    or                      r8,                     rcx
    vpextrw                 rdx,                    xmm_edgevec,            7
    shl                     rdx,                    15
    vpextrw                 rcx,                    xmm_edgevec,            3
    or                      rcx,                    rdx
    or                      r8,                     rcx                                         ; r8 has the current destination vertex ID

IFDEF EXPERIMENT_FRONTIERS_WEAK_PULL
    ; check each source to make sure it has updated information
    ; this is a weak frontier type for the pull-based engine
    
    ; obtain the address of the weak frontier type from the address stash
    vpextrq                 rax,                    xmm_addrstash,          1
    
    ; perform a weak frontier check and skip this iteration if all values are 0
IFDEF EXPERIMENT_WITHOUT_VECTORS
    phase_helper_weak_frontier_check_novec          rax,                    ymm0,                   ymm_elist,              ymm_emask,              edge_pull_iteration_done
ELSE
    phase_helper_weak_frontier_check                rax,                    ymm0,                   ymm_elist,              ymm_emask,              edge_pull_iteration_done
ENDIF
    
    vmovapd                 ymm_emask,              ymm0
ENDIF
    
    ; perform the main gather operation
    ; wait until after frontier detection in case it might be skipped
IFDEF EXPERIMENT_WITHOUT_VECTORS
    phase_helper_vgatherqpd_novec                   ymm_gresult,            r_vprop,                ymm_elist,              ymm_emask
ELSE
    vgatherqpd              ymm_gresult,            QWORD PTR [r_vprop+8*ymm_elist],                ymm_emask
ENDIF
    
IFNDEF EXPERIMENT_WITHOUT_PREFETCH
    ; write here either now or later, so issue a prefetch
    prefetchw               QWORD PTR [r_vprop+8*r_prevvid]
ENDIF

IFNDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
IFDEF EXPERIMENT_EDGE_PULL_FORCE_MERGE
    ; if the previous destination vertex ID and current destination vertex ID are the same, do not consider writing to memory this iteration
    cmp                     r_prevvid,              r8
    je                      edge_pull_iteration_skip_write
    
    ; moved onto a new vertex, so reset the accumulator
    ; if the value of the present vertex is not changing, don't do anything else because the vertex is not being updated
    mov                     r10,                    QWORD PTR [r_vprop+8*r_prevvid]
    vmovq                   xmm0,                   r10
    vminpd                  xmm_gaccum,             xmm_gaccum,             xmm0
    vmovq                   rcx,                    xmm_gaccum
    vmovapd                 ymm_gaccum,             ymm_infinity
IFNDEF EXPERIMENT_EDGE_PULL_FORCE_WRITE
    cmp                     r10,                    rcx
    je                      edge_pull_iteration_skip_write
ENDIF
    
  edge_pull_iteration_do_write:
    ; if the value of the present vertex is changing, write back the value to the master vertex property
    mov                     QWORD PTR [r_vprop+8*r_prevvid],                rcx
    
IFDEF EXPERIMENT_FRONTIERS_WEAK_PULL
    ; asynchronous updates to the frontier are only useful if this phase involves HasInfo frontier checks
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    vpextrq                 r9,                     xmm_addrstash,          1
    phase_helper_bitmask_set                        r9,                     r_prevvid
ENDIF
ENDIF
    
    ; add the vertex to HasInfo* for the next algorithm iteration
    phase_helper_bitmask_set                        r_vaccum,               r_prevvid
    
IFDEF EXPERIMENT_EDGE_PULL_FORCE_WRITE
    cmp                     r10,                    QWORD PTR [r_vprop+8*r_prevvid]
    je                      edge_pull_iteration_skip_write
ENDIF
    
    ; increase the count of vertices that changed, for convergence detection and next algorithm iteration's engine selection
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r_prevvid
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0

  edge_pull_iteration_skip_write:
    ; capture the current destination vertex ID for the next iteration
    mov                     r_prevvid,              r8
ELSE
    ; if the previous destination vertex ID and current destination vertex ID are the same, do not consider writing to memory this iteration
    cmp                     r_prevvid,              r8
    je                      edge_pull_iteration_no_vertex_change
    
    ; moved onto a new vertex, so reset the accumulator
    ; if the value of the present vertex is not changing, don't do anything else because the vertex is not being updated
  edge_pull_iteration_write_loop:
    mov                     r10,                    QWORD PTR [r_vprop+8*r_prevvid]
    vmovq                   xmm0,                   r10
    vminpd                  xmm0,                   xmm0,                   xmm_gaccum
    vmovq                   rcx,                    xmm0
IFNDEF EXPERIMENT_EDGE_PULL_FORCE_WRITE
    cmp                     r10,                    rcx
    je                      edge_pull_iteration_skip_write
ENDIF
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SYNC
    mov                     QWORD PTR [r_vprop+8*r_prevvid],                rcx
ELSE
    lock cmpxchg            QWORD PTR [r_vprop+8*r_prevvid],                rcx
    jne                     edge_pull_iteration_write_loop
ENDIF
    
IFDEF EXPERIMENT_FRONTIERS_WEAK_PULL
    ; asynchronous updates to the frontier are only useful if this phase involves HasInfo frontier checks
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    vpextrq                 r9,                     xmm_addrstash,          1
    phase_helper_bitmask_set                        r9,                     r_prevvid
ENDIF
ENDIF
    
    ; add the vertex to HasInfo* for the next algorithm iteration
    phase_helper_bitmask_set                        r_vaccum,               r_prevvid
    
IFDEF EXPERIMENT_EDGE_PULL_FORCE_WRITE
    cmp                     r10,                    QWORD PTR [r_vprop+8*r_prevvid]
    je                      edge_pull_iteration_skip_write
ENDIF
    
    ; increase the count of vertices that changed, for convergence detection and next algorithm iteration's engine selection
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r_prevvid
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0

  edge_pull_iteration_skip_write:
    ; reset the accumulator
    vmovapd                 ymm_gaccum,             ymm_infinity
  
  edge_pull_iteration_no_vertex_change:
    ; capture the current destination vertex ID for the next iteration
    mov                     r_prevvid,              r8
ENDIF
ELSE
    ; the sign bit is used to say "this vertex has been counted for convergence detection"
    ; if the vertex has changed, indicate as much by overwriting the current vertex indicator, which also clears the flag
    mov                     rax,                    r_prevvid
    btr                     rax,                    63
    cmp                     rax,                    r8
    cmovne                  r_prevvid,              r8
ENDIF
    
    ; perform a reduction on the gather results, including the previously-set accumulator, then save back to the accumulator
IFDEF EXPERIMENT_WITHOUT_VECTORS
    vpextrq                 rax,                    xmm_gresult,            0
    vmovq                   xmm0,                   rax
    
    vpextrq                 rax,                    xmm_gresult,            1
    vmovq                   xmm1,                   rax
    vminpd                  xmm0,                   xmm0,                   xmm1
    
    vextractf128            xmm_gresult,            ymm_gresult,            1
    
    vpextrq                 rax,                    xmm_gresult,            0
    vmovq                   xmm1,                   rax
    vminpd                  xmm0,                   xmm0,                   xmm1
    
    vpextrq                 rax,                    xmm_gresult,            1
    vmovq                   xmm1,                   rax
    vminpd                  xmm0,                   xmm0,                   xmm1
    
    vminpd                  xmm_gaccum,             xmm_gaccum,             xmm0
ELSE
    vextractf128            xmm0,                   ymm_gresult,            1
    vminpd                  xmm0,                   xmm_gresult,            xmm0
    vpsrldq                 xmm1,                   xmm0,                   8
    vminpd                  xmm1,                   xmm1,                   xmm0
    vminpd                  xmm_gaccum,             xmm1,                   xmm_gaccum
ENDIF
    
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
  edge_pull_iteration_write_loop:
    ; aggregate the just-computed result with the accumulator
    mov                     r8,                     r_prevvid
    btr                     r8,                     63
    mov                     r10,                    QWORD PTR [r_vprop+8*r8]
    vmovq                   xmm0,                   r10
    vminpd                  xmm0,                   xmm0,                   xmm_gaccum
    vmovq                   rcx,                    xmm0
IFNDEF EXPERIMENT_EDGE_PULL_FORCE_WRITE
    cmp                     r10,                    rcx
    je                      edge_pull_iteration_write_loop_skip
ENDIF
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SYNC
    mov                     QWORD PTR [r_vprop+8*r8],                       rcx
ELSE
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_pull_iteration_write_loop
ENDIF

IFDEF EXPERIMENT_FRONTIERS_WEAK_PULL
    ; asynchronous updates to the frontier are only useful if this phase involves HasInfo frontier checks
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    vpextrq                 r9,                     xmm_addrstash,          1
    phase_helper_bitmask_set                        r9,                     r8
ENDIF
ENDIF
    
    ; add the vertex to HasInfo* for the next algorithm iteration
    phase_helper_bitmask_set                        r_vaccum,               r8
    
IFDEF EXPERIMENT_EDGE_PULL_FORCE_WRITE
    cmp                     r10,                    QWORD PTR [r_vprop+8*r8]
    je                      edge_pull_iteration_write_loop_skip
ENDIF
    
    ; increase the count of vertices that changed, for convergence detection and next algorithm iteration's engine selection
    ; predicate this on the sign bit of r_prevvid to ensure it only happens once per vertex
    btr                     r_prevvid,              63
    jc                      edge_pull_iteration_write_loop_skip
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    bts                     r_prevvid,              63
    
  edge_pull_iteration_write_loop_skip:
    ; reset the per-vertex accumulator
    vmovapd                 ymm_gaccum,             ymm_infinity
ENDIF
    
    ; iteration complete
  edge_pull_iteration_done:
ENDM

; Writes a final Edge-Pull phase result to the correct merge buffer entry for the current thread.
edge_pull_op_write_to_merge_buffer_entry       MACRO
    ; load the current value of the vertex property, in case it was previously changed to something smaller and will therefore not need updating
    mov                     rax,                    QWORD PTR [r_vprop+8*r_prevvid]
    vmovq                   xmm0,                   rax
    vminpd                  xmm_gaccum,             xmm_gaccum,             xmm0
    
    ; if the value of the current vertex is not different, replace the value to be written with +INFINITY
    vmovq                   rcx,                    xmm_gaccum
    cmp                     rcx,                    rax
    cmove                   rcx,                    rax
    mov                     r8,                     rcx
    je                      edge_pull_merge_skip_frontier
    
    ; if the value of the current vertex is different, add the vertex to HasInfo*
    phase_helper_bitmask_set                        r_vaccum,               r_prevvid
    
    ; if the value of the current vertex is different, increase the number of detected vertices that have changed
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r_prevvid
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_pull_merge_skip_frontier:
    ; write the destinaion vertex ID (offset 8) and the partial value (offset 16) to the merge buffer record
    ; record base is in r10 as a parameter
    mov                     QWORD PTR [r10+8],      r_prevvid
    mov                     QWORD PTR [r10+16],     r8
ENDM

; Writes a final Edge-Pull phase result to the vertex properties.
edge_pull_op_write_final                       MACRO
  edge_pull_iteration_final_write_loop:
    ; same write steps as during the main loop body
    ; skip the write if the value is not going to be changing
    mov                     rax,                    QWORD PTR [r_vprop+8*r_prevvid]
    vmovq                   xmm0,                   rax
    vminpd                  xmm0,                   xmm0,                   xmm_gaccum
    vmovq                   rcx,                    xmm0
    cmp                     rax,                    rcx
    je                      edge_pull_iteration_skip_final_write
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SYNC
    mov                     QWORD PTR [r_vprop+8*r_prevvid],                rcx
ELSE
    lock cmpxchg            QWORD PTR [r_vprop+8*r_prevvid],                rcx
    jne                     edge_pull_iteration_final_write_loop
ENDIF
    
IFDEF EXPERIMENT_FRONTIERS_WEAK_PULL
    ; asynchronous updates to the frontier are only useful if this phase involves HasInfo frontier checks
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    vpextrq                 r9,                     xmm_addrstash,          1
    phase_helper_bitmask_set                        r9,                     r_prevvid
ENDIF
ENDIF
    
    ; add the vertex to HasInfo* for the next algorithm iteration
    phase_helper_bitmask_set                        r_vaccum,               r_prevvid
    
    ; increase the count of vertices that changed, for convergence detection and next algorithm iteration's engine selection
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r_prevvid
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
  
  edge_pull_iteration_skip_final_write:
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_edge_pull_phase_cc                  PROC PUBLIC
    ; save non-volatile registers used throughout this phase
    push                    rbx
    push                    rsi
    push                    rdi
    push                    r12
    push                    r13
    push                    r14
    push                    r15
    
    ; set the base address for the current gather list block
    mov                     r_edgelist,             rcx
    
    ; set aside the number of vectors (parameter, rdx)
    vextracti128            xmm0,                   ymm_addrstash,          1
    vpinsrq                 xmm0,                   xmm0,                   rdx,                    1
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   1
    
    ; initialize
    edge_pull_op_initialize
    
    ; get this thread's first work assignment
    scheduler_get_first_assigned_unit
    
  edge_pull_phase_work_start:
    ; check if there is a work assignment for this thread and, if not, the Edge-Pull phase is done
    cmp                     rax,                    0
    jl                      done_edge_pull_phase
    
IFNDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
IFDEF EXPERIMENT_EDGE_PULL_FORCE_MERGE
    ; get the address of the merge buffer for this block of work
    ; first get a pointer to the start of the merge buffers for the current group (NUMA node)
    ; then get a 32-byte offset into the array based on the work unit number obtained from the scheduler
    ; stash away the address for later
    mov                     r10,                    QWORD PTR [graph_vertex_merge_buffer_baseptr_numa]
    threads_helper_get_thread_group_id              ecx
    mov                     r10,                    QWORD PTR [r10+8*rcx]
    mov                     rcx,                    rax
    shl                     rcx,                    5
    add                     r10,                    rcx
    vextracti128            xmm0,                   ymm_addrstash,          1
    vpinsrq                 xmm0,                   xmm0,                   r10,                    0
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   1
ENDIF
ENDIF
    
    ; retrieve the number of vectors into rdx
    vextracti128            xmm0,                   ymm_addrstash,          1
    vpextrq                 rdx,                    xmm0,                   1
    
    ; get the work assignment for this thread
    mov                     rcx,                    rax
    scheduler_assign_work_for_unit
    cmp                     rsi,                    rdi
    jge                     edge_pull_phase_next_work
    
IFNDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
    ; initialize the "previous destination" indicator to the first destination that this thread will see
    ; this avoids accidentally, and incorrectly, triggering a write to accumulator for vertex 0 on the first iteration of the gather loop
    ; it also causes a prefetch of the first element of the gather list, so there is no real added cost
    mov                     rcx,                    rsi
    shl                     rcx,                    5
    add                     rcx,                    r_edgelist
    vmovapd                 ymm_edgevec,            YMMWORD PTR [rcx]
    vandpd                  ymm_edgevec,            ymm_edgevec,            ymm_vid_and_mask
    
    ; as part of the initialization, extract the first destination ID
    vextracti128            xmm1,                   ymm_edgevec,            1
    vpextrw                 r8,                     xmm1,                   7
    shl                     r8,                     45
    vpextrw                 rcx,                    xmm1,                   3
    shl                     rcx,                    30
    or                      r8,                     rcx
    vpextrw                 rdx,                    xmm_edgevec,            7
    shl                     rdx,                    15
    vpextrw                 rcx,                    xmm_edgevec,            3
    or                      rcx,                    rdx
    or                      r8,                     rcx
    
    ; use the value obtained to initialize the "previous destination" indicator
    mov                     r_prevvid,              r8
IFDEF EXPERIMENT_EDGE_PULL_FORCE_MERGE
    mov                     QWORD PTR [r10+0],      r8
ENDIF
ELSE
    xor                     r_prevvid,              r_prevvid
    dec                     r_prevvid
ENDIF
    
    ; initialize the accumulator
    vmovapd                 ymm_gaccum,             ymm_infinity
    
    ; main Edge-Pull phase loop
  edge_pull_phase_loop:
    cmp                     rsi,                    rdi
    jae                     done_edge_pull_loop
    
    mov                     rcx,                    rsi
    edge_pull_op_iteration_at_index
    
    inc                     rsi
    jmp                     edge_pull_phase_loop
  done_edge_pull_loop:
IFNDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
IFDEF EXPERIMENT_EDGE_PULL_FORCE_MERGE
    ; write to the merge buffer, for merging between overlapping accumulators between successive units of work
    vextracti128            xmm0,                   ymm_addrstash,          1
    vpextrq                 r10,                    xmm0,                   0
    edge_pull_op_write_to_merge_buffer_entry
ELSE
    ; perform the final write operation
    edge_pull_op_write_final
ENDIF
ENDIF
    
  edge_pull_phase_next_work:
    ; get this thread's next work assignment
    scheduler_get_next_assigned_unit
    jmp                     edge_pull_phase_work_start
  
  done_edge_pull_phase:  
    ; restore non-volatile registers and return
    pop                     r15
    pop                     r14
    pop                     r13
    pop                     r12
    pop                     rdi
    pop                     rsi
    pop                     rbx
    ret
perform_edge_pull_phase_cc                  ENDP


_TEXT                                       ENDS


END
