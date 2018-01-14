;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; threadhelpers.inc
;      Thread-related macros for use in assembly.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

IFNDEF __GRAZELLE_THREADHELPERS_INC
__GRAZELLE_THREADHELPERS_INC EQU 1


INCLUDE registers.inc


; --------- MACROS ------------------------------------------------------------

; Retrieves the local thread ID and places it in the specified 32-bit register
threads_helper_get_local_thread_id          MACRO edest
    vpextrd                 edest,                  xmm_threadinfo,         0
ENDM

; Retrieves the global thread ID and places it in the specified 32-bit register
threads_helper_get_global_thread_id         MACRO edest
    vpextrd                 edest,                  xmm_threadinfo,         1
ENDM

; Retrieves the thread group ID and places it in the specified 32-bit register
threads_helper_get_thread_group_id          MACRO edest
    vpextrd                 edest,                  xmm_threadinfo,         2
ENDM

; Retrieves the number of threads per group and places it in the specified 32-bit register
threads_helper_get_threads_per_group        MACRO edest
    vpextrd                 edest,                  xmm_threadinfo,         3
ENDM

; Retrieves the total number of threads and places it in the specified 32-bit register
threads_helper_get_total_threads            MACRO edest
    vextractf128            xmm0,                   ymm_threadinfo,         1
    vpextrd                 edest,                  xmm0,                   0
ENDM

; Retrieves the total number of thread groups and places it in the specified 32-bit register
threads_helper_get_total_groups             MACRO edest
    vextractf128            xmm0,                   ymm_threadinfo,         1
    vpextrd                 edest,                  xmm0,                   1
ENDM

; Sets the per-thread 64-bit variable from the specified 64-bit source register
threads_helper_set_per_thread_variable      MACRO rsrc
    vextractf128            xmm0,                   ymm_threadinfo,         1
    vpinsrq                 xmm0,                   xmm0,                   rsrc,                   1
    vinsertf128             ymm_threadinfo,         ymm_threadinfo,         xmm0,                   1
ENDM

; Retrieves the per-thread 64-bit variable and places it in the specified 64-bit register
threads_helper_get_per_thread_variable      MACRO rdest
    vextractf128            xmm0,                   ymm_threadinfo,         1
    vpextrq                 rdest,                  xmm0,                   1
ENDM


ENDIF ;__GRAZELLE_THREADHELPERS_INC
