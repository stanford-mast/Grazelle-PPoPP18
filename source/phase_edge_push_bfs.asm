;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_edge_push_bfs.asm
;      Implementation of the Edge-Push phase for Breadth-First Search.
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
    
    ; place the address of the weak frontier type and the outdegree array into the address stash
    mov                     rax,                    QWORD PTR [graph_vertex_outdegrees]
    mov                     rcx,                    QWORD PTR [graph_frontier_wants_info]
    vextracti128            xmm0,                   ymm_addrstash,          0
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    vpinsrq                 xmm0,                   xmm0,                   rcx,                    1
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   0
    
    ; get the address of the strong frontier
    mov                     r_frontier,             QWORD PTR [graph_frontier_has_info]
    
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
    
    ; verify that the source was visited in the last algorithm implementation
    ; check the strong frontier in case this vertex can be skipped
    phase_helper_strong_frontier_check              r8,                     rsi,                    edge_push_iteration_done,                       done_edge_push_phase
    
    ; check each destination to make sure it wishes to receive information
    ; this is a weak frontier type for the push-based engine
    
    ; obtain the address of the weak frontier type from the address stash
    vpextrq                 rax,                    xmm_addrstash,          1
    
    ; perform a weak frontier check and skip this iteration if all values are 0
IFDEF EXPERIMENT_WITHOUT_VECTORS
    phase_helper_weak_frontier_check_novec          rax,                    ymm0,                   ymm_elist,              ymm_emask,              edge_push_iteration_done
ELSE
    phase_helper_weak_frontier_check                rax,                    ymm0,                   ymm_elist,              ymm_emask,              edge_push_iteration_done
ENDIF
    
    vmovapd                 ymm_emask,              ymm0

    ; obtain the address of the WantsInfo frontier from the address stash
    vpextrq                 r12,                    xmm_addrstash,          1
    
    ; a format conversion is required before writing the curent source ID to the various destinations
    vcvtsi2sd               xmm0,                   xmm0,                   r8
    
    ; for each vector element, check its mask bit and "visit" it accordingly if the bit is set
    ; the mask bit will be set if the edge in the vector is valid and the vertex has not yet been visited, otherwise it will be cleared
  edge_push_update_1:
    ; extract the mask bit
    vpextrq                 r9,                     xmm_emask,              0
    
    ; check that the mask bit is set, indicating both presence of an edge and that the destination is not visited
    bt                      r9,                     63
    jnc                     edge_push_update_2
    
    ; extract the vertex ID of the destination
    vpextrq                 r9,                     xmm_elist,              0
    
    ; visit the destination vertex
    
    ; remove the vertex from WantsInfo
    phase_helper_bitmask_clear                      r12,                    r9
    
    ; add the vertex to HasInfo for next algorithm iteration
    phase_helper_bitmask_set                        r_vaccum,               r9
    
    ; write to the vertex's property the ID of the current source, as its parent
    vmovq                   QWORD PTR [r_vprop+8*r9],                       xmm0
    
    ; increment the global variable that stores the number of vertices changed this algorithm iteration
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r9
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_update_2:
    ; same as above, but using the second position
    vpextrq                 r9,                     xmm_emask,              1
    bt                      r9,                     63
    jnc                     edge_push_update_3
    vpextrq                 r9,                     xmm_elist,              1
    phase_helper_bitmask_clear                      r12,                    r9
    phase_helper_bitmask_set                        r_vaccum,               r9
    vmovq                   QWORD PTR [r_vprop+8*r9],                       xmm0
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r9
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_update_3:
    vextracti128            xmm_emask,              ymm_emask,              1
    vextracti128            xmm_elist,              ymm_elist,              1
    
    ; same as above, but using the third position (first in the newly-extracted 128-bit quantities)
    vpextrq                 r9,                     xmm_emask,              0
    bt                      r9,                     63
    jnc                     edge_push_update_4
    vpextrq                 r9,                     xmm_elist,              0
    phase_helper_bitmask_clear                      r12,                    r9
    phase_helper_bitmask_set                        r_vaccum,               r9
    vmovq                   QWORD PTR [r_vprop+8*r9],                       xmm0
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r9
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
  edge_push_update_4:
    ; same as above, but using the fourth position (second in the newly-extracted 128-bit quantities)
    vpextrq                 r9,                     xmm_emask,              1
    bt                      r9,                     63
    jnc                     edge_push_iteration_done
    vpextrq                 r9,                     xmm_elist,              1
    phase_helper_bitmask_clear                      r12,                    r9
    phase_helper_bitmask_set                        r_vaccum,               r9
    vmovq                   QWORD PTR [r_vprop+8*r9],                       xmm0
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r9
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
    ; iteration complete
  edge_push_iteration_done:
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_edge_push_phase_bfs                 PROC PUBLIC
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
    ja                      edge_push_phase_next_work
    
    ; if the last time around we found an active vertex somewhere in the middle of this unit of work, skip to that vertex
    cmp                     r8,                     rsi
    cmova                   rsi,                    r8
    
    ; verify we still have work to do, and if not, skip this unit of work
    cmp                     rsi,                    rdi
    jge                     edge_push_phase_next_work
    
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
    jae                     edge_push_phase_next_work
    
    mov                     rcx,                    rsi
    edge_push_op_iteration_at_index
    
    inc                     rsi
    jmp                     edge_push_phase_loop
  
  edge_push_phase_next_work:
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
perform_edge_push_phase_bfs                 ENDP


_TEXT                                       ENDS


END
