;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; phases.asm
;      Implementation of common phase-related operators.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

INCLUDE registers.inc
INCLUDE threadhelpers.inc


_TEXT                                       SEGMENT


; --------- PROCESSING PHASE OPERATORS ----------------------------------------
; See "phases.h" for documentation.

phase_op_reset_global_accum                 PROC PUBLIC
    ; clear out the partial sum accumulator
    vxorpd                  ymm_globaccum,          ymm_globaccum,          ymm_globaccum
    
    ; operation complete
    ret
phase_op_reset_global_accum                 ENDP

; ---------

phase_op_write_global_accum_to_buf          PROC PUBLIC
    ; extract the current thread ID * 8 to use as an index into the reduce buffer
    threads_helper_get_global_thread_id             eax
    shl                     rax,                    3

    ; perform the write
    vpextrq                 QWORD PTR [rcx+rax],    xmm_globaccum,          0

    ; operation complete
    ret
phase_op_write_global_accum_to_buf          ENDP


_TEXT                                       ENDS


END
