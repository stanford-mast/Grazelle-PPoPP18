###############################################################################
# Grazelle
#      High performance, hardware-optimized graph processing engine.
#      Targets a single machine with one or more x86-based sockets.
###############################################################################
# Authored by Samuel Grossman
# Department of Electrical Engineering, Stanford University
# (c) 2015-2018
###############################################################################
# x-as.mk
#      Defines the commands for transforming an assembly source file's syntax
#      from MASM to AS form.
###############################################################################

X-AS    = egrep -v 'END[SP]?$$' \
        | sed 's/;/\#/g' \
        | sed 's/INCLUDE \(.*\)\$(MASM_HEADER_SUFFIX)/.include \"\1\$(ASSEMBLY_HEADER_SUFFIX)\"/' \
        | sed 's/^IF\([^ ]*\) *\(.*\)/.if\L\1 \E\2/' \
        | sed 's/^ELSE/.else/' \
        | sed 's/^ENDIF/.endif/' \
        | sed 's/^\([^ ]*\) *EQU *\(.*\)$$/.equ \1, \2/' \
        | sed 's/^\([^ ]*\) *TEXTEQU *<\([^>]*\)>/.equ \1, \2/' \
        | sed 's/^EXTRN \([^:]*\).*/.extern \1/' \
        | sed 's/ALIGN(\([0-9]*\).*/\n.align \1/' \
        | sed 's/_TEXT *SEGMENT.*/.section .text/' \
        | sed 's/DATA *SEGMENT.*/.section .data/' \
        |sed 's/CONST *SEGMENT.*/.section .rodata/' \
        | sed 's/^\([^ ]*\) *PROC PUBLIC/.globl \1\n\1:/' \
        | sed 's/^\([^ ]*\) *PROC$$/\1:/' \
        | sed 's/^PUBLIC *\([^ ]*\).*/.globl \1/' \
        | sed 's/^ *\([^ ]\+\) \+\(DB\|DW\|DD\|DQ\|REAL8\) \(.*\)/\1:\n\2 \3/' \
        | sed 's/^.*DW \+\([^Hh]*\).*/.word 0x\1/' \
        | sed 's/^.*DQ \+\([^Hh]*\).*/.8byte 0x\1/' \
        | sed 's/^.*REAL8 \+\([^Hh]*\).*/.double \1/' \
        | sed 's/\(, *\)\([a-fA-F0-9]*\)[hH]/\10x\2/' \
        | buildhelpers/x-as-macros.sh
