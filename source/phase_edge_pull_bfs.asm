;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_edge_pull_bfs.asm
;      Implementation of the Edge-Pull phase for Breadth-First Search.
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
    
    ; place the address of the weak frontier type and the outdegree array into the address stash
    mov                     rax,                    QWORD PTR [graph_vertex_outdegrees]
    mov                     rcx,                    QWORD PTR [graph_frontier_has_info]
    vextracti128            xmm0,                   ymm_addrstash,          0
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    vpinsrq                 xmm0,                   xmm0,                   rcx,                    1
    vinserti128             ymm_addrstash,          ymm_addrstash,          xmm0,                   0
    
    ; get the address of the strong frontier
    mov                     r_frontier,             QWORD PTR [graph_frontier_wants_info]
    
    ; initialize the bitwise-AND masks used throughout this phase
    vmovapd                 ymm_vid_and_mask,       YMMWORD PTR [const_vid_and_mask]
    vmovapd                 ymm_elist_and_mask,     YMMWORD PTR [const_edge_list_and_mask]
    vmovapd                 ymm_emask_and_mask,     YMMWORD PTR [const_edge_mask_and_mask]
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

IFDEF EXPERIMENT_ITERATION_STATS
    phase_helper_iteration_stats
ENDIF

    ; perform the bitwise AND operations with the required masks
    vandpd                  ymm_elist,              ymm_edgevec,            ymm_elist_and_mask
    vandpd                  ymm_emask,              ymm_edgevec,            ymm_emask_and_mask
    vandpd                  ymm_edgevec,            ymm_edgevec,            ymm_vid_and_mask
    
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

    ; verify that the target has not yet been visited
    ; check the strong frontier in case this vertex can be skipped
    phase_helper_strong_frontier_check              r8,                     rsi,                    edge_pull_iteration_done,                       done_edge_pull_phase
    
IFNDEF EXPERIMENT_WITHOUT_PREFETCH
    ; prefetch, with intention to write, the vertex property for the unvisited current destination
    prefetchw               QWORD PTR [r_vprop+8*r8]
ENDIF
    
    ; verify that at least one source has been visited
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
    
IFDEF EXPERIMENT_WITHOUT_VECTORS
    ; pick a parent valid for a BFS traversal by looking at each source vertex in order
    ; once a parent is found, this process is complete
    ; note that, because of the weak frontier check above, one of the parents is guaranteed to be valid
    
    ; first element
    ; extract the potential parent to xmm1
    vextracti128            xmm0,                   ymm_elist,              0
    vpextrq                 rax,                    xmm0,                   0
    vmovq                   xmm1,                   rax
    
    ; extract the mask bit and verify that it is set, if so this process is complete
    vextracti128            xmm0,                   ymm_emask,              0
    vpextrq                 rax,                    xmm0,                   0
    bt                      rax,                    63
    jc                      bfs_found_parent
    
    ; second element
    ; same as above, but adjusted for the second position
    vextracti128            xmm0,                   ymm_elist,              0
    vpextrq                 rax,                    xmm0,                   1
    vmovq                   xmm1,                   rax
    vextracti128            xmm0,                   ymm_emask,              0
    vpextrq                 rax,                    xmm0,                   1
    bt                      rax,                    63
    jc                      bfs_found_parent
    
    ; third element
    ; same as above, but adjusted for the third position
    vextracti128            xmm0,                   ymm_elist,              1
    vpextrq                 rax,                    xmm0,                   0
    vmovq                   xmm1,                   rax
    vextracti128            xmm0,                   ymm_emask,              1
    vpextrq                 rax,                    xmm0,                   0
    bt                      rax,                    63
    jc                      bfs_found_parent
    
    ; fourth element
    ; just extract the potential parent and move on
    ; if we get here this element is guaranteed to be the one to choose
    vextracti128            xmm0,                   ymm_elist,              1
    vpextrq                 rax,                    xmm0,                   1
    vmovq                   xmm1,                   rax
    
  bfs_found_parent:
ELSE
    ; the mask specifies if any of the sources have been visited, and at this point it is guaranteed to have at least one
    ; since there is not a right-shift-arithmetic-packed instruction, produce the mask by comparing against zero
    vxorpd                  ymm0,                   ymm0,                   ymm0
    vpcmpgtq                ymm_emask,              ymm0,                   ymm_emask
    
    ; sources not present will be zero, otherwise they will be an actual value
    ; note that 0 is actually a valid source ID, but because of the weak frontier check above we do not need to worry about distinguishing valid and invalid values of 0
    vandpd                  ymm_elist,              ymm_emask,              ymm_elist
    
    ; pick a parent valid for a BFS traversal by computing the max of all values present in ymm_elist
    ; max is arbitrary (as opposed to min) for selecting which parent
    vextractf128            xmm0,                   ymm_elist,              1
    vmaxpd                  xmm0,                   xmm0,                   xmm_elist
    vpsrldq                 xmm1,                   xmm0,                   8
    vmaxpd                  xmm1,                   xmm1,                   xmm0
ENDIF
    
    ; selected parent is in the lower position of xmm1, so write it to the vertex property for the current vertex
    ; need to do some format conversion for correctness in writing to the vertex properties
    vmovq                   rax,                    xmm1
    vcvtsi2sd               xmm1,                   xmm1,                   rax
    vmovq                   QWORD PTR [r_vprop+8*r8],                       xmm1
    
    ; set the bit in HasInfo* for the current vertex
    phase_helper_bitmask_set                        r_vaccum,               r8
    
    ; clear the bit from WantsInfo for the current vertex
    phase_helper_bitmask_clear                      r_frontier,             r8
    
    ; increment the global variable that stores the number of vertices changed this algorithm iteration
    vpextrq                 rax,                    xmm_globaccum,          0
    phase_helper_add_frontier_stat                  rax,                    r8
    vpinsrq                 xmm_globaccum,          xmm_globaccum,          rax,                    0
    
    ; iteration complete
  edge_pull_iteration_done:
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_edge_pull_phase_bfs                 PROC PUBLIC
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
    edge_pull_op_initialize
    
    ; get this thread's first work assignment
    scheduler_get_first_assigned_unit
    xor                     rsi,                    rsi
    
  edge_pull_phase_work_start:
    ; check if there is a work assignment for this thread and, if not, the Edge-Pull phase is done
    cmp                     rax,                    0
    jl                      done_edge_pull_phase
    
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
    ja                      edge_pull_phase_next_work
    
    ; if the last time around we found an active vertex somewhere in the middle of this unit of work, skip to that vertex
    cmp                     r8,                     rsi
    cmova                   rsi,                    r8
    
    ; verify we still have work to do, and if not, skip this unit of work
    cmp                     rsi,                    rdi
    jge                     edge_pull_phase_next_work
    
    ; load the vertex index
    threads_helper_get_thread_group_id              eax
    mov                     r_vindex,               QWORD PTR [graph_vertex_gather_index_numa]
    mov                     r_vindex,               QWORD PTR [r_vindex+8*rax]
    
    ; compute the number of elements valid for this NUMA node's frontier searches
    ; valid frontier bits are numbered from 0 to the last shared vertex in this node's edge list
    ; of course, this node will only start searching based on its edge list assignment
    ; each frontier element holds 64 vertices, so divide by 64 (shift right by 6) to obtain the proper element count
    ; increment once more to compensate for any remainders left over
    mov                     r_frontiercount,        QWORD PTR [graph_vertex_gather_index_end_numa]
    mov                     r_frontiercount,        QWORD PTR [r_frontiercount+8*rax]
    inc                     r_frontiercount
    shr                     r_frontiercount,        6
    inc                     r_frontiercount
    
    ; main Edge-Pull phase loop
  edge_pull_phase_loop:
    cmp                     rsi,                    rdi
    jae                     edge_pull_phase_next_work
    
    mov                     rcx,                    rsi
    edge_pull_op_iteration_at_index
    
    inc                     rsi
    jmp                     edge_pull_phase_loop
    
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
perform_edge_pull_phase_bfs                 ENDP


_TEXT                                       ENDS


END
