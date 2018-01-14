# Grazelle

Grazelle is a high-performance hybrid (push-based and pull-based) graph processing framework targeting a single machine containing or more x86-64-based processors.  It is the embodiment of the two optimization strategies described in the PPoPP 2018 paper *Making Pull-Based Graph Processing Performant*.

The content of this repository is intended to support the results presented in the aforementioned paper.  It contains all the source code and documentation required to build and run Grazelle as configured for each experiment presented in the paper as well as instructions on how to obtain the datasets used as input.


# Requirements

Grazelle requires an x86-64-based CPU with support for AVX2 instructions, such as Intel processors of the Haswell generation or later.  NUMA scaling experiments require multiple CPU sockets.  We recommend 256GB DRAM per socket.

Grazelle is intended to run on Ubuntu 14.04 or later.  Runtime dependencies include glibc, libnuma (package "libnuma-dev"), and pthreads.  Build dependencies include make, gcc 4.8.4 or later, and either as 2.24 or [nasm](http://www.nasm.us) 2.10.09 or later.  The build system is configured to use as by default, but if this does not work for some reason, install nasm to a directory covered by the `PATH` environment variable and switch the "AS" variable to "nasm" in the Makefile.


# Building

Grazelle can be built either pre-configured for the purpose of reproducing a figure presented in the paper or using a set of custom options for enabling or disabling many of the optimizations we tried.  The algorithm that Grazelle runs is fixed at compile-time.


## Paper Experiment Builds

To build Grazelle for the purpose of reproducing any of the figures in the paper, use one of the pre-configured Makefile targets.  Each one supplies the needed pre-processor options to construct a version of Grazelle intended to run a particular experiment.  Specifically, the following Makefile targets are supplied for this purpose.

 - `fig567-trad`, `fig567-tradna`, `fig567-sa`: Configures Grazelle to run the PageRank Scheduler Awareness experiments (Figures 5, 6, and 7).  Suffixes "-trad", "-tradna", and "-sa" produce traditional, traditional-nonatomic, and scheduler-aware versions respectively.

 - `fig8a-trad`, `fig8a-tradna`, `fig8a-sa`, `fig8b-trad`, `fig8b-tradna`, `fig8b-sa`: Configures Grazelle to run the Connected Components Scheduler Awareness experiments (Figure 8). "8a" targets produce write-intense versions, "8b" targets produce standard versions, and suffixes "-trad", "-tradna", and "-sa" produce traditional, traditional-nonatomic, and scheduler-aware versions respectively.

 - `fig9`: Configures Grazelle to output vector packing efficiency results for vectors of length 4, 8, and 16.  The results of this experiment are shown in Figure 9.

 - `fig10a-edgepull-base`, `fig10a-edgepull-vec`, `fig10a-edgepush-base`, `fig10a-edgepush-vec`, `fig10a-vertex-base`, `fig10a-vertex-vec`: Configures Grazelle to run the per-phase vectorization performance tests (Figure 10a); "edgepull", "edgepush", and "vertex" respectively identify the phase of execution, and "base" and "vec" respectively identify the baseline and vectorized implementations.

 - `fig10b-pr-base`, `fig10b-pr-vec`, `fig10b-cc-base`, `fig10b-cc-vec`, `fig10b-bfs-base`, `fig10b-bfs-vec`: Configures Grazelle to run the end-to-end application vectorization performance tests (Figure 10b); "pr", "cc", and "bfs" identify the application, and "base" and "vec" respectively identify the baseline and vectorized implementations.

 - `fig11-pull`, `fig11-push`, `fig12`, `fig13`: Configures Grazelle for performance comparisons with other frameworks (Figures 11, 12, and 13).

For example, to build Grazelle so that it runs Breadth-First Search in a mode optimized for comparison with other frameworks:

    make fig13


## Custom Builds

To create a custom build of Grazelle, note that the Makefile-based build system includes built-in documentation on how to use it.  To access this documentation:

    make help
    
Customizable options include both algorithm selection and experiment flags.  The latter can be used to enable or disable various optimizations we tried, including some not covered in the paper.  Please be aware that we have not exhaustively tested all possible combinations of these flags, so we cannot guarantee that every combination will produce a functionally-correct (or even functioning) executable.


# Datasets

Grazelle supports graphs represented using a binary edge list format.  Each data element in such a file is a 64-bit unsigned integer, and the files are laid out as follows.

1. A 64-bit unsigned integer indicating the number of vertices in the graph.

1. A 64-bit unsigned integer indicating the number of edges in the graph.

1. For each edge in the graph, a 64-bit unsigned integer identifying the source vertex followed by a second 64-bit unsigned integer identifying the destination vertex.

Each vertex is identified by a 64-bit unsigned integer (of which only the lower 48 bits are used, per the paper) ranging from 0 to one less than the number of vertices in the graph.

Grazelle expects that each graph will be represented using two binary edge lists.  The first, used by Grazelle's pull engine, contains edges grouped by destination vertex such that the destination vertices appear in ascending order.  The second, used by Grazelle's push engine, contains edges grouped by source vertex such that the source vertices appear in ascending order.  Both files must be placed in the same directory and have the same filename, with the exception that the former should have a suffix of "-pull" appended to it and the latter a suffix of "-push".


## Paper Experiment Datasets

Unfortunately, datasets are far too large to be hosted on GitHub.  We are therefore supplying pre-converted ready-to-use versions of each of the graphs from the paper in the following Google Drive folder.

https://drive.google.com/open?id=1wMvyikOTJTvZCHwPJzSl65AU8st1DasO


## Custom Datasets

We are developing a tool to aid in the conversion of graphs from one format to another, including Grazelle's binary edge list format.  We will update this section once said tool is released.


# Running

Simply type Grazelle's executable path and supply the required command-line options.  Assuming the current directory is the directory from which `make` was launched, the executable would normally be located at `output/linux/grazelle`.  To get help:

    output/linux/grazelle -h

The only required command-line option is `-i`, which is used to specify the location of the input graph.  Note that the "-push" and "-pull" suffixes should be omitted from this command-line option; Grazelle adds these suffixes automatically when attempting to read the input graph.

Other common command-line options are listed below.

 - `-u [numa-nodes]`: Comma-delimited list of NUMA nodes Grazelle should use to run the graph application.  For example, `-u 0,2` specifies that nodes 0 and 2 should be used.  By default only the first node in the system is used.

 - `-n [num-threads]`: Total number of threads that should be used for running the graph application.  By default Grazelle uses all available threads on the configured NUMA node(s).

 - `-N [num-iterations]`: Number of iterations of PageRank to run (ignored for the other applications).  Defaults to 1.

 - `-s [sched-granularity]`: Scheduling granularity to use, expressed as number of edge vectors per unit of work.  Default behavior is to create 32*N* units of work, where *N* is the number of threads.

 - `-o [output-file]`: If specified, causes Grazelle to write output produced by the running application to the specified file. For PageRank this is the final rank of each vertex, for Connected Components this is the component identifier of each vertex, and for Breadth-First Search this is the parent of each vertex.

When running PageRank, we suggest executing a sufficient number of iterations to get steady-state behavior while also not causing the experiment to take an unnecessarily long time to run.  We suggest the following iterations counts.

| Graph          | fig10a-vertex-* | All Others |
| -------------- | --------------: | ---------: |
| cit-Patents    | 1024            | 1024       |
| dimacs-usa     | 256             | 256        |
| livejournal    | 1024            | 256        |
| twitter-2010   | 64              | 16         |
| friendster     | 64              | 16         |
| uk-2007        | 32              | 16         |


## Comparing with Other Frameworks

Reproducing Figures 11, 12, and 13 requires comparing performance results obtained by running Grazelle with those obtained by running other frameworks.  Resources to aid in carrying out this comparison, including instructions and all input datasets encoded using the format expected by each other framework, are available in the following Google Drive folders.

- GraphMat:  https://drive.google.com/open?id=13GIAbcxdoB59mP1lal3CgreqLKaM0oDS
- Ligra and Polymer: https://drive.google.com/open?id=1Lwyon9cAM8V8UwfO8kcIakzBD6qX2H6L


## Notes

Each invocation of the Grazelle executable produces a single data point.  Reproducing figures generally requires data points obtained from multiple invocations that are then compared.  For example, replicating Figure 10 requires comparing corresponding data points obtained using baseline and vectorized configurations, and replicating Figure 7 requires sweeping the `-n` command-line parameter.

We used the "perf" tool to generate Figure 5b.  We are not able to supply a script to automate the process of generating that graph, as it involved manually looking at traces to capture time percentages spent in specific functions.
