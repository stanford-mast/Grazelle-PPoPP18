;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_edge_pull_pr.asm
;      Implementation of the Edge-Pull phase for PageRank.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

INCLUDE constants.inc
INCLUDE phasehelpers.inc
INCLUDE registers.inc
INCLUDE scheduler_pull.inc
INCLUDE threadhelpers.inc


_TEXT                                       SEGMENT


; --------- MACROS ------------------------------------------------------------

; Initializes required state for the Edge-Pull phase.
edge_pull_op_initialize                     MACRO
    ; initialize the work scheduler
    scheduler_phase_init
    
    ; perform common initialization tasks
    phase_helper_set_base_addrs
    phase_helper_set_graph_info
    
    ; initialize the bitwise-AND masks used throughout this phase
    vmovapd                 ymm_vid_and_mask,       YMMWORD PTR [const_vid_and_mask]
    vmovapd                 ymm_elist_and_mask,     YMMWORD PTR [const_edge_list_and_mask]
    vmovapd                 ymm_emask_and_mask,     YMMWORD PTR [const_edge_mask_and_mask]
    
    ; initialize accumulator values with initial values (PageRank uses 0)
    vxorpd                  ymm_gaccum,             ymm_gaccum,             ymm_gaccum
ENDM

; Performs an iteration of the Edge-Pull phase at the specified index.
edge_pull_op_iteration_at_index             MACRO
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
    
    ; perform the bitwise AND operations with the required masks
    vandpd                  ymm_elist,              ymm_edgevec,            ymm_elist_and_mask
    vandpd                  ymm_emask,              ymm_edgevec,            ymm_emask_and_mask
    vandpd                  ymm_edgevec,            ymm_edgevec,            ymm_vid_and_mask
    
    ; initialize the gather result register
    ; PageRank uses 0
    vxorpd                  ymm_gresult,            ymm_gresult,            ymm_gresult
    
    ; perform the main gather operation
    ; it takes a while, so issue it as soon as possible
IFDEF EXPERIMENT_WITHOUT_VECTORS
    phase_helper_vgatherqpd_novec                   ymm_gresult,      r_vprop,                ymm_elist,              ymm_emask
ELSE
    vgatherqpd              ymm_gresult,            QWORD PTR [r_vprop+8*ymm_elist],                ymm_emask
ENDIF
    
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
    
IFNDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
IFNDEF EXPERIMENT_WITHOUT_PREFETCH
    ; write here either now or later, so issue a prefetch
    prefetchw               QWORD PTR [r_vaccum+8*r_prevvid]
ENDIF

    ; conditionally write the accumulator to memory, if the previous destination vertex ID and the current destination vertex ID are different
    ; also add to the total and reinitialize the vertex accumulator
    cmp                     r_prevvid,              r8
    je                      edge_pull_iteration_skip_write
  edge_pull_iteration_do_write:
    vmovq                   rcx,                    xmm_gaccum
    mov                     QWORD PTR [r_vaccum+8*r_prevvid],               rcx
    vaddpd                  xmm_globaccum,          xmm_globaccum,          xmm_gaccum
    vxorpd                  ymm_gaccum,             ymm_gaccum,             ymm_gaccum

  edge_pull_iteration_skip_write:
    ; capture the current destination vertex ID for the next iteration
    mov                     r_prevvid,              r8
ENDIF
    
    ; perform a reduction on the gather results, including the previously-set accumulator, then save back to the accumulator
IFDEF EXPERIMENT_WITHOUT_VECTORS
    vpextrq                 rax,                    xmm_gresult,            0
    vmovq                   xmm0,                   rax
    
    vpextrq                 rax,                    xmm_gresult,            1
    vmovq                   xmm1,                   rax
    vaddpd                  xmm0,                   xmm0,                   xmm1
    
    vextractf128            xmm_gresult,            ymm_gresult,            1
    
    vpextrq                 rax,                    xmm_gresult,            0
    vmovq                   xmm1,                   rax
    vaddpd                  xmm0,                   xmm0,                   xmm1
    
    vpextrq                 rax,                    xmm_gresult,            1
    vmovq                   xmm1,                   rax
    vaddpd                  xmm0,                   xmm0,                   xmm1
    
    vaddpd                  xmm_gaccum,             xmm0,                   xmm_gaccum
ELSE
    vhaddpd                 ymm1,                   ymm_gresult,            ymm_gresult
    vextractf128            xmm0,                   ymm1,                   1
    vaddpd                  xmm0,                   xmm0,                   xmm1
    vaddpd                  xmm_gaccum,             xmm0,                   xmm_gaccum
ENDIF
    
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
  edge_pull_iteration_write_loop:
    ; aggregate the just-computed result with the accumulator
    mov                     rax,                    QWORD PTR [r_vaccum+8*r8]
    vmovq                   xmm0,                   rax
    vaddpd                  xmm0,                   xmm0,                   xmm_gaccum
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SYNC
    vmovq                   QWORD PTR [r_vaccum+8*r8],                      xmm0
ELSE
    vmovq                   rcx,                    xmm0
    lock cmpxchg            QWORD PTR [r_vaccum+8*r8],                      rcx
    jne                     edge_pull_iteration_write_loop
ENDIF
    ; add the ranks to the total and reset the per-vertex accumulator
    vaddpd                  xmm_globaccum,          xmm_globaccum,          xmm_gaccum
    vxorpd                  ymm_gaccum,             ymm_gaccum,             ymm_gaccum
ENDIF
    ; iteration complete
  edge_pull_iteration_done:
ENDM

; Writes a final Edge-Pull phase result to the correct merge buffer entry for the current thread.
edge_pull_op_write_to_merge_buffer_entry    MACRO
    ; obtain the value to write and add to the total
    vmovq                   rax,                    xmm_gaccum
    vaddpd                  xmm_globaccum,          xmm_globaccum,          xmm_gaccum
    
    ; write the destinaion vertex ID (offset 8) and the partial value (offset 16) to the merge buffer record
    ; record base is in rcx as a parameter
    mov                     QWORD PTR [r10+8],      r_prevvid
    mov                     QWORD PTR [r10+16],     rax
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_edge_pull_phase_pr                  PROC PUBLIC
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
    ; get the address of the merge buffer for this block of work
    ; first get a pointer to the start of the merge buffers for the current group (NUMA node)
    ; then get a 32-byte offset into the array based on the work unit number obtained from the scheduler
    ; stash away the address for later, using the stack
    mov                     r10,                    QWORD PTR [graph_vertex_merge_buffer_baseptr_numa]
    threads_helper_get_thread_group_id              ecx
    mov                     r10,                    QWORD PTR [r10+8*rcx]
    mov                     rcx,                    rax
    shl                     rcx,                    5
    add                     r10,                    rcx
    vextracti128            xmm0,                   ymm_addrstash,          0
    vpinsrq                 xmm0,                   xmm0,                   r10,                    0
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   0
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
    
    ; use the value obtained to initialize the "previous destination" indicator and write into the merge buffer (at offset 0)
    mov                     r_prevvid,              r8
    mov                     QWORD PTR [r10+0],      r8
ENDIF
    
    ; initialize the accumulator
    vxorpd                  ymm_gaccum,             ymm_gaccum,             ymm_gaccum
    
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
    ; write to the merge buffer, for merging between overlapping accumulators between successive units of work
    vmovq                   r10,                    xmm_addrstash
    edge_pull_op_write_to_merge_buffer_entry
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
perform_edge_pull_phase_pr                  ENDP


_TEXT                                       ENDS


END
