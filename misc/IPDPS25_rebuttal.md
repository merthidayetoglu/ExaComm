**Common Questions**

**Q1: Performance at large scale (Reviewers 2, 3, 4).** 
We are explicit about the limitation of our current approach beyond 256 Nodes in S6-E, and mention that HiCCL could be extended to implement latency oriented optimizations as a potential future direction. However HPC strong scaling workloads as well as ML inference leverage lower than 256 nodes would still benefit from this work. For example, as large language models become more efficient, inference production is typically done on a few nodes.

**Reviewer 1**

**Race condition in single step:**
HiCCL allows composition of collective communications in multiple steps as explained in S3-C. Each step is composed of reduction and multicast primitives, that should not write to the same output. In other words, each output element must be updated by a single primitive.

**Derived data types:**
The data type is templatized, and passed when initializing a communicator as shown in Line 3 of Listing 3. Derived data types can be passed as a structure, and the reduction operation can be extended for the derived data type. In this extent, HiCCL can be used as a drop-in replacement for traditional data types, but it would require some additional engineering when it comes to derived data types.

**Theoretical throughput:**
The theoretical throughputs in Figure 8, are based on Table III. These formulas are based on B/W = dg/t, where d is the volume of each point-to-point communication, g is the number of participating GPUs, and t is the elapsed time. Since all-to-all involves more number of point-to-point communications than other collectives, all-to-all would take the most elapsed time with the same d. Therefore all-to-all's algorithmic (theoretical) throughput maps to the lowest among all collectives.

**Reviewer 2**

**Node placement:**
All experiments are conducted in a single SLURM session, resulting in consistent node placement across scaling. Thus all experiments use the same layout between runs. Further control on node placement is challenging without the assistance of administration. We will add this to the paper.

**Reviewer 3**

**Comparison with NCCL:**
As stated shown in Figure 10(a), NCCL achieves a higher throughput for node counts larger than four. We agree with the reviewer that NCCL is faster on medium to large node counts, it is a vendor-specific solution. Whereas HiCCL manages to reach competitive performance while being portable across multiple vendors and architectures. 

**Reviewer 4**

**Integration of a new API:**
HiCCL’s is designed for easy integration of new library APIs for mixed-library implementation. The collectives are ultimately implemented with point-to-point functions, and HiCCL takes advantage of non-blocking point-to-point API of a new communication library via a simplified interface. We used that interface to integrate the existing libraries–NCCL, MPI, IPC (CUDA/HIP/OneAPI). In fact, we have recently integrated GASNet for non-MPI applications in one day of engineering effort. In the end, the user can choose whichever library they want in a particular hierarchy level as in Line 14 of Listing 2.

**Intra-node communication hierarchies:**
The key contribution of this work is the abstraction of the communication hierarchies. HiCCL takes inter-tile and inter-GPU interconnects into account as explicitly stated in the 4th sentence of 2nd paragraph of S6-C.2). The overall hierarchy is set using a vector as in Line 13 of Listing 2. In the Aurora example, the last two elements {6, 2} represent six devices (connected with XeLinks) with two tiles (connected with MDFI). In evaluation, we will include this detail and refer to Line 13 of Listing 2 for convenience of the reader.

**Message size vs. bandwidth:**
We show the throughput for various message sizes in Figure 9 for a few representative cases. Since other systems / collectives show similar curves, we omitted them from the evaluation section for brevity.

**Strong scaling:**
We did not choose the message sizes based on any specific application. We chose them for the sake of stressing the network bandwidth with large messages and to investigate if we can reach the theoretical limits. Therefore we chose large buffer sizes (8.6 GB and 17.2 GB) for strong scaling experiment. We observe that throughput-oriented optimizations break down with large number of nodes.

**Latency:**
In our scaling experiments, we keep the buffer size per GPU constant while increasing the node count. In tree configuration, the number of point-to-point communications are increased whereas the volume per communication is decreased. In strong scaling to thousands of GPUs, the work per GPU becomes so small (<MB) that the network latency becomes significant. In ring+tree configuration, each hop across nodes have a latency which adds up significantly when hundreds of nodes are involved. It is future research to find latency-oriented hierarchical compositions for improving scalability.
