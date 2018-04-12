;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phasehelpers.inc
;      Implementation of helper macros for both Edge and Vertex phases.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

IFNDEF __GRAZELLE_PHASEHELPERS_INC
__GRAZELLE_PHASEHELPERS_INC EQU 1


INCLUDE constants.inc
INCLUDE graphdata.inc
INCLUDE registers.inc
INCLUDE threadhelpers.inc


; --------- MACROS ------------------------------------------------------------

; Initializes the reserved registers with constant pointers that specify the base addresses of gather elements and intermediate accumulators.
phase_helper_set_base_addrs                 MACRO
    mov                     r_vprop,                QWORD PTR [graph_vertex_props]
    mov                     r_vaccum,               QWORD PTR [graph_vertex_accumulators]
ENDM

; Initializes the reserved registers with information about the graph and the algorithm.
; In particular, initializes registers with algorithm-specific constants.
phase_helper_set_graph_info                 MACRO
    ; store the number of vertices as AVX packed doubles
    mov                     rax,                    QWORD PTR [graph_num_vertices]
    vcvtsi2sd               xmm0,                   xmm0,                   rax
    vbroadcastsd            ymm_numvertices,        xmm0

IFDEF CONNECTED_COMPONENTS
    vmovapd                 ymm_one,                YMMWORD PTR [const_one]
    vmovapd                 ymm_infinity,           YMMWORD PTR [const_infinity]
ELSE
IFDEF BREADTH_FIRST_SEARCH
    vmovapd                 ymm_one,                YMMWORD PTR [const_one]
    vmovapd                 ymm_infinity,           YMMWORD PTR [const_infinity]
ELSE
    ; store the damping factor and calculate (1 - d) / V
    vmovapd                 ymm_1_minus_d_by_V,     YMMWORD PTR [const_one]
    vmovapd                 ymm_damping,            YMMWORD PTR [const_damping_factor]
    
    vsubpd                  ymm_1_minus_d_by_V,     ymm_1_minus_d_by_V,     ymm_damping
    vdivpd                  ymm_1_minus_d_by_V,     ymm_1_minus_d_by_V,     ymm_numvertices
ENDIF
ENDIF
ENDM

; Given a pointer to a bit-mask, sets the specified bit to '1'.
; Uses rax, rcx, and rdx as scratch registers.
phase_helper_bitmask_set                    MACRO r_bitmask, r_bitidx
    mov                     rax,                    r_bitidx
    mov                     rcx,                    r_bitidx
    
    ; rax will hold the index of the 8-bit block (obtained by dividing by 8)
    ; rcx will hold the bit position within its 8-bit block (obtained by taking modulus with 8)
    ; dl will hold the mask for bitwise-OR
    shr                     rax,                    3
    and                     rcx,                    7
    mov                     edx,                    1
    shl                     edx,                    cl
    
    ; set the bit in the bit-mask
    lock or                 BYTE PTR [r_bitmask+rax],                       dl
ENDM

; Given a pointer to a bit-mask, sets the specified bit to '1'.
; This version omits all synchronization.
; Uses rax, rcx, and rdx as scratch registers.
phase_helper_bitmask_set_nosync             MACRO r_bitmask, r_bitidx
    mov                     rax,                    r_bitidx
    mov                     rcx,                    r_bitidx
    
    ; rax will hold the index of the 8-bit block (obtained by dividing by 8)
    ; rcx will hold the bit position within its 8-bit block (obtained by taking modulus with 8)
    ; dl will hold the mask for bitwise-OR
    shr                     rax,                    3
    and                     rcx,                    7
    mov                     edx,                    1
    shl                     edx,                    cl
    
    ; set the bit in the bit-mask
    or                      BYTE PTR [r_bitmask+rax],                       dl
ENDM

; Given a pointer to a bit-mask, sets the specified bit to '0'.
; Uses rax, rcx, and rdx as scratch registers.
phase_helper_bitmask_clear                  MACRO r_bitmask, r_bitidx
    mov                     rax,                    r_bitidx
    mov                     rcx,                    r_bitidx
    
    ; rax will hold the index of the 8-bit block (obtained by dividing by 8)
    ; rcx will hold the bit position within its 8-bit block (obtained by taking modulus with 8)
    ; dl will hold the mask for bitwise-OR
    shr                     rax,                    3
    and                     rcx,                    7
    mov                     edx,                    1
    shl                     edx,                    cl
    not                     dl
    
    ; clear the bit in the bit-mask
    lock and                BYTE PTR [r_bitmask+rax],                       dl
ENDM

; Given a pointer to a bit-mask, sets the specified bit to '0'.
; This version omits all synchronization.
; Uses rax, rcx, and rdx as scratch registers.
phase_helper_bitmask_clear_nosync           MACRO r_bitmask, r_bitidx
    mov                     rax,                    r_bitidx
    mov                     rcx,                    r_bitidx
    
    ; rax will hold the index of the 8-bit block (obtained by dividing by 8)
    ; rcx will hold the bit position within its 8-bit block (obtained by taking modulus with 8)
    ; dl will hold the mask for bitwise-OR
    shr                     rax,                    3
    and                     rcx,                    7
    mov                     edx,                    1
    shl                     edx,                    cl
    not                     dl
    
    ; clear the bit in the bit-mask
    and                     BYTE PTR [r_bitmask+rax],                       dl
ENDM

; Given a pointer to the strong frontier (in r_frontier, with count in r_frontiercount) and a vertex ID, implements a strong frontier check.
; Jumps to the specified label if the frontier is not set, with the next edge vector to process being placed into the specified destination.
; Uses as scratch and overwrites rax, rcx, rdx, and r9.
phase_helper_strong_frontier_check          MACRO r_vid, r_nextvec, lbl_skipiter, lbl_skipphase
    
  strong_frontier_check:
    
    ; load the frontier element that corresponds to the vertex to check if it in the frontier or not
    ; to figure this out, need to divide the source vertex ID by 64 (to get the 64-bit frontier array element) and then mod by 64 (to figure out the bit within the frontier array element)
    ; use rax for the index of the element within the frontier array element, rdx for the frontier array element itself, and rcx for the bit position (0 to 63)
    ; the CF flag gets set to the value of the bit in the frontier: if set, then the vertex has info, otherwise the vertex has converged and must be skipped
    mov                     rax,                    r_vid
    shr                     rax,                    6
    mov                     rcx,                    r_vid
    and                     rcx,                    003fh
    mov                     rdx,                    QWORD PTR [r_frontier+8*rax]
    bt                      rdx,                    rcx
    jc                      vertex_is_active
    
  vertex_search_start:
    ; vertex is not active, so use the frontier to find the next one that is active
    ; the goal is to search through the frontier bitmask to find the next '1' bit that is set, starting at the current vertex's bit position
    ; this will be done in a loop, but first it must be set up
    ; first step is to transform the current frontier element to invalidate any vertices that come before the one currently being processed (so that TZCNT will not pick them up)
    ; this is reasonably simple: use rcx (bit position of the current vertex) to create a mask for bitwise-and with rdx (current frontier array element)
    ; start with a value of 0x2 as an implementation detail: rcx = 0 should map to 0x1, rcx = 1 should map to 0x3, etc. (of course, need to invert for bitwise-and, since the goal is to place 0s there not 1s)
    ; edge case: rcx = 63, then r9 will be all 1s set, so TZCNT will find nothing and the result will be moving on to the next frontier position (this is what should happen)
    mov                     r9,                     0002h
    shl                     r9,                     cl
    dec                     r9
    not                     r9
    and                     rdx,                    r9
    
    ; second step is to perform the check on the current frontier bitmask
    ; count the number of trailing zeroes, using again rcx as the bit position (and thus the destination)
    ; if the frontier is equal to zero (i.e. no vertices are active in this block), then CF gets set to '1' and the loop is initiated
    ; if there are ones in the bitmask (CF set to '0') then a candidate was identified, so skip the loop and check the candidate vertex
    tzcnt                   rcx,                    rdx
    jnc                     vertex_search_candidate_found
    
    ; iterate until a candidate vertex is found in the frontier array or the end of the frontier array is reached (in which case this phase is over)
  vertex_search_loop:
    inc                     rax
    cmp                     rax,                    r_frontiercount
    jge                     lbl_skipphase
    
    ; similar to above, load the element of the frontier array and see if any vertices want info
    ; in this case, jump back if no candidate found (i.e. all zeroes, so CF is set to '1')
    mov                     rdx,                    QWORD PTR [r_frontier+8*rax]
    tzcnt                   rcx,                    rdx
    jc                      vertex_search_loop
    
  vertex_search_candidate_found:
    ; a candidate vertex has been identified at bit position (rcx) within the block at index (rax)
    ; the actual vertex ID is equal to (rax * 64) + (rcx), place this into r9
    mov                     r9,                     rax
    shl                     r9,                     6
    add                     r9,                     rcx
    
    ; load the vertex's index value
    mov                     r9,                     QWORD PTR [r_vindex+8*r9]
    
    ; if the top bit of the index value is set, the vertex is past the end of the number of vertices in the graph
    ; abort the search and cause this phase to terminate
    bt                      r9,                     63
    jc                      lbl_skipphase
    
    ; if the second-to-top bit of the index value is set, the vertex has no associated edges so keep searching
    ; need to ignore the vertex that was found, so jump back and create the AND mask
    bt                      r9,                     62
    jc                      vertex_search_start
    
    ; vertex found, load its offset into specified destination and then subtract 1
    ; the outer loop that invokes this macro will increment by 1 and then check scheduling range
    mov                     r_nextvec,              r9
    dec                     r_nextvec
    jmp                     lbl_skipiter
    
  vertex_is_active:
ENDM

; Given a pointer to the weak frontier, a set of weak vertex IDs, and a mask, implements a weak frontier check.
; Places the frontier bits into the upper-most bit positions of the destination.
; If all results are zero, jumps to the specified label to cause the iteration to be skipped.
; Uses rax, ymm1, and ymm_edgevec as scratch registers.
phase_helper_weak_frontier_check            MACRO r_wfrontier, ymm_dest, ymm_vid, ymm_mask, lbl_skipiter
  
  weak_frontier_check:
  
    ; offsets in the weak frontier are just the four vertices in the edge elements divided by 64 (shifted right by 6)
    ; once this is calculated, gather from the weak frontier data structure
    ; also need to ensure any positions not enabled in the edge vector mask are set to 0
    ; when this is done, ymm_dest will hold the correct frontier bit-masks for all of the vertices enabled in the edge vector mask
    vpsrlq                  ymm_dest,               ymm_vid,                6
    vmovapd                 ymm1,                   ymm_mask
    vmovapd                 ymm_edgevec,            ymm_dest
    vgatherqpd              ymm_dest,               QWORD PTR [r_wfrontier+8*ymm_edgevec],          ymm1
    vxorpd                  ymm1,                   ymm1,                   ymm1
    vblendvpd               ymm_dest,               ymm1,                   ymm_dest,               ymm_mask
    
    ; correct bit position for each vertex of interest is equal to ymm_elist % 64, on a per-element basis, or equivalently ymm_elist & 63
    ; when this is done, ymm1 will hold the bit positions of each bit within the 64 bits loaded into ymm_dest that correspond to each vertex of interest
    mov                     rax,                    63
    vmovq                   xmm1,                   rax
    vbroadcastsd            ymm1,                   xmm1
    vandpd                  ymm1,                   ymm1,                   ymm_elist
    
    ; shift all positions in ymm_dest to the right by the specified number of bits in ymm1
    ; then shift them all left by 63 to place the correct weak frontier bits in the upper-most bit position
    vpsrlvq                 ymm_dest,               ymm_dest,               ymm1
    vpsllq                  ymm_dest,               ymm_dest,               63
    
    ; test if all the mask bits are 0 and, if they are, jump to the specified label, otherwise continue executing
    vtestpd                 ymm_dest,               ymm_dest
    jz                      lbl_skipiter
    
  done_weak_frontier_check:
ENDM

; Given a pointer to the weak frontier, a set of weak vertex IDs, and a mask, implements a weak frontier check.
; Places the frontier bits into the upper-most bit positions of the destination.
; If all results are zero, jumps to the specified label to cause the iteration to be skipped.
; Uses rcx, rdx, r9, and xmm1 as scratch registers.
; This is the non-vectorized version.
phase_helper_weak_frontier_check_novec      MACRO r_wfrontier, ymm_dest, ymm_vid, ymm_mask, lbl_skipiter
  
  weak_frontier_check_novec:
    
    ; initialize the destination register
    vxorpd                  ymm_dest,               ymm_dest,               ymm_dest
    
    ; initialize a register to hold a count of the number of weak frontier bits that are set to '1'
    xor                     r9,                     r9
    
  weak_frontier_check_1:
    ; extract the first mask bit provided as input, placing it into the lowest bit position
    vextracti128            xmm1,                   ymm_mask,               0
    vpextrq                 rcx,                    xmm1,                   0
    shr                     rcx,                    63
    
    ; check if the mask bit is set and, if not, skip ahead to the next weak frontier check because the current edge is invalid
    cmp                     rcx,                    0
    je                      weak_frontier_check_2
    
    ; extract the first vertex ID provided as input
    vextracti128            xmm1,                   ymm_vid,                0
    vpextrq                 rcx,                    xmm1,                   0
    
    ; figure out the corresponding element index in the weak frontier and bit index within that element
    ; each element holds the frontier bits for 64 vertices
    mov                     rdx,                    rcx
    shr                     rdx,                    6
    and                     rcx,                    63
    
    ; place the weak frontier bit for the corresponding vertex ID into the lowest bit position
    mov                     rdx,                    QWORD PTR [r_wfrontier+8*rdx]
    shr                     rdx,                    cl
    
    ; if the bit is not set, skip ahead to the next weak frontier check
    bt                      rdx,                    0
    jnc                     weak_frontier_check_2
    
    ; weak frontier bit is set, so insert a corresponding value into the destination
    mov                     rdx,                    1
    shl                     rdx,                    63
    vextracti128            xmm1,                   ymm_dest,               0
    vpinsrq                 xmm1,                   xmm1,                   rdx,                    0
    vinserti128             ymm_dest,               ymm_dest,               xmm1,                   0
    
    ; increase the count of the number of set weak frontier bits
    inc                     r9
    
  weak_frontier_check_2:
    ; same as above, adjusted for the second bit
    vextracti128            xmm1,                   ymm_mask,               0
    vpextrq                 rcx,                    xmm1,                   1
    shr                     rcx,                    63
    cmp                     rcx,                    0
    je                      weak_frontier_check_3
    vextracti128            xmm1,                   ymm_vid,                0
    vpextrq                 rcx,                    xmm1,                   1
    mov                     rdx,                    rcx
    shr                     rdx,                    6
    and                     rcx,                    63
    mov                     rdx,                    QWORD PTR [r_wfrontier+8*rdx]
    shr                     rdx,                    cl
    bt                      rdx,                    0
    jnc                     weak_frontier_check_3
    mov                     rdx,                    1
    shl                     rdx,                    63
    vextracti128            xmm1,                   ymm_dest,               0
    vpinsrq                 xmm1,                   xmm1,                   rdx,                    1
    vinserti128             ymm_dest,               ymm_dest,               xmm1,                   0
    inc                     r9
  
  weak_frontier_check_3:
    ; same as above, adjusted for the third bit
    vextracti128            xmm1,                   ymm_mask,               1
    vpextrq                 rcx,                    xmm1,                   0
    shr                     rcx,                    63
    cmp                     rcx,                    0
    je                      weak_frontier_check_4
    vextracti128            xmm1,                   ymm_vid,                1
    vpextrq                 rcx,                    xmm1,                   0
    mov                     rdx,                    rcx
    shr                     rdx,                    6
    and                     rcx,                    63
    mov                     rdx,                    QWORD PTR [r_wfrontier+8*rdx]
    shr                     rdx,                    cl
    bt                      rdx,                    0
    jnc                     weak_frontier_check_4
    mov                     rdx,                    1
    shl                     rdx,                    63
    vextracti128            xmm1,                   ymm_dest,               1
    vpinsrq                 xmm1,                   xmm1,                   rdx,                    0
    vinserti128             ymm_dest,               ymm_dest,               xmm1,                   1
    inc                     r9
  
  weak_frontier_check_4:
    ; same as above, adjusted for the fourth bit
    vextracti128            xmm1,                   ymm_mask,               1
    vpextrq                 rcx,                    xmm1,                   1
    shr                     rcx,                    63
    cmp                     rcx,                    0
    je                      weak_frontier_check_final
    vextracti128            xmm1,                   ymm_vid,                1
    vpextrq                 rcx,                    xmm1,                   1
    mov                     rdx,                    rcx
    shr                     rdx,                    6
    and                     rcx,                    63
    mov                     rdx,                    QWORD PTR [r_wfrontier+8*rdx]
    shr                     rdx,                    cl
    bt                      rdx,                    0
    jnc                     weak_frontier_check_final
    mov                     rdx,                    1
    shl                     rdx,                    63
    vextracti128            xmm1,                   ymm_dest,               1
    vpinsrq                 xmm1,                   xmm1,                   rdx,                    1
    vinserti128             ymm_dest,               ymm_dest,               xmm1,                   1
    inc                     r9
  
  weak_frontier_check_final:
    ; if no weak frontier bits are set, jump to the supplied label
    cmp                     r9,                     0
    je                      lbl_skipiter
  
  done_weak_frontier_check_novec:
ENDM

; Adds frontier statistics and convergence information to the specified register.
; Depending on experiment configuration, will either add 1, add the outdegree, or add both.
; Requires a destination register to be pre-initialized with the old value.
; Understands how the address stash works, so requires a vertex identifier as well for extracting the address of the vertex outdegree.
; Uses as scratch and overwrites rcx.
phase_helper_add_frontier_stat              MACRO r_dest, r_vid
IFNDEF EXPERIMENT_THRESHOLD_WITHOUT_COUNT
    ; increase the count of vertices that changed, for convergence detection and next algorithm iteration's engine selection
    inc                     r_dest
ENDIF
IFNDEF EXPERIMENT_THRESHOLD_WITHOUT_OUTDEGREES
    ; if using outdegrees to help with the engine selection threshold, add the destination's outdegree
    vpextrq                 rcx,                    xmm_addrstash,          0
    vcvtsd2si               rcx,                    QWORD PTR [rcx+8*r_vid]
    add                     r_dest,                 rcx
ENDIF
ENDM

; Updates per-iteration statistics used during experiments.
; Adds 1 to the number of vectors encountered by the present thread, and computes the number of valid edges in that vector.
phase_helper_iteration_stats                MACRO
    threads_helper_get_global_thread_id             ecx
    
    ; increment the number of vectors encountered by this thread
    mov                     rax,                    QWORD PTR [graph_stat_num_vectors_per_thread]
    lea                     rax,                    QWORD PTR [rax + 8*rcx]
    inc                     QWORD PTR [rax]
    
    ; compute the address of the variables that counts edges encountered by this thread
    mov                     rax,                    QWORD PTR [graph_stat_num_edges_per_thread]
    lea                     rax,                    QWORD PTR [rax + 8*rcx]
    
    ; sum up the "valid" bits in the loaded edge vector to count the number of edges encountered
    vpsrlq                  ymm0,                   ymm_edgevec,            63
    vpextrq                 rcx,                    xmm0,                   0
    vpextrq                 rdx,                    xmm0,                   1
    add                     rcx,                    rdx
    vextracti128            xmm1,                   ymm0,                   1
    vpextrq                 rdx,                    xmm1,                   0
    add                     rcx,                    rdx
    vpextrq                 rdx,                    xmm1,                   1
    add                     rcx,                    rdx
    
    ; add to the number of edges encountered by this thread
    add                     QWORD PTR [rax],        rcx
ENDM

; Non-vectorized emulation of the "vgatherqpd" for experiments.
; Uses xmm0, rax, and rcx as intermediates.
phase_helper_vgatherqpd_novec               MACRO ymm_result, r_base, ymm_offsets, ymm_mask
    
  vgatherqpd_novec_1:
    ; first element
    ; extract the mask bit first
    ; if it isn't set, skip to the next element
    vextracti128            xmm0,                   ymm_mask,               0
    vpextrq                 rax,                    xmm0,                   0
    bt                      rax,                    63
    jnc                     vgatherqpd_novec_2
    
    ; extract the offset and perform the load
    vextracti128            xmm0,                   ymm_offsets,            0
    vpextrq                 rcx,                    xmm0,                   0
    mov                     rax,                    QWORD PTR [r_base+8*rcx]
    
    ; place the result in the correct spot in the destination
    vextracti128            xmm0,                   ymm_result,             0
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    vinserti128             ymm_result,             ymm_result,             xmm0,                   0
  
  vgatherqpd_novec_2:
    ; second element
    ; same as above, but adjusted for the second position
    vextracti128            xmm0,                   ymm_mask,               0
    vpextrq                 rax,                    xmm0,                   1
    bt                      rax,                    63
    jnc                     vgatherqpd_novec_3
    vextracti128            xmm0,                   ymm_offsets,            0
    vpextrq                 rcx,                    xmm0,                   1
    mov                     rax,                    QWORD PTR [r_base+8*rcx]
    vextracti128            xmm0,                   ymm_result,             0
    vpinsrq                 xmm0,                   xmm0,                   rax,                    1
    vinserti128             ymm_result,             ymm_result,             xmm0,                   0
  
  vgatherqpd_novec_3:
    ; third element
    ; same as above, but adjusted for the third position
    vextracti128            xmm0,                   ymm_mask,               1
    vpextrq                 rax,                    xmm0,                   0
    bt                      rax,                    63
    jnc                     vgatherqpd_novec_4
    vextracti128            xmm0,                   ymm_offsets,            1
    vpextrq                 rcx,                    xmm0,                   0
    mov                     rax,                    QWORD PTR [r_base+8*rcx]
    vextracti128            xmm0,                   ymm_result,             1
    vpinsrq                 xmm0,                   xmm0,                   rax,                    0
    vinserti128             ymm_result,             ymm_result,             xmm0,                   1
  
  vgatherqpd_novec_4:
    ; fourth element
    ; same as above, but adjusted for the fourth position
    vextracti128            xmm0,                   ymm_mask,               1
    vpextrq                 rax,                    xmm0,                   1
    bt                      rax,                    63
    jnc                     vgatherqpd_novec_done
    vextracti128            xmm0,                   ymm_offsets,            1
    vpextrq                 rcx,                    xmm0,                   1
    mov                     rax,                    QWORD PTR [r_base+8*rcx]
    vextracti128            xmm0,                   ymm_result,             1
    vpinsrq                 xmm0,                   xmm0,                   rax,                    1
    vinserti128             ymm_result,             ymm_result,             xmm0,                   1
  
  vgatherqpd_novec_done:
ENDM


ENDIF ;__GRAZELLE_PHASEHELPERS_INC
