#!/bin/sh
###############################################################################
# Grazelle
#      High performance, hardware-optimized graph processing engine.
#      Targets a single machine with one or more x86-based sockets.
###############################################################################
# Authored by Samuel Grossman
# Department of Electrical Engineering, Stanford University
# (c) 2015-2018
###############################################################################
# x-nasm-macros.sh
#      Tranformation script for assembly macros, MASM -> NASM.
#      Input is a MASM assembly file, output is an assembly file with all the
#      macros changed to NASM format and nothing else changed.
###############################################################################

# Initialization
inmacro=0

# Go through the input, line by line, and convert over each macro found along the way
while read line; do
    modified_line=$line
    
    if [ -n "$(echo $line | egrep '^[^ ]* *MACRO')" ]; then
        # New macro found, extract information about it and transform the line to use a parameter count
        inmacro=1
        mname=$(echo $line | awk '{print $1;}')
        mparams=$(echo $line | sed 's/^.*MACRO *//' | sed 's/,//g')
        mparamcount=$(echo $mparams | wc -w)
        modified_line="%macro $mname $mparamcount"
    else
        if [ -n "$(echo $line | grep 'ENDM')" ]; then
            # Ending a macro
            inmacro=0
            mname=""
            mparams=""
            mparamcount=""
            modified_line="%endmacro"
        else
            if [ $inmacro -ne 0 ]; then
                # In the middle of a macro, so do paramater -> number translation
                idx=0
                for mparam in $mparams; do
                    idx=$(( $idx + 1 ))
                    modified_line=$(echo $modified_line | sed "s/$mparam/%$idx/g")
                done
            fi
        fi
    fi
    
    echo $modified_line
done
