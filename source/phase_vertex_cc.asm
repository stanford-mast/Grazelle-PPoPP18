;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phase_vertex_cc.asm
;      Implementation of the Vertex phase for Connected Components.
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
    
    ; compensate for the first vertex assigned to be processed (assignment passed as a parameter in rcx)
    shr                     rcx,                    3
    add                     r_vaccum,               rcx
ENDM

; Performs an iteration of the Vertex phase at the specified index.
vertex_op_iteration_at_index                MACRO
    ; calculate the base index for write, which is just (index << 3)
    ; to get a byte offset, just multiply by 8 (another << 3)
    mov                     r_woffset,              rcx
    shl                     r_woffset,              6
    
    ; zero out the assigned vertices in the accumulators
    vmovntpd                YMMWORD PTR [r_vaccum+r_woffset+0],             ymm_caccum1
    vmovntpd                YMMWORD PTR [r_vaccum+r_woffset+32],            ymm_caccum2
ENDM

; Performs an iteration of the Vertex phase at the specified index.
; This is the non-vectorized version.
vertex_op_iteration_at_index_novec          MACRO
    ; calculate the base index for write, which is just (index << 0)
    ; to get a byte offset, just multiply by 8 (another << 3)
    mov                     r_woffset,              rcx
    shl                     r_woffset,              3
    
    ; zero out the assigned vertex in the accumulator
    movnti                  QWORD PTR [r_vaccum+r_woffset],                 r10
ENDM


; --------- PHASE CONTROL FUNCTION --------------------------------------------
; See "phases.h" for documentation.

perform_vertex_phase_cc                     PROC PUBLIC
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
    ; the job of this phase is to zero out the accumulator, so create a zero-valued register for that purpose
    xor                     r10,                    r10
ELSE
    ; the job of this phase is to zero out the accumulator, so initialize some AVX registers for that purpose
    vxorpd                  ymm_caccum1,            ymm_caccum1,            ymm_caccum1
    vxorpd                  ymm_caccum2,            ymm_caccum2,            ymm_caccum2
ENDIF
    
    ; calculate the number of total iterations across all threads based on the number of vertices
    ; by the time this is done, rdx stores the number of iterations
IFDEF EXPERIMENT_WITHOUT_VECTORS
    ; where V is the number of vertices, number of iterations is equal to (V/64) + (V%64 ? 1 : 0)
    ; number of vertices is passed in rdx
    mov                     rcx,                    rdx
    shr                     rdx,                    6
    and                     rcx,                    63
ELSE
    ; where V is the number of vertices, number of iterations is equal to (V/512) + (V%512 ? 1 : 0)
    ; number of vertices is passed in rdx
    mov                     rcx,                    rdx
    shr                     rdx,                    9
    and                     rcx,                    511
ENDIF
    je                      skip_add_extra_iteration
    inc                     rdx
  skip_add_extra_iteration:
    
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
perform_vertex_phase_cc                     ENDP


_TEXT                                       ENDS


END
