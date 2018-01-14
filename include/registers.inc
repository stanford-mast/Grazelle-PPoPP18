;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; registers.inc
;      Name mappings for registers used throughout this program. Intended to
;      do proper book-keeping of ymm registers that are reserved and otherwise
;      used so nobody clobbers anybody else and registers are used effectively.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

IFNDEF __GRAZELLE_REGISTERS_INC
__GRAZELLE_REGISTERS_INC EQU 1


; --------- GLOBALLY-RESERVED CONSTANT REGISTERS ------------------------------
; These are set once using a common operation function call and then never changed.
; Note that the only registers allowed in this section are vector registers, since integer registers participate in various calling conventions.

xmm_threadinfo                              TEXTEQU     <xmm15>     ; thread information, used by the threading subsystem
ymm_threadinfo                              TEXTEQU     <ymm15>
xmm_numvertices                             TEXTEQU     <xmm14>     ; number of vertices in the graph
ymm_numvertices                             TEXTEQU     <ymm14>
xmm_damping                                 TEXTEQU     <xmm13>     ; PageRank: damping factor
ymm_damping                                 TEXTEQU     <ymm13>
xmm_one                                     TEXTEQU     <xmm13>     ; BFS/CC: constant +1.0
ymm_one                                     TEXTEQU     <ymm13>
xmm_1_minus_d_by_V                          TEXTEQU     <xmm12>     ; PageRank: 1 minus the damping factor divided by the number of vertices
ymm_1_minus_d_by_V                          TEXTEQU     <ymm12>
xmm_infinity                                TEXTEQU     <xmm12>     ; CC: constant +INFINITY
ymm_infinity                                TEXTEQU     <ymm12>
xmm_minusone                                TEXTEQU     <xmm12>     ; BFS: constant -1.0
ymm_minusone                                TEXTEQU     <ymm12>


; --------- PURPOSE-NAMED REGISTERS (COMMON) ----------------------------------
; These are named for specific purposes as a convenience in reading the code.
; They can be used in any phase of execution but must be initialized at the start of that phase.

r_vprop                                     TEXTEQU     <r15>       ; base address for the array of vertex properties
r_vaccum                                    TEXTEQU     <r14>       ; base address for the array of vertex accumulators
r_frontier                                  TEXTEQU     <r13>       ; pointer to the start of the graph frontier data structure
xmm_addrstash                               TEXTEQU     <xmm11>     ; general-purpose memory address stash, rather than using the stack
ymm_addrstash                               TEXTEQU     <ymm11>


; --------- PURPOSE-NAMED REGISTERS (EDGE PHASE) ------------------------------
; These are named for specific purposes as a convenience in reading the code.
; They should be used during their phase only for the specified purpose but may be reused between phases.
; Any registers that are not named like this should be considered scratch or calling convention registers.
; Some registers may have multiple names, which indicates they are used during different parts of the phase and these parts are mutually exclusive.

xmm_vid_and_mask                            TEXTEQU     <xmm10>     ; constant AND mask for generating the spread-encoding vertex ID from the edge vector
ymm_vid_and_mask                            TEXTEQU     <ymm10>
xmm_elist_and_mask                          TEXTEQU     <xmm9>      ; constant AND mask for generating the scatter/gather list from the edge vector
ymm_elist_and_mask                          TEXTEQU     <ymm9>
xmm_emask_and_mask                          TEXTEQU     <xmm8>      ; constant AND mask for generating the scatter/gather mask from the edge vector
ymm_emask_and_mask                          TEXTEQU     <ymm8>
xmm_edgevec                                 TEXTEQU     <xmm7>      ; edge vector load destination
ymm_edgevec                                 TEXTEQU     <ymm7>
xmm_elist                                   TEXTEQU     <xmm6>      ; edge source/destination list register, receives a transformed version of the edge vector
ymm_elist                                   TEXTEQU     <ymm6>
xmm_emask                                   TEXTEQU     <xmm5>      ; edge validity mask register, receives a transformed version of the edge vector
ymm_emask                                   TEXTEQU     <ymm5>
xmm_gresult                                 TEXTEQU     <xmm4>      ; gather result register, receives the results of the gather operation
ymm_gresult                                 TEXTEQU     <ymm4>
xmm_gaccum                                  TEXTEQU     <xmm3>      ; Gather: accumulator for the current destination vertex
ymm_gaccum                                  TEXTEQU     <ymm3>
xmm_smsgout                                 TEXTEQU     <xmm3>      ; Scatter: holds the outgoing message from the current source vertex
ymm_smsgout                                 TEXTEQU     <ymm3>
xmm_globaccum                               TEXTEQU     <xmm2>      ; accumulator for global variables
ymm_globaccum                               TEXTEQU     <ymm2>
r_prevvid                                   TEXTEQU     <r12>       ; holds onto the previous destination vertex ID for comparison
r_edgelist                                  TEXTEQU     <r11>       ; pointer to the gather list currently being used
r_frontiercount                             TEXTEQU     <r10>       ; number of elements in the graph frontier array (valid only in the current iteration macro)
r_vindex                                    TEXTEQU     <rbx>       ; pointer to the vertex index (valid only in the current iteration macro)


; --------- PURPOSE-NAMED REGISTERS (VERTEX PHASE) ----------------------------
; These are named for specific purposes as a convenience in reading the code.
; They should be used during their phase only for the specified purpose but may be reused between phases.
; Any registers that are not named like this should be considered scratch or calling convention registers.
; Some registers may have multiple names, which indicates they are used during different parts of the phase and these parts are mutually exclusive.

xmm_caccum1                                 TEXTEQU     <xmm10>     ; combine accumulator 1, at a displacement of 0 from the effective address
ymm_caccum1                                 TEXTEQU     <ymm10>
xmm_caccum2                                 TEXTEQU     <xmm9>      ; combine accumulator 2, at a displacement of 32 from the effective address
ymm_caccum2                                 TEXTEQU     <ymm9>
xmm_globvars                                TEXTEQU     <xmm8>      ; global variables read from the Edge phase
ymm_globvars                                TEXTEQU     <ymm8>
r_outdeglist                                TEXTEQU     <r12>       ; base address for the array of vertex outdegrees
r_woffset                                   TEXTEQU     <r11>       ; write offset, refers to the vertex identifiers that need to be written


ENDIF ;__GRAZELLE_REGISTERS_INC
