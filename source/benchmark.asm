;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; benchmark.asm
;      Implementation of some benchmarking-related helper operations. Most
;      benchmarking operations are implemented in C.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


_TEXT                                       SEGMENT


; --------- FUNCTIONS ---------------------------------------------------------
; See "benchmark.h" for documentation.

benchmark_rdtsc                             PROC PUBLIC
    lfence
    rdtsc
    
    shl                     rdx,                    32
    or                      rax,                    rdx
    
    ret
benchmark_rdtsc                             ENDP


_TEXT                                       ENDS


END
