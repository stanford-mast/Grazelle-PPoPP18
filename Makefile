###############################################################################
# Grazelle
#      High performance, hardware-optimized graph processing engine.
#      Targets a single machine with one or more x86-based sockets.
###############################################################################
# Authored by Samuel Grossman
# Department of Electrical Engineering, Stanford University
# (c) 2015-2018
###############################################################################
# Makefile
#      Build script for GNU-compatible Linux operating systems.
###############################################################################


# --------- PROJECT PROPERTIES ------------------------------------------------

PROJECT_NAME                = grazelle
PLATFORM_NAME               = linux

LIBRARY_DEPENDENCIES        = librt libpthread libnuma

SOURCE_DIR                  = source
INCLUDE_DIR                 = include
OUTPUT_DIR                  = output/$(PLATFORM_NAME)
ASSEMBLY_SOURCE_DIR         = $(OUTPUT_DIR)/asm/source
ASSEMBLY_INCLUDE_DIR        = $(OUTPUT_DIR)/asm/include

C_SOURCE_SUFFIX             = .c
CXX_SOURCE_SUFFIX           = .cpp

MASM_SOURCE_SUFFIX          = .asm
MASM_HEADER_SUFFIX          = .inc
ASSEMBLY_SOURCE_SUFFIX      = .s
ASSEMBLY_HEADER_SUFFIX      = .S


# --------- TOOL SELECTION AND CONFIGURATION ----------------------------------

CC                          = gcc
CXX                         = g++
AS                          = as
LD                          = g++

CCFLAGS                     = -g -O3 -Wall -std=c11 -march=core-avx2 -masm=intel -mno-vzeroupper -pthread -I$(INCLUDE_DIR) -D_GNU_SOURCE
CXXFLAGS                    = -g -O3 -Wall -std=c++0x -march=core-avx2 -masm=intel -mno-vzeroupper -pthread -I$(INCLUDE_DIR)
LDFLAGS                     = -g

ifeq 'as' '$(AS)'
ASFLAGS                     = --64 -mmnemonic=intel -msyntax=intel -mnaked-reg -I$(ASSEMBLY_INCLUDE_DIR)
else
ifeq 'nasm' '$(AS)'
ASFLAGS                     = -f elf64 -i$(ASSEMBLY_INCLUDE_DIR)/
else
$(error "Your assembler is not supported. Use either `as' or `nasm'.")
endif
endif


# --------- EXPERIMENTS -------------------------------------------------------

ifeq 'HELP' '$(EXPERIMENTS)'

experimenthelp:
	@echo ''
	@echo 'Software Experiments:'
	@echo '    EDGE_ONLY'
	@echo '        Run only the Edge phase. Useful for per-phase benchmarks.'
	@echo '        For use only with algorithms that do not dynamically converge.'
	@echo '    VERTEX_ONLY'
	@echo '        Run only the Vertex phase. Useful for per-phase benchmarks.'
	@echo '        For use only with algorithms that do not dynamically converge.'
	@echo '    EDGE_ONLY VERTEX_ONLY'
	@echo '        Run only the control-flow constructs (neither phase).'
	@echo '        Useful for per-phase benchmarks to isolate the time taken by each.'
	@echo '        For use only with algorithms that do not dynamically converge.'
	@echo '    THRESHOLD_WITHOUT_OUTDEGREES'
	@echo '        Causes frontier outdegrees not to be considered when choosing an engine.'
	@echo '        By default, both count and outdegrees are considered.'
	@echo '    THRESHOLD_WITHOUT_COUNT'
	@echo '        Causes frontier vertex count not to be considered when choosing an engine.'
	@echo '        By default, both count and outdegrees are considered.'
	@echo '    EDGE_FORCE_PULL'
	@echo '        Forces the Edge phase to use only the pull-based engine.'
	@echo '    EDGE_FORCE_PUSH'
	@echo '        Forces the Edge phase to use only the push-based engine.'
	@echo '    EDGE_PULL_WITHOUT_SCHED_AWARE'
	@echo '        Disables scheduler awareness in the pull-based engine.'
	@echo '        Causes writes to happen immediately and atomically.'
	@echo '    EDGE_PULL_WITHOUT_SYNC'
	@echo '        Disables atomicity in the pull-based engine.'
	@echo '        Useful as a performance measurement, but impacts correctness.'
	@echo '    EDGE_PULL_FORCE_MERGE'
	@echo '        Forces the pull-based engine to use merging behavior.'
	@echo '        Meaningful only when scheduler awareness is enabled.'
	@echo '    EDGE_PULL_FORCE_WRITE'
	@echo '        Increase the write intensity of low-intensity applications.'
	@echo '        Impacts performance, not correctness.'
	@echo '    EDGE_PUSH_WITHOUT_SYNC'
	@echo '        Disables write synchronization when using the push-based engine.'
	@echo '        May negatively affect correctness, depending on the algorithm.'
	@echo '    EDGE_PUSH_WITH_HTM'
	@echo '        Uses hardware transactional memory to synchronize writes.'
	@echo '        Default behavior is to use atomics for this purpose.'
	@echo '        Does not impact correctness, but does impact performance.'
	@echo '    EDGE_PUSH_HTM_SINGLE'
	@echo '        Synchronize using a single coarse-grained transaction when using HTM.'
	@echo '        Default HTM behavior is to use multiple fine-grained transactions.'
	@echo '        Does not impact correctness, but does impact performance.'
	@echo '    EDGE_PUSH_HTM_ATOMIC_FALLBACK'
	@echo '        Use atomic synchronization if an HTM transaction fails to commit.'
	@echo '        Only effective when using single coarse-grained transactions.'
	@echo '        Does not impact correctness, but does impact performance.'
	@echo '    MODEL_LONG_VECTORS'
	@echo '        Models the effect of lengthening the vector length from 4 to 8 and 16.'
	@echo '        Causes the ingress code to output packing efficiency for those lengths.'
	@echo '    WITHOUT_PREFETCH'
	@echo '        Disables software prefetching hints.'
	@echo '    WITHOUT_VECTORS'
	@echo '        Uses alternate engine implementations with limited vectorization.'
	@echo '        It is not possible to disable vectorization completely, however.'
	@echo '    ASSIGN_VERTICES_BY_PUSH'
	@echo '        For NUMA experiments, alters how vertices are assigned to nodes.'
	@echo '        When enabled, assigns using boundaries in the Edge-Push edge list.'
	@echo '        Default is to use boundaries in the Edge-Pull edge list.'
	@echo '    ITERATION_PROFILE'
	@echo '        Print per-iteration frontier details for the Edge phase.'
	@echo '        Includes engine selection, execution time, and frontier size.'
	@echo '        Invalidates overall execution time and performance results.'
	@echo '        Only effective with algorithms that use frontiers.'
	@echo '        Output is printed to stderr in CSV format.'
	@echo '    ITERATION_STATS'
	@echo '        Print per-iteration statistics for the Edge phase.'
	@echo '        Includes total number of edge vectors scanned and packing efficiency.'
	@echo '        Invalidates overall execution time and performance results.'
	@echo '        Only effective with algorithms that use frontiers.'
	@echo '        Output is printed to stderr in CSV format.'
	@echo ''
	@echo 'Frontier Detection:'
	@echo '    FRONTIERS_WEAK_PULL'
	@echo '        Enables frontier detection in the Pull engine.'
	@echo '        Only applicable for applications that optionally use the frontier.'
	@echo '    FRONTIERS_NOSTRONG_PUSH'
	@echo '        Disables frontier detection in the Push engine.'
	@echo '        Only applicable for applications that optionally use the frontier.'
	@echo '    FRONTIERS_WITHOUT_ASYNC'
	@echo '        Disables asynchronous (immediate) updates to frontiers during processing.'
	@echo '        Enabled by default and can reduce the number of iterations to convergence.'
	@echo '        This optimization does not apply to all applications.'
	@echo ''

else

SUPPORTED_EXPERIMENTS       = EDGE_ONLY VERTEX_ONLY THRESHOLD_WITHOUT_OUTDEGREES THRESHOLD_WITHOUT_COUNT EDGE_FORCE_PULL EDGE_FORCE_PUSH EDGE_PULL_WITHOUT_SCHED_AWARE EDGE_PULL_WITHOUT_SYNC EDGE_PULL_FORCE_MERGE EDGE_PULL_FORCE_WRITE EDGE_PUSH_WITHOUT_SYNC EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK MODEL_LONG_VECTORS WITHOUT_PREFETCH WITHOUT_VECTORS ASSIGN_VERTICES_BY_PUSH ITERATION_PROFILE ITERATION_STATS FRONTIERS_WEAK_PULL FRONTIERS_NOSTRONG_PUSH FRONTIERS_WITHOUT_ASYNC
UNSUPPORTED_EXPERIMENTS     = $(filter-out $(SUPPORTED_EXPERIMENTS), $(EXPERIMENTS))

ifneq ($(strip $(UNSUPPORTED_EXPERIMENTS)),)
$(error Invalid experiment(s): $(UNSUPPORTED_EXPERIMENTS).  Try 'EXPERIMENTS=HELP' for more information)
endif

ifneq ($(strip $(EXPERIMENTS)),)

CCFLAGS                     += -DEXPERIMENT_STR="\"$(EXPERIMENTS)\""
CXXFLAGS                    += -DEXPERIMENT_STR="\"$(EXPERIMENTS)\""

CCFLAGS                     += $(foreach EXPERIMENT, $(EXPERIMENTS), -DEXPERIMENT_$(EXPERIMENT))
CXXFLAGS                    += $(foreach EXPERIMENT, $(EXPERIMENTS), -DEXPERIMENT_$(EXPERIMENT))
ifeq 'as' '$(AS)'
ASFLAGS                     += $(foreach EXPERIMENT, $(EXPERIMENTS), --defsym EXPERIMENT_$(EXPERIMENT)=1)
endif
ifeq 'nasm' '$(AS)'
ASFLAGS                     += $(foreach EXPERIMENT, $(EXPERIMENTS), -DEXPERIMENT_$(EXPERIMENT))
endif

endif

endif


# --------- ALGORITHM SELECTION -----------------------------------------------

ifdef ALGORITHM

ifeq 'HELP' '$(ALGORITHM)'

algohelp:
	@echo ''
	@echo 'Algorithms:'
	@echo '    PAGERANK'
	@echo '        Runs PageRank.'
	@echo '        This is the default when no algorithm is specified.'
	@echo '        Converges after a statically-specified number of iterations.'
	@echo '    CONNECTED_COMPONENTS'
	@echo '        Runs Connected Components instead of PageRank.'
	@echo '        This algorithm uses dynamic convergence.'
	@echo '    BREADTH_FIRST_SEARCH'
	@echo '        Runs Breadth-First Search instead of PageRank.'
	@echo '        This algorithm uses dynamic convergence.'
	@echo ''

else

SUPPORTED_ALGORITHMS        = PAGERANK CONNECTED_COMPONENTS BREADTH_FIRST_SEARCH
UNSUPPORTED_ALGORITHMS      = $(filter-out $(SUPPORTED_ALGORITHMS), $(ALGORITHM))

ifneq ($(words $(ALGORITHM)),1)
$(error Specify only a single algorithm.  Try 'ALGORITHM=HELP' for more information)
endif

ifneq ($(strip $(UNSUPPORTED_ALGORITHMS)),)
$(error Invalid algorithm: $(UNSUPPORTED_ALGORITHMS).  Try 'ALGORITHM=HELP' for more information)
endif

CCFLAGS                     += -D$(ALGORITHM)
CXXFLAGS                    += -D$(ALGORITHM)
ifeq 'as' '$(AS)'
ASFLAGS                     += --defsym $(ALGORITHM)=1
endif
ifeq 'nasm' '$(AS)'
ASFLAGS                     += -D$(ALGORITHM)
endif
endif

endif


# --------- FILE ENUMERATION --------------------------------------------------

OBJECT_FILE_SUFFIX          = .o
DEP_FILE_SUFFIX             = .d

C_SOURCE_FILES              = $(wildcard $(SOURCE_DIR)/*$(C_SOURCE_SUFFIX))
CXX_SOURCE_FILES            = $(wildcard $(SOURCE_DIR)/*$(CXX_SOURCE_SUFFIX))
ALL_SOURCE_FILES            = $(C_SOURCE_FILES) $(CXX_SOURCE_FILES)
MASM_SOURCE_FILES           = $(wildcard $(SOURCE_DIR)/*$(MASM_SOURCE_SUFFIX))
MASM_HEADER_FILES           = $(wildcard $(INCLUDE_DIR)/*$(MASM_HEADER_SUFFIX))
ASSEMBLY_SOURCE_FILES       = $(patsubst $(SOURCE_DIR)/%$(MASM_SOURCE_SUFFIX), $(ASSEMBLY_SOURCE_DIR)/%$(ASSEMBLY_SOURCE_SUFFIX), $(MASM_SOURCE_FILES))
ASSEMBLY_HEADER_FILES       = $(patsubst $(INCLUDE_DIR)/%$(MASM_HEADER_SUFFIX), $(ASSEMBLY_INCLUDE_DIR)/%$(ASSEMBLY_HEADER_SUFFIX), $(MASM_HEADER_FILES))
OBJECT_FILES_FROM_SOURCE    = $(patsubst $(SOURCE_DIR)/%, $(OUTPUT_DIR)/%$(OBJECT_FILE_SUFFIX), $(ALL_SOURCE_FILES))
OBJECT_FILES_FROM_ASSEMBLY  = $(patsubst $(ASSEMBLY_SOURCE_DIR)/%, $(OUTPUT_DIR)/%$(OBJECT_FILE_SUFFIX), $(ASSEMBLY_SOURCE_FILES))
DEP_FILES_FROM_SOURCE       = $(patsubst $(SOURCE_DIR)/%, $(OUTPUT_DIR)/%$(DEP_FILE_SUFFIX), $(ALL_SOURCE_FILES))
LINK_LIBRARIES              = $(patsubst lib%, -l%, $(LIBRARY_DEPENDENCIES))


# --------- TOP-LEVEL RULE CONFIGURATION --------------------------------------

.PHONY: grazelle help clean

.SECONDARY: $(ASSEMBLY_SOURCE_FILES) $(ASSEMBLY_HEADER_FILES)


# --------- TARGET DEFINITIONS ------------------------------------------------

grazelle: $(OUTPUT_DIR)/$(PROJECT_NAME)

help:
	@echo ''
	@echo 'Usage: make [target] [ALGORITHM=algorithm] [EXPERIMENTS=experiments]'
	@echo ''
	@echo 'Targets:'
	@echo '    grazelle'
	@echo '        Default target. Builds Grazelle.'
	@echo '    help'
	@echo '        Shows this information.'
	@echo ''
	@echo 'Variables:'
	@echo '    ALGORITHM'
	@echo '        Selects the algorithm that Grazelle should be built to run.'
	@echo '        Defaults to PageRank, which does not use dynamic convergence.'
	@echo '        Type `make ALGORITHM=HELP'\'' for more information.'
	@echo '    EXPERIMENTS'
	@echo '        Selects experiments to run or experimental optimizations to enable.'
	@echo '        Defaults to no experiments or experimental optimizations.'
	@echo '        Type `make EXPERIMENTS=HELP'\'' for more information.'
	@echo ''


# --------- FOR PPOPP 2018 ----------------------------------------------------

fig567-trad: clean
	make -j 8 EXPERIMENTS="EDGE_FORCE_PULL EDGE_PULL_WITHOUT_SCHED_AWARE" ALGORITHM=""

fig567-tradna: clean
	make -j 8 EXPERIMENTS="EDGE_FORCE_PULL EDGE_PULL_WITHOUT_SCHED_AWARE EDGE_PULL_WITHOUT_SYNC" ALGORITHM=""

fig567-sa: clean
	make -j 8 EXPERIMENTS="EDGE_FORCE_PULL" ALGORITHM=""

fig8a-trad: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_WRITE EDGE_PULL_WITHOUT_SCHED_AWARE" ALGORITHM="CONNECTED_COMPONENTS"

fig8a-tradna: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_WRITE EDGE_PULL_WITHOUT_SCHED_AWARE EDGE_PULL_WITHOUT_SYNC" ALGORITHM="CONNECTED_COMPONENTS"

fig8a-sa: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_WRITE EDGE_PULL_FORCE_MERGE" ALGORITHM="CONNECTED_COMPONENTS"

fig8b-trad: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_WITHOUT_SCHED_AWARE" ALGORITHM="CONNECTED_COMPONENTS"

fig8b-tradna: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_WITHOUT_SCHED_AWARE EDGE_PULL_WITHOUT_SYNC" ALGORITHM="CONNECTED_COMPONENTS"

fig8b-sa: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_MERGE" ALGORITHM="CONNECTED_COMPONENTS"

fig9: clean
	make -j 8 EXPERIMENTS="MODEL_LONG_VECTORS" ALGORITHM=""

fig10a-edgepull-base: clean
	make -j 8 EXPERIMENTS="EDGE_ONLY EDGE_FORCE_PULL WITHOUT_VECTORS" ALGORITHM=""

fig10a-edgepull-vec: clean
	make -j 8 EXPERIMENTS="EDGE_ONLY EDGE_FORCE_PULL" ALGORITHM=""

fig10a-edgepush-base: clean
	make -j 8 EXPERIMENTS="EDGE_ONLY EDGE_FORCE_PUSH EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK WITHOUT_VECTORS" ALGORITHM=""

fig10a-edgepush-vec: clean
	make -j 8 EXPERIMENTS="EDGE_ONLY EDGE_FORCE_PUSH EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK" ALGORITHM=""

fig10a-vertex-base: clean
	make -j 8 EXPERIMENTS="VERTEX_ONLY WITHOUT_VECTORS" ALGORITHM=""

fig10a-vertex-vec: clean
	make -j 8 EXPERIMENTS="VERTEX_ONLY" ALGORITHM=""

fig10b-pr-base: clean
	make -j 8 EXPERIMENTS="WITHOUT_VECTORS" ALGORITHM=""

fig10b-pr-vec: clean
	make -j 8 EXPERIMENTS="" ALGORITHM=""

fig10b-cc-base: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_MERGE WITHOUT_VECTORS" ALGORITHM="CONNECTED_COMPONENTS"

fig10b-cc-vec: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_MERGE" ALGORITHM="CONNECTED_COMPONENTS"

fig10b-bfs-base: clean
	make -j 8 EXPERIMENTS="THRESHOLD_WITHOUT_COUNT WITHOUT_VECTORS" ALGORITHM="BREADTH_FIRST_SEARCH"

fig10b-bfs-vec: clean
	make -j 8 EXPERIMENTS="THRESHOLD_WITHOUT_COUNT" ALGORITHM="BREADTH_FIRST_SEARCH"

fig11-pull: clean
	make -j 8 EXPERIMENTS="EDGE_FORCE_PULL" ALGORITHM=""

fig11-push: clean
	make -j 8 EXPERIMENTS="EDGE_FORCE_PUSH EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK" ALGORITHM=""

fig12: clean
	make -j 8 EXPERIMENTS="EDGE_PUSH_WITH_HTM EDGE_PUSH_HTM_SINGLE EDGE_PUSH_HTM_ATOMIC_FALLBACK THRESHOLD_WITHOUT_COUNT EDGE_PULL_FORCE_MERGE" ALGORITHM="CONNECTED_COMPONENTS"

fig13: clean
	make -j 8 EXPERIMENTS="THRESHOLD_WITHOUT_COUNT" ALGORITHM="BREADTH_FIRST_SEARCH"


# --------- BUILDING AND CLEANING RULES ---------------------------------------

$(OUTPUT_DIR)/$(PROJECT_NAME): $(OBJECT_FILES_FROM_SOURCE) $(OBJECT_FILES_FROM_ASSEMBLY)
	@echo '   LD        $@'
	@$(LD) $(LDFLAGS) -o $@ $(OBJECT_FILES_FROM_SOURCE) $(OBJECT_FILES_FROM_ASSEMBLY) $(LINK_LIBRARIES) $(LDEXTRAFLAGS)
	@echo 'Build completed: $(PROJECT_NAME).'

clean:
	@echo '   RM        $(OUTPUT_DIR)'
	@rm -rf $(OUTPUT_DIR)
	@echo 'Clean completed: $(PROJECT_NAME).'


# --------- COMPILING AND ASSEMBLING RULES ------------------------------------

$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

$(OUTPUT_DIR)/%$(ASSEMBLY_SOURCE_SUFFIX)$(OBJECT_FILE_SUFFIX): $(ASSEMBLY_SOURCE_DIR)/%$(ASSEMBLY_SOURCE_SUFFIX) $(ASSEMBLY_HEADER_FILES) | $(OUTPUT_DIR)
	@echo '   AS        $@'
	@$(AS) $(ASFLAGS) $< -o $@

$(OUTPUT_DIR)/%$(C_SOURCE_SUFFIX)$(OBJECT_FILE_SUFFIX): $(SOURCE_DIR)/%$(C_SOURCE_SUFFIX) | $(OUTPUT_DIR)
	@echo '   CC        $@'
	@$(CC) $(CCFLAGS) -MD -MP -c -o $@ -Wa,-adhlms=$(patsubst %$(OBJECT_FILE_SUFFIX),%$(ASSEMBLY_SOURCE_SUFFIX),$@) $<

$(OUTPUT_DIR)/%$(CXX_SOURCE_SUFFIX)$(OBJECT_FILE_SUFFIX): $(SOURCE_DIR)/%$(CXX_SOURCE_SUFFIX) | $(OUTPUT_DIR)
	@echo '   CXX       $@'
	@$(CXX) $(CXXFLAGS) -MD -MP -c -o $@ -Wa,-adhlms=$(patsubst %$(OBJECT_FILE_SUFFIX),%$(ASSEMBLY_SOURCE_SUFFIX),$@) $<

-include $(DEP_FILES_FROM_SOURCE)


# --------- ASSEMBLY SOURCE FILE TRANSFORMATION RULES -------------------------

$(ASSEMBLY_SOURCE_DIR):
	@mkdir -p $(ASSEMBLY_SOURCE_DIR)

$(ASSEMBLY_INCLUDE_DIR):
	@mkdir -p $(ASSEMBLY_INCLUDE_DIR)

$(ASSEMBLY_SOURCE_DIR)/%$(ASSEMBLY_SOURCE_SUFFIX): $(SOURCE_DIR)/%$(MASM_SOURCE_SUFFIX) | $(ASSEMBLY_SOURCE_DIR)
	@echo '   X-AS      $@'
	@cat $< | $(X-AS) > $@

$(ASSEMBLY_INCLUDE_DIR)/%$(ASSEMBLY_HEADER_SUFFIX): $(INCLUDE_DIR)/%$(MASM_HEADER_SUFFIX) | $(ASSEMBLY_INCLUDE_DIR)
	@echo '   X-AS      $@'
	@cat $< | $(X-AS) > $@

-include buildhelpers/x-$(AS).mk
