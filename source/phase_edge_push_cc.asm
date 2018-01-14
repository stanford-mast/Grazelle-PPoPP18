;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_edge_push_cc.asm
;      Implementation of the Edge-Push phase for Connected Components.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

INCLUDE constants.inc
INCLUDE phasehelpers.inc
INCLUDE registers.inc
INCLUDE scheduler_push.inc
INCLUDE threadhelpers.inc


_TEXT                                       SEGMENT


; --------- MACROS ------------------------------------------------------------

; Initializes required state for the Edge-Push phase.
edge_push_op_initialize                     MACRO
    ; initialize the work scheduler
    scheduler_phase_init
    
    ; perform common initialization tasks
    phase_helper_set_base_addrs
    phase_helper_set_graph_info
    
    ; get the address of the "has_info" (strong) frontier
    mov                     r_frontier,             QWORD PTR [graph_frontier_has_info]
    
    ; place the address of the outdegree array into the address stash
    mov                     rax,                    QWORD PTR [graph_vertex_outdegrees]
    vextracti128            xmm0,                   ymm_addrstash,          0
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   0
    
    ; initialize the bitwise-AND masks used throughout this phase
    vmovapd                 ymm_vid_and_mask,       YMMWORD PTR [const_vid_and_mask]
    vmovapd                 ymm_elist_and_mask,     YMMWORD PTR [const_edge_list_and_mask]
    vmovapd                 ymm_emask_and_mask,     YMMWORD PTR [const_edge_mask_and_mask]
ENDM

; Performs an iteration of the Edge-Push phase at the specified index.
edge_push_op_iteration_at_index             MACRO
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
    ; this will produce a stream of far-ahead prefetches every iteration of the Edge-Push phase
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
    
    ; extract the source vertex ID by extracting individual 16-bit words as needed, shifting, and bitwise-ORing
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
    or                      r8,                     rcx                                         ; r8 has the current source vertex ID
    
IFNDEF EXPERIMENT_FRONTIERS_NOSTRONG_PUSH
    ; check the strong frontier in case this iteration can be skipped
    phase_helper_strong_frontier_check              r8,                     rsi,                    edge_push_iteration_done,                       done_edge_push_phase
ENDIF
    
    ; prepare the message the source vertex will send to its neighbors
    ; from above, r8 currently holds the source vertex ID
    ; this is a scalar but floating-point quantity
    vmovq                   xmm_smsgout,            QWORD PTR [r_vprop+8*r8]
    
    ; there are currently no scatter instructions capable of performing updates
    ; it is also possible that there are multiple edges in the present vector going to the same place
    ; so it would not be correct to perform a vector gather, vector update, and vector write-back
    ; in the absence of instructions that can aggregate updates and perform them atomically, this needs to be done in scalar form
    
  edge_push_iteration_update_1_start:
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    ; if using HTM and only doing a single transaction, begin that transaction here
IFDEF EXPERIMENT_EDGE_PUSH_HTM_ATOMIC_FALLBACK
    xbegin                  edge_push_fallback_iteration_update_1_start
ELSE
    xbegin                  edge_push_iteration_update_1_start
ENDIF
ENDIF
ENDIF
ENDIF
    ; extract the destination vertex identifier and mask bit for the first vertex
    ; verify that the top bit is set and, if not, skip the vertex entirely
    vpextrq                 r8,                     xmm_elist,              0
    vpextrq                 r9,                     xmm_emask,              0
    bt                      r9,                     63
    jnc                     edge_push_iteration_update_2_start
    
  edge_push_iteration_update_1_loop:
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    ; begin an update transaction
    xbegin                  edge_push_iteration_update_1_loop
ENDIF
ENDIF
ENDIF
    
    ; read the destination vertex's current value
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    
    ; set aside the current value for "cmpxchg" below, which implicitly uses rax
    vmovq                   rax,                    xmm0
    
    ; aggregate with the outgoing message using the "min" operation for CC
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    
    ; if the message does not change the value of the vertex, skip to the next one
    ; the result of this comparison is all '1's in the lower position of xmm1 if the value has changed
    ; then the bit test will result in CF being set, so if it is not set then skip over the write
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jc                      edge_push_iteration_update_1_write
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    ; terminate the transaction early, since this update is being skipped
    xend
ENDIF
ENDIF
ENDIF
    jmp                     edge_push_iteration_update_2_start
    
  edge_push_iteration_update_1_write:
IFDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
    ; write back the aggregated property
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
    ; write back the aggregated property
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
    ; atomically update the aggregated property
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_iteration_update_1_loop
ENDIF
ENDIF
    
IFDEF EXPERIMENT_PUSH_WITHOUT_SYNC
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    
    ; add the vertex to HasInfo*
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM    
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    
    ; add the vertex to HasInfo*
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
    
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    ; commit the transaction
    xend
ENDIF
ELSE
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    ; immediately add the vertex to HasInfo (an asynchronous frontier update)
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF
    
    ; add the vertex to HasInfo*
    phase_helper_bitmask_set                        r_vaccum,               r8
ENDIF
ENDIF
    
    ; increase the count of vertices that changed, for convergence detection and next algorithm iteration's engine selection
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_iteration_update_2_start:
    ; same as above
    vpextrq                 r8,                     xmm_elist,              1
    vpextrq                 r9,                     xmm_emask,              1
    bt                      r9,                     63
    jnc                     edge_push_iteration_update_3_start
    
  edge_push_iteration_update_2_loop:
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xbegin                  edge_push_iteration_update_2_loop
ENDIF
ENDIF
ENDIF
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jc                      edge_push_iteration_update_2_write
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xend
ENDIF
ENDIF
ENDIF
    jmp                     edge_push_iteration_update_3_start
    
  edge_push_iteration_update_2_write:
IFDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_iteration_update_2_loop
ENDIF
ENDIF
    
IFDEF EXPERIMENT_PUSH_WITHOUT_SYNC
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM    
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xend
ENDIF
ELSE
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF
    phase_helper_bitmask_set                        r_vaccum,               r8
ENDIF
ENDIF
    
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_iteration_update_3_start:
    ; same as above, except move to the upper 128 bits of the edge list and mask registers
    vextracti128            xmm_elist,              ymm_elist,              1
    vextracti128            xmm_emask,              ymm_emask,              1
    vpextrq                 r8,                     xmm_elist,              0
    vpextrq                 r9,                     xmm_emask,              0
    bt                      r9,                     63
    jnc                     edge_push_iteration_update_4_start
    
  edge_push_iteration_update_3_loop:
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xbegin                  edge_push_iteration_update_3_loop
ENDIF
ENDIF
ENDIF
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jc                      edge_push_iteration_update_3_write
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xend
ENDIF
ENDIF
ENDIF
    jmp                     edge_push_iteration_update_4_start
    
  edge_push_iteration_update_3_write:
IFDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_iteration_update_3_loop
ENDIF
ENDIF
    
IFDEF EXPERIMENT_PUSH_WITHOUT_SYNC
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM    
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xend
ENDIF
ELSE
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF
    phase_helper_bitmask_set                        r_vaccum,               r8
ENDIF
ENDIF
    
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_iteration_update_4_start:
    ; same as above
    vpextrq                 r8,                     xmm_elist,              1
    vpextrq                 r9,                     xmm_emask,              1
    bt                      r9 ,                    63
    jnc                     edge_push_iteration_update_commit
    
  edge_push_iteration_update_4_loop:
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xbegin                  edge_push_iteration_update_4_loop
ENDIF
ENDIF
ENDIF
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jc                      edge_push_iteration_update_4_write
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xend
ENDIF
ENDIF
ENDIF
    jmp                     edge_push_iteration_update_commit
    
  edge_push_iteration_update_4_write:
IFDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
ELSE
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_iteration_update_4_loop
ENDIF
ENDIF
    
IFDEF EXPERIMENT_PUSH_WITHOUT_SYNC
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
ELSE
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM    
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set_nosync                 r_frontier,             r8
ENDIF
    phase_helper_bitmask_set_nosync                 r_vaccum,               r8
IFNDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    xend
ENDIF
ELSE
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF
    phase_helper_bitmask_set                        r_vaccum,               r8
ENDIF
ENDIF
    
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_iteration_update_commit:
IFNDEF EXPERIMENT_EDGE_PUSH_WITHOUT_SYNC
IFDEF EXPERIMENT_EDGE_PUSH_WITH_HTM
IFDEF EXPERIMENT_EDGE_PUSH_HTM_SINGLE
    ; if using HTM and only doing a single transaction, commit it here
    xend
ENDIF

IFDEF EXPERIMENT_EDGE_PUSH_HTM_ATOMIC_FALLBACK
    ; if using HTM and with atomic fallback, jump over that code
    jmp edge_push_fallback_iteration_done
    
    ; this is just the atomic code from above, condensed for space, and placed here to enable an atomic fallback path
  edge_push_fallback_iteration_update_1_start:
    vpextrq                 r8,                     xmm_elist,              0
    vpextrq                 r9,                     xmm_emask,              0
    bt                      r9,                     63
    jnc                     edge_push_fallback_iteration_update_2_start
    
  edge_push_fallback_iteration_update_1_loop:
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jnc                     edge_push_fallback_iteration_update_2_start
    
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_fallback_iteration_update_1_loop
    
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF
    
    phase_helper_bitmask_set                        r_vaccum,               r8
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_fallback_iteration_update_2_start:
    vpextrq                 r8,                     xmm_elist,              1
    vpextrq                 r9,                     xmm_emask,              1
    bt                      r9,                     63
    jnc                     edge_push_fallback_iteration_update_3_start
    
  edge_push_fallback_iteration_update_2_loop:
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jnc                     edge_push_fallback_iteration_update_3_start
    
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_fallback_iteration_update_2_loop

IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF

    phase_helper_bitmask_set                        r_vaccum,               r8
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_fallback_iteration_update_3_start:
    vextracti128            xmm_elist,              ymm_elist,              1
    vextracti128            xmm_emask,              ymm_emask,              1
    vpextrq                 r8,                     xmm_elist,              0
    vpextrq                 r9,                     xmm_emask,              0
    bt                      r9,                     63
    jnc                     edge_push_fallback_iteration_update_4_start
    
  edge_push_fallback_iteration_update_3_loop:
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jnc                     edge_push_fallback_iteration_update_4_start
    
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_fallback_iteration_update_3_loop

IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF

    phase_helper_bitmask_set                        r_vaccum,               r8
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_fallback_iteration_update_4_start:
    vpextrq                 r8,                     xmm_elist,              1
    vpextrq                 r9,                     xmm_emask,              1
    bt                      r9 ,                    63
    jnc                     edge_push_fallback_iteration_done
    
  edge_push_fallback_iteration_update_4_loop:
    vmovq                   xmm0,                   QWORD PTR [r_vprop+8*r8]
    vmovq                   rax,                    xmm0
    vminpd                  xmm1,                   xmm0,                   xmm_smsgout
    vcmpneqpd               xmm0,                   xmm0,                   xmm1
    vmovq                   rdx,                    xmm0
    bt                      rdx,                    63
    jnc                     edge_push_fallback_iteration_done
    
    vmovq                   rcx,                    xmm1
    lock cmpxchg            QWORD PTR [r_vprop+8*r8],                       rcx
    jne                     edge_push_fallback_iteration_update_4_loop
    
IFNDEF EXPERIMENT_FRONTIERS_WITHOUT_ASYNC
    phase_helper_bitmask_set                        r_frontier,             r8
ENDIF

    phase_helper_bitmask_set                        r_vaccum,               r8
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_fallback_iteration_done:
ENDIF
ENDIF
ENDIF
    
    ; iteration complete
  edge_push_iteration_done:
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_edge_push_phase_cc                  PROC PUBLIC
    ; save non-volatile registers used throughout this phase
    push                    rbx
    push                    rsi
    push                    rdi
    push                    r12
    push                    r13
    push                    r14
    push                    r15
    
    ; set the base address for the current edge list block
    mov                     r_edgelist,             rcx
    
    ; set aside the number of vectors (parameter, rdx)
    vextracti128            xmm0,                   ymm_addrstash,          1
    vpinsrq                 xmm0,                   xmm0,                   rdx,                    1
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   1
    
    ; initialize
    edge_push_op_initialize
    
    ; get this thread's first work assignment
    scheduler_get_first_assigned_unit
    xor                     rsi,                    rsi
    
  edge_push_phase_work_start:
    ; check if there is a work assignment for this thread and, if not, the Edge-Push phase is done
    cmp                     rax,                    0
    jl                      done_edge_push_phase
    
    ; retrieve the number of vectors into rdx
    vextracti128            xmm0,                   ymm_addrstash,          1
    vpextrq                 rdx,                    xmm0,                   1
    
    ; get the work assignment for this thread
    mov                     rcx,                    rax
    mov                     r8,                     rsi
    scheduler_assign_work_for_unit
    
    ; if the last time around we found that no vertices were active until after this unit of work, skip this unit of work
    cmp                     r8,                     rdi
    cmova                   rsi,                    r8
    ja                      done_edge_push_loop
    
    ; if the last time around we found an active vertex somewhere in the middle of this unit of work, skip to that vertex
    cmp                     r8,                     rsi
    cmova                   rsi,                    r8
    
    ; verify we still have work to do, and if not, skip this unit of work
    cmp                     rsi,                    rdi
    jge                     done_edge_push_loop
    
    ; load the vertex index
    threads_helper_get_thread_group_id              eax
    mov                     r_vindex,               QWORD PTR [graph_vertex_scatter_index_numa]
    mov                     r_vindex,               QWORD PTR [r_vindex+8*rax]
    
    ; compute the number of elements valid for this NUMA node's frontier searches
    ; valid frontier bits are numbered from 0 to the last shared vertex in this node's edge list
    ; of course, this node will only start searching based on its edge list assignment
    ; each frontier element holds 64 vertices, so divide by 64 (shift right by 6) to obtain the proper element count
    ; increment once more to compensate for any remainders left over
    mov                     r_frontiercount,        QWORD PTR [graph_vertex_scatter_index_end_numa]
    mov                     r_frontiercount,        QWORD PTR [r_frontiercount+8*rax]
    inc                     r_frontiercount
    shr                     r_frontiercount,        6
    inc                     r_frontiercount
    
    ; main Edge-Push phase loop
  edge_push_phase_loop:
    cmp                     rsi,                    rdi
    jae                     done_edge_push_loop
    
    mov                     rcx,                    rsi
    edge_push_op_iteration_at_index
    
    inc                     rsi
    jmp                     edge_push_phase_loop
  done_edge_push_loop:
    
    ; get this thread's next work assignment
    scheduler_get_next_assigned_unit
    jmp                     edge_push_phase_work_start
  
  done_edge_push_phase:  
    ; restore non-volatile registers and return
    pop                     r15
    pop                     r14
    pop                     r13
    pop                     r12
    pop                     rdi
    pop                     rsi
    pop                     rbx
    ret
perform_edge_push_phase_cc                  ENDP


_TEXT                                       ENDS


END
