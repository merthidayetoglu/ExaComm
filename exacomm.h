/* Copyright 2023 Stanford University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <vector>

namespace ExaComm {

  template <typename T>
  struct P2P {
    public:
    T* const sendbuf;
    const size_t sendoffset;
    T* const recvbuf;
    size_t const recvoffset;
    size_t const count;
    int const sendid;
    int const recvid;

    P2P(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, int recvid)
    : sendbuf(sendbuf), sendoffset(sendoffset), recvbuf(recvbuf), recvoffset(recvoffset), count(count), sendid(sendid), recvid(recvid) {};
  };

  template <typename T>
  class Comm {

    const MPI_Comm &comm_mpi;

    std::vector<P2P<T>> addlist;
    std::vector<CommBench::Comm<T>> comm_intra;
    std::vector<CommBench::Comm<T>> comm_inter;
    std::vector<CommBench::Comm<T>> comm_split;
    std::vector<CommBench::Comm<T>> comm_merge;

    std::vector<T*> sendbuf_inter;
    std::vector<T*> recvbuf_inter;

    void start() {
      for(auto &comm : comm_split)
        comm.run();
      for(auto &comm : comm_inter)
        comm.launch();
      for(auto &comm : comm_intra)
        comm.launch();
    }
    void wait() {
      for(auto &comm : comm_intra)
        comm.wait();
      for(auto &comm : comm_inter)
        comm.wait();
      for(auto &comm : comm_merge)
        comm.run();
    }

    void init_striped(int numlevel, int groupsize[], CommBench::library lib[]) {

      int myid;
      int numproc;
      MPI_Comm_rank(comm_mpi, &myid);
      MPI_Comm_size(comm_mpi, &numproc);

      for(int level = 0; level < numlevel; level++)
        comm_intra.push_back(CommBench::Comm<T>(comm_mpi, lib[level]));

      std::vector<P2P<T>> addlist_inter;

      for(auto &p2p : addlist) {
        bool found = false;
        for(int level = numlevel - 1; level > -1; level--)
          if(p2p.sendid / groupsize[level] == p2p.recvid / groupsize[level]) {
            if(myid == ROOT)
              printf("level %d ", level + 1);
            comm_intra[level].add(p2p.sendbuf, p2p.sendoffset, p2p.recvbuf, p2p.recvoffset, p2p.count, p2p.sendid, p2p.recvid);
            found = true;
            break;
          }
        if(!found) {
          if(myid == ROOT)
            printf("level 0  *  (%d -> %d) sendoffset %lu recvoffset %lu count %lu \n", p2p.sendid, p2p.recvid, p2p.sendoffset, p2p.recvoffset, p2p.count);
          addlist_inter.push_back(P2P<T>(p2p.sendbuf, p2p.sendoffset, p2p.recvbuf, p2p.recvoffset, p2p.count, p2p.sendid, p2p.recvid));
        }
      }
      if(myid == ROOT) {
        printf("* to be splitted\n");
	printf("\n");
      }

      comm_split.push_back(CommBench::Comm<T>(comm_mpi, lib[0]));
      comm_merge.push_back(CommBench::Comm<T>(comm_mpi, lib[0]));

      for(auto &p2p : addlist_inter) {
        int sendgroup = p2p.sendid / groupsize[0];
        int recvgroup = p2p.recvid / groupsize[0];
        int mygroup = myid / groupsize[0];
        T *sendbuf_temp;
        T *recvbuf_temp;
        size_t splitcount = p2p.count / groupsize[0];
#ifdef PORT_CUDA
        if(mygroup == sendgroup && myid != p2p.sendid) {
          cudaMalloc(&sendbuf_temp, splitcount * sizeof(T));
	  sendbuf_inter.push_back(sendbuf_temp);
        }
	if(mygroup == recvgroup && myid != p2p.recvid) {
          cudaMalloc(&recvbuf_temp, splitcount * sizeof(T));
	  recvbuf_inter.push_back(recvbuf_temp);
        }
#elif PORT_HIP
#endif
        for(int p = 0; p < groupsize[0]; p++) {
          int recver = sendgroup * groupsize[0] + p;
          if(myid == ROOT)
            printf("split ");
          if(recver != p2p.sendid)
            comm_split[0].add(p2p.sendbuf, p2p.sendoffset + p * splitcount, sendbuf_temp, 0, splitcount, p2p.sendid, recver);
	  else
            if(myid == ROOT)
              printf(" * \n");
        }
	for(int p = 0; p < groupsize[0]; p++) {
          if(myid == ROOT)
            printf("inter ");
          int sender = sendgroup * groupsize[0] + p;
          int recver = recvgroup * groupsize[0] + p;
	  if(sender == p2p.sendid && recver == p2p.recvid)
            comm_inter[0].add(p2p.sendbuf, p2p.sendoffset + p * splitcount, p2p.recvbuf, p2p.recvoffset + p * splitcount, splitcount, sender, recver);
	  if(sender != p2p.sendid && recver == p2p.recvid)
            comm_inter[0].add(sendbuf_temp, 0, p2p.recvbuf, p2p.recvoffset + p * splitcount, splitcount, sender, recver);
	  if(sender == p2p.sendid && recver != p2p.recvid)
            comm_inter[0].add(p2p.sendbuf, p2p.sendoffset + p * splitcount, recvbuf_temp, 0, splitcount, sender, recver);
	  if(sender != p2p.sendid && recver != p2p.recvid)
            comm_inter[0].add(sendbuf_temp, 0, recvbuf_temp, 0, splitcount, sender, recver);
        }
        for(int p = 0; p < groupsize[0]; p++) {
          int sender = recvgroup * groupsize[0] + p;
          if(myid == ROOT)
            printf("merge ");
          if(sender != p2p.recvid)
            comm_merge[0].add(recvbuf_temp, 0, p2p.recvbuf, p2p.recvoffset + p * splitcount, splitcount, sender, p2p.recvid);
	  else
            if(myid == ROOT)
              printf(" * \n");
        }
      }
      if(myid == ROOT) {
        printf("* pruned\n");
        printf("\n");
      }
    }

    void init_mixed(int numlevel, int groupsize[], CommBench::library lib[]) {

      int myid;
      int numproc;
      MPI_Comm_rank(comm_mpi, &myid);
      MPI_Comm_size(comm_mpi, &numproc);

      for(int level = 0; level < numlevel; level++)
        comm_intra.push_back(CommBench::Comm<T>(comm_mpi, lib[level]));

      for(auto &p2p : addlist) {
        bool found = false;
        for(int level = numlevel - 1; level > -1; level--)
          if(p2p.sendid / groupsize[level] == p2p.recvid / groupsize[level]) {
            if(myid == ROOT)
              printf("level %d ", level + 1);
            comm_intra[level].add(p2p.sendbuf, p2p.sendoffset, p2p.recvbuf, p2p.recvoffset, p2p.count, p2p.sendid, p2p.recvid);
	    found = true;
            break;
          }
        if(!found) {
          if(myid == ROOT)
            printf("level 0 ");
          comm_inter[0].add(p2p.sendbuf, p2p.sendoffset, p2p.recvbuf, p2p.recvoffset, p2p.count, p2p.sendid, p2p.recvid);
        }
      }
      if(myid == ROOT)
        printf("\n");
    }

    public:

    Comm(const MPI_Comm &comm_mpi, CommBench::library lib)
    : comm_mpi(comm_mpi) {
      comm_inter.push_back(CommBench::Comm<T>(comm_mpi, lib));
    }

    void add(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, int recvid) {
      addlist.push_back(ExaComm::P2P<T>(sendbuf, sendoffset, recvbuf, recvoffset, count, sendid, recvid));
    }

    void init_flat() {
      init_mixed(0, NULL, NULL);
    }
    void init_mixed(int groupsize, CommBench::library lib) {
      init_mixed(1, &groupsize, &lib);
    };
    void init_mixed(int groupsize_1, int groupsize_2, CommBench::library lib_1, CommBench::library lib_2) {
      int numlevel = 2;
      int groupsize[numlevel] = {groupsize_1, groupsize_2};
      CommBench::library lib[numlevel] = {lib_1, lib_2};
      init_mixed(numlevel, groupsize, lib);
    }

    void init_striped(int groupsize, CommBench::library lib) {
      init_striped(1, &groupsize, &lib);
    }

    void init_striped(int groupsize_1, int groupsize_2, CommBench::library lib_1, CommBench::library lib_2) {
      int numlevel = 2;
      int groupsize[numlevel] = {groupsize_1, groupsize_2};
      CommBench::library lib[numlevel] = {lib_1, lib_2};
      init_striped(numlevel, groupsize, lib);
    }

    void run() {
      start();
      wait();
    }
    void measure(int numwarmup, int numiter) {
      int myid;
      int numproc;
      MPI_Comm_rank(comm_mpi, &myid);
      MPI_Comm_size(comm_mpi, &numproc);

      for(auto &comm : comm_split) {
        if(myid == ROOT)
          printf("******************** measure split map ");
        comm.measure(numwarmup, numiter);
      }
      for(auto &comm : comm_inter) {
        if(myid == ROOT)
          printf("******************** measure inter-group ");
        comm.measure(numwarmup, numiter);
      }
      for(auto &comm : comm_merge) {
        if(myid == ROOT)
          printf("******************** measure merge map ");
        comm.measure(numwarmup, numiter);
      }
      for(auto &comm : comm_intra) {
        if(myid == ROOT)
          printf("******************** measure intra-group ");
        comm.measure(numwarmup, numiter);
      }
    }
  };
}

