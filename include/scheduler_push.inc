;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; scheduler_push.inc
;      References and macros that are used to schedule work to threads.
;      Behave like functions in terms of the ABI each uses.
;      Intended to be invoked by threads during the main processing phase.
;      This version targets the push engine.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

IFNDEF __GRAZELLE_SCHEDULER_INC
__GRAZELLE_SCHEDULER_INC EQU 1


EXTRN threads_barrier:PROC


; --------- CONSTANTS ---------------------------------------------------------

; Log2(number of units of work to create per thread).
SCHED_PUSH_UNITS_PER_THREAD_LOG2            TEXTEQU     <5>


; --------- MACROS ------------------------------------------------------------

; Retrieves the address of the current thread group's dynamic scheduling counter into rax.
; Overwrites rcx, used as scratch.
scheduler_get_dynamic_counter_address       MACRO
    threads_helper_get_thread_group_id              ecx
    mov                     rax,                    QWORD PTR [graph_scheduler_dynamic_counter_numa]
    mov                     rax,                    QWORD PTR [rax+8*rcx]
ENDM

; Initializes the work scheduler at the beginning of the current Edge phase.
; Sets the dynamic scheduler counter to # threads in the current group.
scheduler_phase_init                        MACRO
    ; only thread 0 within the current group needs to perform this operation
    threads_helper_get_local_thread_id              eax
    cmp                     eax,                    0
    jne                     scheduler_phase_init_done
    
    ; get the number of threads per group
    threads_helper_get_threads_per_group            ebx
    
    ; write this value into the dynamic scheduling counter
    scheduler_get_dynamic_counter_address
    mov                     QWORD PTR [rax],        rbx
    
  scheduler_phase_init_done:
    call                    threads_barrier
ENDM

; Retrieves and returns the number of units of work in the current phase.
; No parameters.
scheduler_get_num_units                     MACRO
    threads_helper_get_threads_per_group            eax
    
    ; SCHED_PUSH_UNITS_PER_THREAD_LOG2 units per thread
    shl                     rax,                    SCHED_PUSH_UNITS_PER_THREAD_LOG2
ENDM

; Retrieves and returns the index of the first unit of work to assign to the calling thread.
; If no unit is available, returns -1.
; No parameters.
scheduler_get_first_assigned_unit           MACRO
    ; first assignment = local thread ID
    threads_helper_get_local_thread_id              eax
ENDM

; Retrieves and returns the index of the next unit of work to assign to the calling thread.
; If no unit is available, returns -1.
; No parameters.
scheduler_get_next_assigned_unit            MACRO
    ; get the address of the dynamic scheduler counter, atomically increment, and obtain the old value in rcx
    scheduler_get_dynamic_counter_address
    mov                     rcx,                    1
    lock xadd               QWORD PTR [rax],        rcx
    
    ; get the number of units of work for the current group, place into rdx
    scheduler_get_num_units
    mov                     rdx,                    rax
    
    ; compare the proposed assignment with number of units available: if assignment >= number of units, return -1
    mov                     rax,                    rcx
    xor                     rcx,                    rcx
    dec                     rcx
    cmp                     rax,                    rdx
    cmovge                  rax,                    rcx
ENDM

; Sets the work assignment (first edge vector index, last edge vector index) into rsi and rdi, respectively.
; Since the schedule is by edges, there is never a need to look up a vertex index, so this version is engine-independent.
; If the result is that rdi < rsi, then there is no work available.
; No return.
; Parameters: work unit index (rcx), total # edge vectors (rdx).
scheduler_assign_work_for_unit              MACRO
    ; each unit of work represents a fixed-size number of edges, assigned first-come-first-served to threads that call this function
    ; of course, threads should use the other functions to receive their work unit assignments first
    ;
    ; parameters:
    ;    rcx: unit_index
    ;    rdx: #vectors (rdx)
    ;
    ; formulas:
    ;    assignment  = #vectors / #units_of_work
    ;    addon       = #vectors % #units_of_work < unit_index ? 1 : 0
    ;    prev_addons = min(#vectors % #units_of_work, unit_index)
    ;
    ;    base (rsi)  = (assignment * unit_index) + prev_addons
    ;    max  (rdi)  = base + assignment + addon - 1
    
    ; first, perform the unsigned division by setting rdx:rax = #vectors and dividing by #units_of_work
    ; afterwards, rax contains the quotient ("assignment" in the formulas above) and rdx contains the remainder
    scheduler_get_num_units
    mov                     rbx,                    rax
    mov                     rax,                    rdx
    xor                     rdx,                    rdx
    div                     rbx
    
    ; calculate other values using unit_index, which was passed in as a parameter in rcx
    ; can be used directly to obtain "addon" (rbx) and "prev_addons" (rsi)
    xor                     rbx,                    rbx
    mov                     rsi,                    rdx
    mov                     rdi,                    0000000000000001h
    cmp                     rcx,                    rdx
    cmovl                   rbx,                    rdi
    cmovl                   rsi,                    rcx
    
    ; create some partial values using the calculated quantities
    ; rsi (base) = prev_addons - this was done above, rdi (max) = assignment + addon - 1
    ; note that because "jge" is used below and not "jg", skip the -1, since "jge" requires that rdi be (last index to process + 1)
    mov                     rdi,                    rax
    add                     rdi,                    rbx
    
    ; perform multiplication of assignment * unit_index, result in rax
    ; use the result to add to rsi and figure out "base", then add to rdi to get "max"
    mul                     rcx
    add                     rsi,                    rax
    add                     rdi,                    rsi
ENDM


ENDIF ;__GRAZELLE_SCHEDULER_INC
