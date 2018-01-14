;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Grazelle
;      High performance, hardware-optimized graph processing engine.
;      Targets a single machine with one or more x86-based sockets.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Authored by Samuel Grossman
; Department of Electrical Engineering, Stanford University
; (c) 2015-2018
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; graphdata.inc
;      Declarations of data structures used to represent a graph in memory.
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

IFNDEF __GRAZELLE_GRAPHDATA_INC
__GRAZELLE_GRAPHDATA_INC EQU 1


; --------- DATA STRUCTURES ---------------------------------------------------
; See "graphdata.h" for documentation.

EXTRN graph_num_vertices:QWORD
EXTRN graph_num_edges:QWORD
EXTRN graph_vertex_props:QWORD
EXTRN graph_vertex_outdegrees:QWORD
EXTRN graph_frontier_has_info:QWORD;
EXTRN graph_frontier_wants_info:QWORD;
EXTRN graph_edges_gather_list_vector_count:QWORD;
EXTRN graph_edges_scatter_list_vector_count:QWORD;
EXTRN graph_vertex_accumulators:QWORD
EXTRN graph_edges_gather_list_num_blocks:QWORD
EXTRN graph_edges_gather_list_block_first_dest_vertex:QWORD
EXTRN graph_edges_gather_list_block_last_dest_vertex:QWORD
EXTRN graph_vertex_merge_buffer:QWORD
EXTRN graph_vertex_merge_buffer_baseptr_numa:QWORD
EXTRN graph_edges_gather_list_block_bufs_numa:QWORD
EXTRN graph_edges_gather_list_block_counts_numa:QWORD
EXTRN graph_vertex_first_numa:QWORD
EXTRN graph_vertex_last_numa:QWORD
EXTRN graph_vertex_count_numa:QWORD
EXTRN graph_vertex_gather_index_numa:QWORD
EXTRN graph_vertex_gather_index_start_numa:QWORD
EXTRN graph_vertex_gather_index_end_numa:QWORD
EXTRN graph_vertex_scatter_index_numa:QWORD
EXTRN graph_vertex_scatter_index_start_numa:QWORD
EXTRN graph_vertex_scatter_index_end_numa:QWORD
EXTRN graph_scheduler_dynamic_counter_numa:QWORD
EXTRN graph_stat_num_vectors_per_thread:QWORD
EXTRN graph_stat_num_edges_per_thread:QWORD
EXTRN graph_stat_num_vectors_per_iteration:QWORD
EXTRN graph_stat_num_edges_per_iteration:QWORD


ENDIF ;__GRAZELLE_GRAPHDATA_INC
