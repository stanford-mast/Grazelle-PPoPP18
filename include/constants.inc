;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; constants.inc
;      Declarations of commonly-used constants.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

IFNDEF __GRAZELLE_CONSTANTS_INC
__GRAZELLE_CONSTANTS_INC EQU 1


; --------- CONSTANTS ---------------------------------------------------------

; An aligned, 256-bit constant that contains {0.0, 1.0, 2.0, 3.0}, to be loaded into an AVX register.
EXTRN const_numa_merge_mask_lower:QWORD

; An aligned, 256-bit constant that contains {4.0, 5.0, 6.0, 7.0}, to be loaded into an AVX register.
EXTRN const_numa_merge_mask_upper:QWORD

; An aligned, 256-bit constant that contains the value +INFINITY four times, to be loaded into an AVX register.
EXTRN const_infinity:QWORD

; An aligned, 256-bit constant that contains the value 1.0 four times, to be loaded into an AVX register
EXTRN const_one:QWORD

; An aligned, 256-bit constant that contains the value -1.0 four times, to be loaded into an AVX register
EXTRN const_minusone:QWORD

; An aligned, 256-bit constant representing the mask for bitwise-AND to obtain the spread-encoding vertex identifier
EXTRN const_vid_and_mask:QWORD

; An aligned, 256-bit constant representing the mask for bitwise-AND to obtain the neighbor list, either sources or destinations, from an edge vector
EXTRN const_edge_list_and_mask:QWORD

; An aligned, 256-bit constant representing the mask for bitwise-AND to obtain the edge validity mask from an edge vector
EXTRN const_edge_mask_and_mask:QWORD

; An aligned, 256-bit constant representing the mask for bitwise-AND to filter out any sign bits and make all values positive
EXTRN const_positive_sign_and_mask:QWORD

; An aligned, 256-bit constant holding the value of the damping factor for use with PageRank
EXTRN const_damping_factor:QWORD


ENDIF ;__GRAZELLE_CONSTANTS_INC
