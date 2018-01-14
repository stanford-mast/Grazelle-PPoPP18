;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; threads.asm
;      Implementation of some lightweight multithreading operations. The rest
;      are implemented in C.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

INCLUDE registers.inc
INCLUDE threadhelpers.inc


DATA                                        SEGMENT ALIGN(64)


; --------- LOCALS ------------------------------------------------------------

; Storage area for the counter of threads that have reached a barrier.
thread_barrier_counter                      DQ          0000000000000000h

; Storage area for the total number of spawned threads.
thread_spawned_count                        DQ          0000000000000000h

; Padding to ensure that the next variable is in a different cache line.
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h

; Thread barrier flag, where threads spin until all threads have passed the barrier.
; Reserve a full cache line for this.
thread_barrier_flag                         DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h
                                            DQ          0000000000000000h


DATA                                        ENDS


_TEXT                                       SEGMENT


; --------- INTERNAL FUNCTIONS ------------------------------------------------
; See "threads.c" for documentation.

threads_init                                PROC PUBLIC
    ; Record the number of threads that have been spawned and initialize the barrier counter
    mov                     DWORD PTR [thread_spawned_count],               ecx
    mov                     DWORD PTR [thread_barrier_counter],             ecx
    ret
threads_init                                ENDP

; ---------

threads_save_context_to                     PROC PUBLIC
    mov                     QWORD PTR [rcx+0],      r12
    mov                     QWORD PTR [rcx+8],      r13
    mov                     QWORD PTR [rcx+16],     r14
    mov                     QWORD PTR [rcx+24],     r15
    mov                     QWORD PTR [rcx+32],     rdi
    mov                     QWORD PTR [rcx+40],     rsi
    mov                     QWORD PTR [rcx+48],     rbx
    mov                     QWORD PTR [rcx+56],     rbp
    ret
threads_save_context_to                     ENDP

; ---------

threads_submit_common_thread_info           PROC PUBLIC
    vpinsrd                 xmm_threadinfo,         xmm_threadinfo,         ecx,                    0           ; local thread ID
    vpinsrd                 xmm_threadinfo,         xmm_threadinfo,         edx,                    1           ; global thread ID
    
    mov                     rcx,                    r8
    mov                     rdx,                    r9
    
    vpinsrd                 xmm_threadinfo,         xmm_threadinfo,         ecx,                    2           ; thread group number
    vpinsrd                 xmm_threadinfo,         xmm_threadinfo,         edx,                    3           ; threads per group
    
    ret
threads_submit_common_thread_info           ENDP

; ---------

threads_submit_other_thread_info            PROC PUBLIC
    xor                     rax,                    rax
    vpinsrq                 xmm0,                   xmm0,                   rax,                    1           ; per-thread 64-bit variable (initialize to 0)
    
    vpinsrd                 xmm0,                   xmm0,                   ecx,                    0           ; total number of threads globally
    vpinsrd                 xmm0,                   xmm0,                   edx,                    1           ; total number of groups globally
    vinsertf128             ymm_threadinfo,         ymm_threadinfo,         xmm0,                   1
    ret
threads_submit_other_thread_info            ENDP

; ---------

threads_restore_context_from                PROC PUBLIC
    mov                     r12,                    QWORD PTR [rcx+0]
    mov                     r13,                    QWORD PTR [rcx+8]
    mov                     r14,                    QWORD PTR [rcx+16]
    mov                     r15,                    QWORD PTR [rcx+24]
    mov                     rdi,                    QWORD PTR [rcx+32]
    mov                     rsi,                    QWORD PTR [rcx+40]
    mov                     rbx,                    QWORD PTR [rcx+48]
    mov                     rbp,                    QWORD PTR [rcx+56]
    ret
threads_restore_context_from                ENDP


; --------- FUNCTIONS ---------------------------------------------------------
; See "threads.h" for documentation.

threads_get_local_thread_id                 PROC PUBLIC
    threads_helper_get_local_thread_id              eax
    ret
threads_get_local_thread_id                 ENDP

; ---------

threads_get_global_thread_id                PROC PUBLIC
    threads_helper_get_global_thread_id             eax
    ret
threads_get_global_thread_id                ENDP

; ---------

threads_get_thread_group_id                 PROC PUBLIC
    threads_helper_get_thread_group_id              eax
    ret
threads_get_thread_group_id                 ENDP

; ---------

threads_get_threads_per_group               PROC PUBLIC
    threads_helper_get_threads_per_group            eax
    ret
threads_get_threads_per_group               ENDP

; ---------

threads_get_total_threads                   PROC PUBLIC
    threads_helper_get_total_threads                eax
    ret
threads_get_total_threads                   ENDP

; ---------

threads_get_total_groups                    PROC PUBLIC
    threads_helper_get_total_groups                 eax
    ret
threads_get_total_groups                    ENDP

; ---------

threads_set_per_thread_variable             PROC PUBLIC
    threads_helper_set_per_thread_variable          rcx
    ret
threads_set_per_thread_variable             ENDP

; ---------

threads_get_per_thread_variable             PROC PUBLIC
    threads_helper_get_per_thread_variable          rax
    ret
threads_get_per_thread_variable             ENDP

; ---------

threads_barrier                             PROC PUBLIC
    ; read in the current value of the thread barrier flag
    mov                     edx,                    DWORD PTR [thread_barrier_flag]

    ; atomically decrement the thread barrier counter and start waiting if needed
    mov                     eax,                    0ffffffffh
    lock xadd               DWORD PTR [thread_barrier_counter],             eax
    jne                     barrier_loop

    ; if all other threads have been here, clean up and signal them to wake up
    mov                     ecx,                    DWORD PTR [thread_spawned_count]
    mov                     DWORD PTR [thread_barrier_counter],             ecx
    inc                     DWORD PTR [thread_barrier_flag]
    jmp                     barrier_done

    ; wait here for the signal
  barrier_loop:
    pause
    cmp                     edx,                    DWORD PTR [thread_barrier_flag]
    je                      barrier_loop
    
  barrier_done:
    ret
threads_barrier                             ENDP

; ---------

threads_merge_barrier                       PROC PUBLIC
    ; read in the current value of the thread barrier flag
    mov                     edx,                    DWORD PTR [thread_barrier_flag]

    ; atomically decrement the thread barrier counter and start waiting if needed
    mov                     eax,                    0ffffffffh
    lock xadd               DWORD PTR [thread_barrier_counter],             eax
    jne                     merge_barrier_loop

    ; if all other threads have been here, clean up and signal them to wake up
    mov                     ecx,                    DWORD PTR [thread_spawned_count]
    mov                     DWORD PTR [thread_barrier_counter],             ecx
    inc                     DWORD PTR [thread_barrier_flag]
    jmp                     merge_barrier_done

    ; wait here for the signal
  merge_barrier_loop:
    pause
    cmp                     edx,                    DWORD PTR [thread_barrier_flag]
    je                      merge_barrier_loop
    
  merge_barrier_done:
    ret
threads_merge_barrier                       ENDP

; ---------

threads_timed_barrier                       PROC PUBLIC
    ; capture the initial timestamp
    lfence
    rdtsc
    shl                     rdx,                    32
    or                      rax,                    rdx
    mov                     r8,                     rax
    
    ; perform the barrier
    call                    threads_barrier
    
    ; capture the final timestamp and calculate the time taken
    lfence
    rdtsc
    shl                     rdx,                    32
    or                      rax,                    rdx
    sub                     rax,                    r8
    
    ret
threads_timed_barrier                       ENDP


_TEXT                                       ENDS


END
