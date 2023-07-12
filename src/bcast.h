
template <typename T>
  struct BCAST {
    public:
    T* const sendbuf;
    const size_t sendoffset;
    T* const recvbuf;
    const size_t recvoffset;
    const size_t count;
    const int sendid;
    std::vector<int> recvids;

    BCAST(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, std::vector<int> &recvids)
    : sendbuf(sendbuf), sendoffset(sendoffset), recvbuf(recvbuf), recvoffset(recvoffset), count(count), sendid(sendid), recvids(recvids) {}
    BCAST(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, int recvid)
    : sendbuf(sendbuf), sendoffset(sendoffset), recvbuf(recvbuf), recvoffset(recvoffset), count(count), sendid(sendid), recvids(recvids) { recvids.push_back(recvid); }

    void report(int id) {
      if(printid == sendid) {
        MPI_Send(&sendbuf, sizeof(T*), MPI_BYTE, id, 0, MPI_COMM_WORLD);
        MPI_Send(&sendoffset, sizeof(size_t), MPI_BYTE, id, 0, MPI_COMM_WORLD);
      }
      for(auto recvid : this->recvids)
        if(printid == recvid) {
          MPI_Send(&recvbuf, sizeof(T*), MPI_BYTE, id, 0, MPI_COMM_WORLD);
          MPI_Send(&recvoffset, sizeof(size_t), MPI_BYTE, id, 0, MPI_COMM_WORLD);
        }
      if(printid == id) {
        T* sendbuf_sendid;
        size_t sendoffset_sendid;
        MPI_Recv(&sendbuf_sendid, sizeof(T*), MPI_BYTE, sendid, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&sendoffset_sendid, sizeof(size_t), MPI_BYTE, sendid, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        size_t recvoffset_recvid[recvids.size()];
        T* recvbuf_recvid[recvids.size()];
        for(int recv = 0; recv < recvids.size(); recv++) {
          MPI_Recv(recvbuf_recvid + recv, sizeof(T*), MPI_BYTE, recvids[recv], 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
          MPI_Recv(recvoffset_recvid + recv, sizeof(size_t), MPI_BYTE, recvids[recv], 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        printf("BCAST report: count %lu\n", count);
        char text[1000];
        int n = sprintf(text, "sendid %d sendbuf %p sendoffset %lu -> ", sendid, sendbuf_sendid, sendoffset_sendid);
        printf("%s", text);
        memset(text, ' ', n);
        for(int recv = 0; recv < recvids.size(); recv++) {
          printf("recvid: %d recvbuf %p recvoffset %lu\n", recvids[recv], recvbuf_recvid[recv], recvoffset_recvid[recv]);
          printf("%s", text);
        }
        printf("\n");
      }
    }
  };

  template <typename T>
  void bcast_tree(const MPI_Comm &comm_mpi, int numlevel, int groupsize[], CommBench::library lib[], std::vector<BCAST<T>> bcastlist, int level, std::list<Command<T>> &commandlist) {

    int myid;
    int numproc;
    MPI_Comm_rank(comm_mpi, &myid);
    MPI_Comm_size(comm_mpi, &numproc);

    if(numproc != groupsize[0]) {
      printf("ERROR!!! groupsize[0] must be equal to numproc.\n");
      return;
    }
    if(bcastlist.size() == 0)
      return;
    CommBench::Comm<T> *comm_temp = new CommBench::Comm<T>(comm_mpi, lib[level-1]);
    commandlist.push_back(Command<T>(comm_temp));
    std::vector<BCAST<T>> bcastlist_new;

    //  EXIT CONDITION
    if(level == numlevel) {
      if(printid == ROOT)
         printf("************************************ leaf level %d groupsize %d\n", level, groupsize[level - 1]);
      for(auto bcast : bcastlist) {
        for(auto recvid : bcast.recvids) {
          int sendid = bcast.sendid;
          comm_temp->add(bcast.sendbuf, bcast.sendoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, sendid, recvid);
        }
      }
      if(printid == ROOT)
        printf("\n");
      return;
    }

    int numgroup = numproc / groupsize[level];

    // LOCAL COMMUNICATIONS
    {
      for(auto bcast : bcastlist) {
        int sendgroup = bcast.sendid / groupsize[level];
        for(int recvgroup = 0; recvgroup < numgroup; recvgroup++) {
          if(sendgroup == recvgroup) {
            std::vector<int> recvids;
            for(auto recvid : bcast.recvids) {
              int group = recvid / groupsize[level];
              if(group == recvgroup)
                recvids.push_back(recvid);
            }
            if(recvids.size()) {
              // if(printid == ROOT)
              //  printf("level %d groupsize %d numgroup %d sendgroup %d recvgroup %d recvid %d\n", level, groupsize[level], numgroup, sendgroup, recvgroup, bcast.sendid);
              bcastlist_new.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, bcast.sendid, recvids));
            }
          }
        }
      }
    }

    // GLOBAL COMMUNICATIONS
    {
      for(int recvgroup = 0; recvgroup < numgroup; recvgroup++) {
        for(auto bcast : bcastlist) {
          int sendgroup = bcast.sendid / groupsize[level];
          if(sendgroup != recvgroup) {
            std::vector<int> recvids;
            for(auto recvid : bcast.recvids) {
              if(recvid / groupsize[level] == recvgroup)
                recvids.push_back(recvid);
            }
            if(recvids.size()) {
              int recvid = recvgroup * groupsize[level] + bcast.sendid % groupsize[level];
              // if(printid == ROOT)
              //  printf("level %d groupsize %d numgroup %d sendgroup %d recvgroup %d recvid %d\n", level, groupsize[level], numgroup, sendgroup, recvgroup, recvid);
              T *recvbuf;
              int recvoffset;
              bool found = false;
              for(auto it = recvids.begin(); it != recvids.end(); ++it) {
                if(*it == recvid) {
                  if(printid == ROOT)
                    printf("******************************************************************************************* found recvid %d\n", *it);
                  recvbuf = bcast.recvbuf;
                  recvoffset = bcast.recvoffset;
                  found = true;
                  recvids.erase(it);
                  break;
                }
              }
              if(!found && myid == recvid) {
#ifdef PORT_CUDA
                cudaMalloc(&recvbuf, bcast.count * sizeof(T));
#elif defined PORT_HIP
                hipMalloc(&recvbuf, bcast.count * sizeof(T));
#endif
                buffsize += bcast.count;
                recvoffset = 0;
                printf("^^^^^^^^^^^^^^^^^^^^^^^ recvid %d myid %d allocates recvbuf %p equal %d\n", recvid, myid, recvbuf, myid == recvid);
              }
              comm_temp->add(bcast.sendbuf, bcast.sendoffset, recvbuf,  recvoffset, bcast.count, bcast.sendid, recvid);
              if(recvids.size()) {
                bcastlist_new.push_back(BCAST<T>(recvbuf, recvoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, recvid, recvids));
              }
            }
          }
        }
      }
    }
    bcast_tree(comm_mpi, numlevel, groupsize, lib, bcastlist_new, level + 1, commandlist);
  }

  template <typename T>
  void stripe(const MPI_Comm &comm_mpi, int nodesize, CommBench::library lib_intra, std::vector<BCAST<T>> &bcastlist, std::list<Command<T>> &commandlist) {

    int myid;
    int numproc;
    MPI_Comm_rank(comm_mpi, &myid);
    MPI_Comm_size(comm_mpi, &numproc);

    // SEPARATE INTRA AND INTER NODES
    std::vector<BCAST<T>> bcastlist_intra;
    std::vector<BCAST<T>> bcastlist_inter;
    for(auto &bcast : bcastlist) {
      int sendid = bcast.sendid;
      std::vector<int> recvid_intra;
      std::vector<int> recvid_inter;
      for(auto &recvid : bcast.recvids)
        if(sendid / nodesize == recvid / nodesize)
          recvid_intra.push_back(recvid);
        else
          recvid_inter.push_back(recvid);
      if(recvid_inter.size())
        bcastlist_inter.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, bcast.sendid, bcast.recvids));
      else
        bcastlist_intra.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, bcast.sendid, bcast.recvids));
    }
    if(printid == ROOT) {
      printf("broadcast striping groupsize: %d numgroups: %d\n", nodesize, numproc / nodesize);
      printf("number of original broadcasts: %zu\n", bcastlist.size());
      printf("number of intra-node broadcast: %zu number of inter-node broadcast: %zu\n", bcastlist_intra.size(), bcastlist_inter.size());
    }
    // CLEAR BCASTLIST
    bcastlist.clear();
    // ADD INTRA-NODE BROADCAST DIRECTLY (IF ANY)
    for(auto &bcast : bcastlist_intra)
      bcastlist.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, bcast.sendid, bcast.recvids));

    // ADD INTER-NODE BROADCAST BY STRIPING
    if(bcastlist_inter.size())
    {
      CommBench::Comm<T> *split = new CommBench::Comm<T>(comm_mpi, lib_intra);
      for(auto &bcast : bcastlist_inter) {
        int sendgroup = bcast.sendid / nodesize;
        int mygroup = myid / nodesize;
        T *sendbuf_temp;
        size_t splitcount = bcast.count / nodesize;
        if(mygroup == sendgroup && myid != bcast.sendid) {
#ifdef PORT_CUDA
          cudaMalloc(&sendbuf_temp, splitcount * sizeof(T));
#elif defined PORT_HIP
          hipMalloc(&sendbuf_temp, splitcount * sizeof(T));
#endif
          buffsize += splitcount;
        }
        // SPLIT
        for(int p = 0; p < nodesize; p++) {
          int recver = sendgroup * nodesize + p;
          if(printid == ROOT)
            printf("split ");
          if(recver != bcast.sendid) {
            if(recver / nodesize == bcast.sendid / nodesize)
              split->add(bcast.sendbuf, bcast.sendoffset + p * splitcount, sendbuf_temp, 0, splitcount, bcast.sendid, recver);
            bcastlist.push_back(BCAST<T>(sendbuf_temp, 0, bcast.recvbuf, bcast.recvoffset + p * splitcount, splitcount, recver, bcast.recvids));
          }
          else {
            if(printid == ROOT)
              printf("* skip self\n");
            bcastlist.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset + p * splitcount, bcast.recvbuf, bcast.recvoffset + p * splitcount, splitcount, bcast.sendid, bcast.recvids));
          }
        }
      }
      commandlist.push_back(Command<T>(split));
    }
  }

  template <typename T>
  void scatter(const MPI_Comm &comm_mpi, int nodesize, CommBench::library lib_inter, CommBench::library lib_intra, std::vector<BCAST<T>> &bcastlist, std::list<Command<T>> &commandlist) {

    int myid;
    int numproc;
    MPI_Comm_rank(comm_mpi, &myid);
    MPI_Comm_size(comm_mpi, &numproc);

    std::vector<BCAST<T>> scatterlist;
    std::vector<BCAST<T>> bcastlist_new;
    for(auto &bcast : bcastlist) {
      // STRIPE INTO NUMBER OF THE RECVIEVING PROCS
      int scattersize = bcast.count / bcast.recvids.size();
      int scatter = 0;
      for(auto it = bcast.recvids.begin(); it != bcast.recvids.end(); ++it) {
        int recvid = *it; 
        scatterlist.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset + scatter * scattersize, bcast.recvbuf, bcast.recvoffset + scatter * scattersize, scattersize, bcast.sendid, recvid));
        // UPDATE THE BCAST PRIMITIVE FOR ALL-GATHER EXCEPT SELF
        std::vector<int> recvids;
        for(auto it_temp = bcast.recvids.begin(); it_temp != bcast.recvids.end(); ++it_temp) 
          if(it_temp != it)
            recvids.push_back(*it_temp);
	if(recvids.size())
          bcastlist_new.push_back(BCAST<T>(bcast.recvbuf, bcast.recvoffset + scatter * scattersize, bcast.recvbuf, bcast.recvoffset + scatter * scattersize, scattersize, recvid, recvids));
        scatter++;
      }
    }
    if(printid == ROOT) {
      printf("bcastlist size %zu\n", bcastlist.size());
      printf("scatterlist size %zu\n", scatterlist.size());
      printf("new bcastlist size %zu\n", bcastlist_new.size());
      printf("lib_inter %d\n", lib_inter);
      printf("lib_intra %d\n", lib_intra);
    }
    // STRIPE SCATTER & UPDATE STRIPELIST
    stripe(comm_mpi, nodesize, lib_intra, scatterlist, commandlist);
    // BUILD TWO-LEVEL BROADCAST TREE
    {
      std::vector<int> groupsize = {numproc, nodesize};
      std::vector<CommBench::library> lib = {lib_inter, lib_intra};
      bcast_tree(comm_mpi, groupsize.size(), groupsize.data(), lib.data(), scatterlist, 1, commandlist);
    }
    for(auto &command : commandlist)
      command.measure(5, 10);
    // CLEAR AND UPDATE WITH NEW BCAST LIST
    bcastlist.clear();
    for(auto &bcast : bcastlist_new) {
      bcastlist.push_back(BCAST<T>(bcast.sendbuf, bcast.sendoffset, bcast.recvbuf, bcast.recvoffset, bcast.count, bcast.sendid, bcast.recvids));
      bcast.report(ROOT);
    }
  }
