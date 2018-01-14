;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; constants.asm
;      Definitions of commonly-used constants.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;


CONST                                       SEGMENT ALIGN(32)


; --------- CONSTANTS ---------------------------------------------------------
; Not intended to be accessed from C. See "constants.inc" for documentation.

PUBLIC const_numa_merge_mask_lower
const_numa_merge_mask_lower                 REAL8       0.0
                                            REAL8       1.0
                                            REAL8       2.0
                                            REAL8       3.0

PUBLIC const_numa_merge_mask_upper
const_numa_merge_mask_upper                 REAL8       4.0
                                            REAL8       5.0
                                            REAL8       6.0
                                            REAL8       7.0

PUBLIC const_infinity
const_infinity                              DQ          7ff0000000000000H
                                            DQ          7ff0000000000000H
                                            DQ          7ff0000000000000H
                                            DQ          7ff0000000000000H

PUBLIC const_one
const_one                                   REAL8       1.0
                                            REAL8       1.0
                                            REAL8       1.0
                                            REAL8       1.0

PUBLIC const_minusone
const_minusone                              REAL8       -1.0
                                            REAL8       -1.0
                                            REAL8       -1.0
                                            REAL8       -1.0

PUBLIC const_vid_and_mask
const_vid_and_mask                          DQ          7fff000000000000H
                                            DQ          7fff000000000000H
                                            DQ          7fff000000000000H
                                            DQ          0007000000000000H

PUBLIC const_edge_list_and_mask  
const_edge_list_and_mask                    DQ          0000ffffffffffffH
                                            DQ          0000ffffffffffffH
                                            DQ          0000ffffffffffffH
                                            DQ          0000ffffffffffffH

PUBLIC const_edge_mask_and_mask  
const_edge_mask_and_mask                    DQ          8000000000000000H
                                            DQ          8000000000000000H
                                            DQ          8000000000000000H
                                            DQ          8000000000000000H

PUBLIC const_positive_sign_and_mask         
const_positive_sign_and_mask                DQ          7fffffffffffffffH
                                            DQ          7fffffffffffffffH
                                            DQ          7fffffffffffffffH
                                            DQ          7fffffffffffffffH

PUBLIC const_damping_factor
const_damping_factor                        REAL8       0.875
                                            REAL8       0.875
                                            REAL8       0.875
                                            REAL8       0.875


CONST                                       ENDS


END
