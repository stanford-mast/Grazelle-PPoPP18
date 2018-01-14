;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_vertex_pr.asm
;      Implementation of the Vertex phase for PageRank.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

INCLUDE constants.inc
INCLUDE graphdata.inc
INCLUDE phasehelpers.inc
INCLUDE registers.inc
INCLUDE threadhelpers.inc


_TEXT                                       SEGMENT


; --------- MACROS ------------------------------------------------------------

; Initializes required state for the Vertex phase.
vertex_op_initialize                        MACRO
    ; perform common initialization tasks
    phase_helper_set_base_addrs
    phase_helper_set_graph_info
    
    ; initialize the array of vertex outdegrees
    mov                     r_outdeglist,           QWORD PTR [graph_vertex_outdegrees]
    
    ; compensate for the first vertex assigned to be processed (assignment passed as a parameter in rcx)
    shl                     rcx,                    3
    add                     r_vprop,                rcx
    add                     r_vaccum,               rcx
    add                     r_outdeglist,           rcx
    
    ; compute the number of loads that need to be performed on the reduce buffer
    ; there is one entry in that buffer per thread, and loads happen in blocks of 4
    ; formula for number of loads is 1 + ((total number of threads - 1) / 4), can skip the first increment and take care of it later
    threads_helper_get_total_threads                eax
    dec                     rax
    shr                     rax,                    2
    
    ; initialize for the load loop - the ymm accumulator and the iteration counter
    vxorpd                  ymm1,                   ymm1,                   ymm1
    xor                     r10,                    r10

    ; load and add the entire content of the reduce buffer, which contains partial PageRank sums from the gather phase
    ; base address was passed as a parameter in r8
  sum_reduce_loop:
    vaddpd                  ymm1,                   ymm1,                   YMMWORD PTR [r8+r10]
    add                     r10,                    32
    dec                     rax
    jge                     sum_reduce_loop
    
    ; perform a reduction to compute the total and complete PageRank sum
    vhaddpd                 ymm1,                   ymm1,                   ymm1
    vextractf128            xmm0,                   ymm1,                   1
    vaddpd                  xmm0,                   xmm0,                   xmm1
    vbroadcastsd            ymm0,                   xmm0
    
    ; calculate (1 - [total PageRank sum]) / V which represents the PageRank correction factor to account for sink vertices
    mov                     rax,                    1
    vcvtsi2sd               xmm_globvars,           xmm_globvars,           rax
    vsubpd                  xmm_globvars,           xmm_globvars,           xmm0
    vdivpd                  xmm_globvars,           xmm_globvars,           xmm_numvertices
    vbroadcastsd            ymm_globvars,           xmm_globvars
ENDM

; Performs an iteration of the Vertex phase at the specified index.
vertex_op_iteration_at_index                MACRO
    ; calculate the base index for write, which is just (index << 3)
    ; to get a byte offset, just multiply by 8 (another << 3)
    mov                     r_woffset,              rcx
    shl                     r_woffset,              6
    
    ; load from the outdegree array to properly divide by a vertex's outdegree
    vmovntdqa               ymm1,                   YMMWORD PTR [r_outdeglist+r_woffset+0]
    vmovntdqa               ymm2,                   YMMWORD PTR [r_outdeglist+r_woffset+32]
    
    ; if an outdegree happens to be equal to zero, it must be set instead to the number of vertices in the graph
    vxorpd                  ymm0,                   ymm0,                   ymm0
    vcmpeqpd                ymm6,                   ymm0,                   ymm1
    vcmpeqpd                ymm7,                   ymm0,                   ymm2
    vblendvpd               ymm1,                   ymm1,                   ymm_numvertices,        ymm6
    vblendvpd               ymm2,                   ymm2,                   ymm_numvertices,        ymm7
    
    ; base address for read is just equal to the vertex index, which is 8 * the iteration index
    ; to get a byte offset, just multiply by 8 (or << 3)
    shl                     rcx,                    6
    add                     rcx,                    r_vaccum
    
    ; read from the accumulators
    vmovntdqa               ymm_caccum1,            YMMWORD PTR [rcx+0]
    vmovntdqa               ymm_caccum2,            YMMWORD PTR [rcx+32]
    
IFDEF EXPERIMENT_EDGE_FORCE_PUSH
    ; reset the accumulators if using Edge-Push for PageRank
    vxorpd                  ymm0,                   ymm0,                   ymm0
    vmovntpd                YMMWORD PTR [rcx+0],    ymm0
    vmovntpd                YMMWORD PTR [rcx+32],   ymm0
ELSE
IFDEF EXPERIMENT_EDGE_PULL_WITHOUT_SCHED_AWARE
    ; reset the accumulators if using Edge-Pull without scheduler awareness for PageRank
    vxorpd                  ymm0,                   ymm0,                   ymm0
    vmovntpd                YMMWORD PTR [rcx+0],    ymm0
    vmovntpd                YMMWORD PTR [rcx+32],   ymm0
ENDIF
ENDIF
    
    ; add the sink vertex constant correction factor to both accumulators
    vaddpd                  ymm_caccum1,            ymm_caccum1,            ymm_globvars
    vaddpd                  ymm_caccum2,            ymm_caccum2,            ymm_globvars
    
    ; multiply by the damping factor and add to the constant ((1 + d) / V)
    vmulpd                  ymm_caccum1,            ymm_caccum1,            ymm_damping
    vmulpd                  ymm_caccum2,            ymm_caccum2,            ymm_damping
    vaddpd                  ymm_caccum1,            ymm_caccum1,            ymm_1_minus_d_by_V
    vaddpd                  ymm_caccum2,            ymm_caccum2,            ymm_1_minus_d_by_V
    
    ; divide by the outdegree to calculate the final outbound message
    vdivpd                  ymm_caccum1,            ymm_caccum1,            ymm1
    vdivpd                  ymm_caccum2,            ymm_caccum2,            ymm2
    
    ; final store to the vertex properties
    vmovntpd                YMMWORD PTR [r_vprop+r_woffset+0],              ymm_caccum1
    vmovntpd                YMMWORD PTR [r_vprop+r_woffset+32],             ymm_caccum2
    
    ; finished this iteration of the Vertex phase
ENDM

; Performs an iteration of the Vertex phase at the specified index.
; This is the non-vectorized version.
vertex_op_iteration_at_index_novec          MACRO
    ; calculate the base index for write, which is just (index << 0)
    ; to get a byte offset, just multiply by 8 (another << 3)
    mov                     r_woffset,              rcx
    shl                     r_woffset,              3
    
    ; load from the outdegree array to properly divide by a vertex's outdegree
    vmovq                   xmm1,                   QWORD PTR [r_outdeglist+r_woffset]
    
    ; if an outdegree happens to be equal to zero, it must be instead set to the number of vertices in the graph
    vmovq                   rax,                    xmm_numvertices
    vmovq                   rdx,                    xmm1
    cmp                     rdx,                    0
    cmove                   rdx,                    rax
    vmovq                   xmm1,                   rdx
    
    ; base address for read is just equal to the vertex index, which is equal to the iteration index
    ; to get a byte offset, just multiply by 8 (or << 3)
    shl                     rcx,                    3
    add                     rcx,                    r_vaccum
    
    ; read from the accumulator
    vmovq                   xmm_caccum1,            QWORD PTR [rcx]

IFDEF EXPERIMENT_EDGE_FORCE_PUSH
    ; reset the accumulator if using Scatter for PageRank
    ; this is not necessary otherwise
    xor                     rax,                    rax
    mov                     QWORD PTR [rcx],        rax
ENDIF
    
    ; add the sink vertex constant correction factor to the accumulator
    vaddpd                  xmm_caccum1,            xmm_caccum1,            xmm_globvars
    
    ; multiply by the damping factor and add to the constant ((1 + d) / V)
    vmulpd                  xmm_caccum1,            xmm_caccum1,            xmm_damping
    vaddpd                  xmm_caccum1,            xmm_caccum1,            xmm_1_minus_d_by_V
    
    ; divide by the outdegree to calculate the final outbound message
    vdivpd                  xmm_caccum1,            xmm_caccum1,            xmm1
    
    ; final store to the vertex properties
    vmovq                   rax,                    xmm_caccum1
    movnti                  QWORD PTR [r_vprop+r_woffset],                  rax
    
    ; finished this iteration of the Vertex phase
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_vertex_phase_pr                     PROC PUBLIC
    ; save non-volatile registers used throughout this phase
    push                    rbx
    push                    rsi
    push                    rdi
    push                    r12
    push                    r13
    push                    r14
    push                    r15
    
    ; initialize
    vertex_op_initialize
    
IFDEF EXPERIMENT_WITHOUT_VECTORS
    ; total number of iterations across all threads is just the number of vertices assigned to be processed
    ; number of vertices is passed in rdx
    ; there is nothing special that needs to be done here
ELSE
    ; calculate the number of total iterations across all threads based on the number of vertices
    ; number of vertices is passed in rdx
    ; where V is the number of vertices, number of iterations is equal to (V/8) + (V%8 ? 1 : 0)
    ; by the time this is done, rdx stores the number of iterations
    mov                     rcx,                    rdx
    shr                     rdx,                    3
    and                     rcx,                    7
    je                      skip_add_extra_iteration
    inc                     rdx
  skip_add_extra_iteration:
ENDIF
    
    ; extract thread information useful as loop controls, assigning chunks to each thread round-robin
    ; formulas:
    ;    assignment  = #iterations / #total_threads
    ;    addon       = #iterations % #total_threads < global_thread_id ? 1 : 0
    ;    prev_addons = min(#iterations % #total_threads, global_thread_id)
    ;
    ;    base (rsi)  = (assignment * global_thread_id) + prev_addons
    ;    inc         = 1
    ;    max  (rdi)  = base + assignment + addon - 1
    
    ; first, perform the unsigned division by setting rdx:rax = #iterations and dividing by #total_threads
    ; afterwards, rax contains the quotient ("assignment" in the formulas above) and rdx contains the remainder
    mov                     rax,                    rdx
    xor                     rdx,                    rdx
    xor                     rcx,                    rcx
    threads_helper_get_threads_per_group            ecx
    div                     rcx
    
    ; to calculate other values using total_threads, extract it to rcx
    ; can be used directly to obtain "addon" (rbx) and "prev_addons" (rsi)
    threads_helper_get_local_thread_id              ecx
    xor                     rbx,                    rbx
    mov                     rsi,                    rdx
    mov                     rdi,                    0000000000000001h
    cmp                     rcx,                    rdx
    cmovl                   rbx,                    rdi
    cmovl                   rsi,                    rcx
    
    ; create some partial values using the calculated quantities
    ; rsi (base) = prev_addons - this was done above, rdi (max) = assignment + addon - 1
    ; note that because we are using "jge" below and not "jg", we skip the -1, since "jge" requires that rdi be (last index to process + 1)
    mov                     rdi,                    rax
    add                     rdi,                    rbx
    
    ; perform multiplication of assignment * total_threads, result in rax
    ; use the result to add to rsi and figure out "base", then add to rdi to get "max"
    mul                     rcx
    add                     rsi,                    rax
    add                     rdi,                    rsi
    
    ; main Vertex phase loop
  vertex_phase_loop:
    cmp                     rsi,                    rdi
    jge                     done_vertex_loop
    
    mov                     rcx,                    rsi
IFDEF EXPERIMENT_WITHOUT_VECTORS
    vertex_op_iteration_at_index_novec
ELSE
    vertex_op_iteration_at_index
ENDIF
    
    inc                     rsi
    jmp                     vertex_phase_loop
  done_vertex_loop:

    ; restore non-volatile registers and return
    pop                     r15
    pop                     r14
    pop                     r13
    pop                     r12
    pop                     rdi
    pop                     rsi
    pop                     rbx
    ret
perform_vertex_phase_pr                     ENDP


_TEXT                                       ENDS


END
