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
#      from MASM to NASM form.
###############################################################################

X-AS    = egrep -v 'END[SP]?$$' \
        | sed 's/INCLUDE \(.*\)\$(MASM_HEADER_SUFFIX)/%include \"\1\$(ASSEMBLY_HEADER_SUFFIX)\"/' \
        | sed 's/^IF\([^ ]*\) *\(.*\)/%if\L\1 \E\2/' \
        | sed 's/^ELSE/%else/' \
        | sed 's/^ENDIF/%endif/' \
        | sed 's/^\([^ ]*\) *EQU *\(.*\)$$/%define \1 \2/' \
        | sed 's/^\([^ ]*\) *TEXTEQU *<\([^>]*\)>/%define \1 \2/' \
        | sed 's/^EXTRN \([^:]*\).*/extern \1/' \
        | sed 's/ALIGN(\([0-9]*\).*/\nalign \1/' \
        | sed 's/_TEXT *SEGMENT.*/section .text/' \
        | sed 's/DATA *SEGMENT.*/section .data/' \
        | sed 's/CONST *SEGMENT.*/section .rodata/' \
        | sed 's/^\([^ ]*\) *PROC PUBLIC/global \1\n\1:/' \
        | sed 's/^\([^ ]*\) *PROC$$/\1:/' \
        | sed 's/^PUBLIC *\([^ ]*\).*/global \1/' \
        | sed 's/^ *\([^ ]\+\) \+\(DB\|DW\|DD\|DQ\|REAL8\) \(.*\)/\1:\n\2 \3/' \
        | sed 's/^.*DW \+\([^Hh]*\).*/dw \1h/' \
        | sed 's/^.*DQ \+\([^Hh]*\).*/dq \1h/' \
        | sed 's/^.*REAL8 \+\([^Hh]*\).*/dq \1/' \
        | sed 's/ PTR / /' \
        | sed 's/XMMWORD/OWORD/' \
        | sed 's/YMMWORD/YWORD/' \
        | buildhelpers/x-nasm-macros.sh
