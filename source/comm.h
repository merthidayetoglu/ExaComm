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

  template <typename T>
  class Comm {

    // PRIMITIVES
    std::vector<std::vector<BROADCAST<T>>> bcast_epoch;
    std::vector<std::vector<REDUCE<T>>> reduce_epoch;
    int numepoch = 0;

    // MACHINE
    std::vector<int> numlevel;
    std::vector<std::vector<int>> groupsize;

    public:

    // HiCCL PARAMETERS
    std::vector<int> hierarchy = {numproc};
    std::vector<CommBench::library> library = {CommBench::MPI};
    int numstripe = 1;
    int ringnodes = 1;
    int pipedepth = 1;

    // ENDPOINTS
    T *sendbuf;
    T *recvbuf;
    size_t sendcount;
    size_t recvcount;
    void set_endpoints(T *sendbuf, size_t sendcount, T *recvbuf, size_t recvcount) {
      this->sendbuf = sendbuf;
      this->sendcount = sendcount;
      this->recvbuf = recvbuf;
      this->recvcount = recvcount;
    }

    void print_parameters() {
      if(myid == printid) {
        printf("**************** HiCCL PARAMETERS\n");
        printf("%d-level hierarchy:\n", hierarchy.size());
        for(int i = 0; i < hierarchy.size(); i++) {
          printf("  level %d factor: %d library: ", i, hierarchy[i]);
          CommBench::print_lib(library[i]);
          printf("\n");
        }
        printf("numstripe: %d", numstripe);
        if(numstripe == 1)
          printf(" (default)\n");
        else
          printf("\n");
        printf("ringnodes: %d", ringnodes);
        if(ringnodes == 1)
          printf(" (default)\n");
        else
          printf("\n");
        printf("pipedepth: %d", pipedepth);
        if(pipedepth == 1)
          printf(" (default)\n");
        else
          printf("\n");
        printf("*********************************\n");
      }
    }

    // FUTURE PIPELINE
    std::vector<std::list<Command<T>>> command_batch;
    std::vector<std::list<Coll<T>*>> coll_batch;

    void add_fence() {
      bcast_epoch.push_back(std::vector<BROADCAST<T>>());
      reduce_epoch.push_back(std::vector<REDUCE<T>>());
      if(myid == printid)
        printf("Add epoch %d\n", numepoch);
      numepoch++;
    }

    Comm() {
      // DEFAULT EPOCH
      this->add_fence();
    }

    // ADD FUNCTIONS FOR BROADCAST AND REDUCE PRIMITIVES
    void add_bcast(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, std::vector<int> &recvids) {
      bcast_epoch.back().push_back(BROADCAST<T>(sendbuf, sendoffset, recvbuf, recvoffset, count, sendid, recvids));
    }
    void add_bcast(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, int recvid) {
      bcast_epoch.back().push_back(BROADCAST<T>(sendbuf, sendoffset, recvbuf, recvoffset, count, sendid, recvid));
    }
    void add_reduce(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, std::vector<int> &sendids, int recvid) {
      reduce_epoch.back().push_back(REDUCE<T>(sendbuf, sendoffset, recvbuf, recvoffset, count, sendids, recvid));
    }
    void add_reduce(T *sendbuf, size_t sendoffset, T *recvbuf, size_t recvoffset, size_t count, int sendid, int recvid) {
      reduce_epoch.back().push_back(REDUCE<T>(sendbuf, sendoffset, recvbuf, recvoffset, count, sendid, recvid));
    }

    // INITIALIZE BROADCAST AND REDUCTION TREES
    void init(int numlevel, int groupsize[], CommBench::library lib[], int numstripe, int numbatch) {

      MPI_Barrier(comm_mpi);
      double init_time = MPI_Wtime();

      if(myid == printid) {
        printf("NUMBER OF EPOCHS: %d\n", numepoch);
        for(int epoch = 0; epoch < numepoch; epoch++)
          printf("epoch %d: %zu bcast %zu reduction\n", epoch, bcast_epoch[epoch].size(), reduce_epoch[epoch].size());
        printf("Initialize HiCCL with %d levels\n", numlevel);
        for(int level = 0; level < numlevel; level++) {
          printf("level %d groupsize %d library: ", level, groupsize[level]);
          switch(lib[level]) {
            case CommBench::IPC  : printf("IPC"); break;
            case CommBench::MPI  : printf("MPI"); break;
            case CommBench::XCCL : printf("XCCL"); break;
            default : break;
          }
          if(level == 0)
            if(groupsize[0] != numproc)
              printf(" *");
          printf("\n");
        }
        printf("\n");
      }

      // ALLOCATE COMMAND BATCH
      for(int batch = 0; batch < numbatch; batch++)
        coll_batch.push_back(std::list<Coll<T>*>());

      // TEMP HIERARCHY FOR TREE
      std::vector<int> groupsize_temp(groupsize, groupsize + numlevel);
      groupsize_temp[0] = numproc;

      // FOR EACH EPOCH
      for(int epoch = 0; epoch < numepoch; epoch++) {
        // INIT BROADCAST
        std::vector<BROADCAST<T>> &bcastlist = bcast_epoch[epoch];
        if(bcastlist.size()) {
          // PARTITION INTO BATCHES
          std::vector<std::vector<BROADCAST<T>>> bcast_batch(numbatch);
          partition(bcastlist, numbatch, bcast_batch);
          // FOR EACH BATCH
          for(int batch = 0; batch < numbatch; batch++) {
            // STRIPE BROADCAST PRIMITIVES
            std::vector<REDUCE<T>> split_list;
            stripe(numstripe, bcast_batch[batch], split_list);
            // ExaComm::stripe_ring(comm_mpi, numstripe, bcast_batch[batch], split_list);
            // IMPLEMENT WITH IPC
            // Coll<T> *stripe = new Coll<T>(CommBench::MPI);
            //for(auto &p2p : split_list)
            //  stripe->add(p2p.sendbuf, p2p.sendoffset, p2p.recvbuf, p2p.recvoffset, p2p.count, p2p.sendids[0], p2p.recvid);
            // coll_batch[batch].push_back(stripe);
            // STRIPING THROUGH CPU
            /*Coll<T> *stripe_d2h = new Coll<T>(CommBench::STAGE);
            Coll<T> *stripe_h2h = new Coll<T>(CommBench::MPI);
            Coll<T> *stripe_h2d = new Coll<T>(CommBench::STAGE);
            for(auto &p2p : split_list) {
              T *sendbuf_h;
              T *recvbuf_h;
              if(myid == p2p.sendids[0])
                allocateHost(sendbuf_h, p2p.count);
              if(myid == p2p.recvid)
                allocateHost(recvbuf_h, p2p.count);
              stripe_d2h->add(p2p.sendbuf, p2p.sendoffset, sendbuf_h, 0, p2p.count, p2p.sendids[0], -1);
              stripe_h2h->add(sendbuf_h, 0, recvbuf_h, 0, p2p.count, p2p.sendids[0], p2p.recvid);
              stripe_h2d->add(recvbuf_h, 0, p2p.recvbuf, p2p.recvoffset, p2p.count, -1, p2p.recvid);
            }
            coll_batch[batch].push_back(stripe_d2h);
            coll_batch[batch].push_back(stripe_h2h);
            coll_batch[batch].push_back(stripe_h2d);*/
            // CPU STAGING ACROSS NODES
            /*{
	      Coll<T> *stage_d2h = new Coll<T>(CommBench::STAGE);
	      std::vector<BROADCAST<T>> temp_batch = bcast_batch[batch];
              bcast_batch[batch].clear();
              for(auto &bcast : temp_batch) {
	        T *stagebuf;
	        allocateHost(stagebuf, bcast.count);
                stage_d2h->add(bcast.sendbuf, bcast.sendoffset, stagebuf, 0, bcast.count, bcast.sendid, -1);
                bcast_batch[batch].push_back(BROADCAST<T>(stagebuf, 0, bcast.recvbuf, bcast.recvoffset, bcast.count, bcast.sendid, bcast.recvids));
              }
	      coll_batch[batch].push_back(stage_d2h);
            }*/
            
	    // Coll<T> *stage_h2d = new Coll<T>(CommBench::STAGE);

            // APPLY REDUCE TREE TO ROOTS FOR STRIPING
            std::vector<T*> recvbuff; // for memory recycling
            reduce_tree(numlevel, groupsize_temp.data(), lib, split_list, numlevel - 1, coll_batch[batch], recvbuff, 0);
            // std::vector<BROADCAST<T>> bcast_temp; // for accumulating intra-node communications for tree (internally)
            // ExaComm::bcast_ring(comm_mpi, 1, lib[numlevel-1], split_list, bcast_temp, coll_batch[batch], &allocate);

            // APPLY RING TO BRANCHES ACROSS NODES
            std::vector<BROADCAST<T>> bcast_intra; // for accumulating intra-node communications for tree (internally)
            bcast_ring(groupsize[0], lib[0], bcast_batch[batch], bcast_intra, coll_batch[batch]);

            // APPLY TREE TO THE LEAVES WITHIN NODES
            bcast_tree(numlevel, groupsize_temp.data(), lib, bcast_intra, 1, coll_batch[batch]);
          }
        }
        // INIT REDUCTION
        std::vector<REDUCE<T>> &reducelist = reduce_epoch[epoch];
        if(reducelist.size()) {
          // PARTITION INTO BATCHES
          std::vector<std::vector<REDUCE<T>>> reduce_batch(numbatch);
          partition(reducelist, numbatch, reduce_batch);
          // FOR EACH BATCH
          for(int batch = 0; batch < numbatch; batch++) {
            // STRIPE REDUCTION
            std::vector<BROADCAST<T>> merge_list;
            stripe(numstripe, reduce_batch[batch], merge_list);
            // HIERARCHICAL REDUCTION RING + TREE
	    std::vector<REDUCE<T>> reduce_intra; // for accumulating intra-node communications for tree (internally)
            reduce_ring(numlevel, groupsize, lib, reduce_batch[batch], reduce_intra, coll_batch[batch]);
            // COMPLETE STRIPING BY INTRA-NODE GATHER
            bcast_tree(numlevel, groupsize_temp.data(), lib, merge_list, 1, coll_batch[batch]);
	  }
        }
      }
      // IMPLEMENT WITH COMMBENCH
      implement(coll_batch, command_batch, 1);

      MPI_Barrier(comm_mpi);
      if(myid == printid)
        printf("initialization time: %e seconds\n", MPI_Wtime() - init_time);
    }

    void init() {
      print_parameters();
      int numlevel = hierarchy.size();
      std::vector<int> groupsize(numlevel);
      groupsize[numlevel - 1] = hierarchy[numlevel - 1];
      for(int i = numlevel - 2; i > -1; i--)
        groupsize[i] = groupsize[i + 1] * hierarchy[i];

      init(numlevel, groupsize.data(), library.data(), numstripe, pipedepth);
    }

    void run() {
      using Iter = typename std::list<Command<T>>::iterator;
      std::vector<Iter> commandptr(command_batch.size());
      for(int i = 0; i < command_batch.size(); i++)
        commandptr[i] = command_batch[i].begin();
      while(true) {
        bool finished = true;
        for(int i = 0; i < command_batch.size(); i++)
          if(commandptr[i] != command_batch[i].end()) {
            commandptr[i]->comm->start();
            finished = false;
          }
        if(finished)
          break;
        for(int i = command_batch.size() - 1; i > -1; i--)
          if(commandptr[i] != command_batch[i].end()) {
            commandptr[i]->comm->wait();
            commandptr[i]->compute->start();
          }
        for(int i = 0; i < command_batch.size(); i++)
          if(commandptr[i] != command_batch[i].end()) {
            commandptr[i]->compute->wait();
            commandptr[i]++;
          }
      }
    }

    void run(T *sendbuf, T *recvbuf) {
      CommBench::memcpyD2D(this->sendbuf, sendbuf, sendcount);
      run();
      CommBench::memcpyD2D(recvbuf, this->recvbuf, recvcount);
    }

    void measure(int warmup, int numiter, size_t count) {

      if(myid == printid) {
        printf("command_batch size %zu\n", command_batch.size());
        printf("commandlist size %zu\n", command_batch[0].size());
      }
      MPI_Barrier(comm_mpi);
      {
        using Iter = typename std::list<Command<T>>::iterator;
        std::vector<Iter> commandptr(command_batch.size());
        for(int i = 0; i < command_batch.size(); i++)
          commandptr[i] = command_batch[i].begin();
        while(true) {
          bool finished = true;
          for(int i = 0; i < command_batch.size(); i++)
            if(commandptr[i] != command_batch[i].end())
              finished = false;
          if(finished)
            break;
          if(myid == printid) printf("******************************************* MEASURE COMMANDS ************************************************\n");
          for(int i = 0; i < command_batch.size(); i++)
            if(commandptr[i] != command_batch[i].end()) {
              commandptr[i]->measure(warmup, numiter, count);
              commandptr[i]++;
            }
          /*if(myid == printid) printf("******************************************* MEASURE STEP ************************************************\n");
          MPI_Barrier(comm_mpi);
          double time = MPI_Wtime();
          for(int i = 0; i < command_batch.size(); i++)
            if(commandptr[i] != command_batch[i].end())
              commandptr[i]->comm->start();
          for(int i = 0; i < command_batch.size(); i++)
            if(commandptr[i] != command_batch[i].end()) {
              commandptr[i]->comm->wait();
              commandptr[i]++;
            }
          MPI_Barrier(comm_mpi);
          time = MPI_Wtime() - time;
          if(myid == printid)
            printf("time: %e\n", time);*/
        }
      }
    }

    void report() {
      if(myid == printid) {
        printf("command_batch size %zu\n", command_batch.size());
        printf("commandlist size %zu\n", command_batch[0].size());
      }
      int command = 0;
      for(auto it = command_batch[0].begin(); it != command_batch[0].end(); it++) {
        if(myid == printid)
          printf("command %d", command);
        it->report();
        command++;
      }
    }

    void time() {
      if(myid == printid) {
        printf("********************************************\n\n");
        printf("pipeline depth %zu\n", command_batch.size());
        printf("commandlist size %zu\n", command_batch[0].size());
        printf("\n");
      }
      int print_batch_size = (command_batch.size() > 16 ? 16 : command_batch.size());
      {
        using Iter = typename std::list<Command<T>>::iterator;
        std::vector<Iter> commandptr(print_batch_size);
        for(int i = 0; i < print_batch_size; i++)
          commandptr[i] = command_batch[i].begin();
        int command = 0;
        while(true) {
          if(myid == printid)
            printf("proc %d command %d: |", myid, command);
          bool finished = true;
          for(int i = 0; i < print_batch_size; i++) {
            if(commandptr[i] != command_batch[i].end()) {
              if(commandptr[i]->comm) {
                int numsend = commandptr[i]->comm->numsend;
                int numrecv = commandptr[i]->comm->numrecv;
                //MPI_Allreduce(MPI_IN_PLACE, &numsend, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
                //MPI_Allreduce(MPI_IN_PLACE, &numrecv, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
                if(myid == printid) {
                  if(numsend) printf(" %d", numsend);
                  else        printf("  ");
                  if(numrecv) printf("+%d", numrecv);
                  else        printf("  ");
                  if(numsend+numrecv)
                    switch(commandptr[i]->comm->lib) {
                      case CommBench::IPC :  printf(" IPC"); break;
                      case CommBench::MPI :  printf(" MPI"); break;
		      case CommBench::XCCL : printf(" XCCL"); break;
                      default : break;
                    }
                  else // printf("-   ");
                    switch(commandptr[i]->comm->lib) {
                      case CommBench::IPC  : printf("I   "); break;
                      case CommBench::MPI  : printf("M   "); break;
                      case CommBench::XCCL : printf("X   "); break;
                      default : break;
                    }
                }
                if(commandptr[i]->compute) {
                  int numcomp = commandptr[i]->compute->numcomp;
                  //MPI_Allreduce(MPI_IN_PLACE, &numcomp, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
                  if(myid == printid) {
                    if(numcomp) printf(" %d*", numcomp);
                    else        printf("*  ");
                  }
                }
                if(myid == printid)
                  printf(" |");
	      }
	      else if(commandptr[i]->compute) {
                int numcomp = commandptr[i]->compute->numcomp;
                //MPI_Allreduce(MPI_IN_PLACE, &numcomp, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
                if(myid == printid) {
                  if(numcomp) printf("  %d  *** |", numcomp);
                  else        printf("    *    |");
                }
              }
              finished = false;
              commandptr[i]++;
            }
            else
              if(myid == printid)
                printf("         |");
          }
          if(myid == printid)
            printf("\n");
          if(finished)
            break;
          command++;
        }
      }

      using Iter = typename std::list<Command<T>>::iterator;
      std::vector<Iter> commandptr(command_batch.size());
      for(int i = 0; i < command_batch.size(); i++)
        commandptr[i] = command_batch[i].begin();

      int command = 0;
      double totalstarttime = 0;
      double totalwaittime = 0;
      MPI_Barrier(comm_mpi);
      double totaltime = MPI_Wtime();
      while(true) {
        double starttime;
        double waittime;
        bool finished = true;
        {
          MPI_Barrier(comm_mpi);
          double time = MPI_Wtime();
          for(int i = 0; i < command_batch.size(); i++)
            if(commandptr[i] != command_batch[i].end()) {
              commandptr[i]->start();
              finished = false;
            }
          MPI_Barrier(comm_mpi);
          starttime = MPI_Wtime() - time;
        }
        MPI_Allreduce(MPI_IN_PLACE, &finished, 1, MPI_C_BOOL, MPI_LOR, comm_mpi);
        if(finished)
          break;
        {
          MPI_Barrier(comm_mpi);
          double time = MPI_Wtime();
          for(int i = 0; i < command_batch.size(); i++)
            if(commandptr[i] != command_batch[i].end()) {
              commandptr[i]->wait();
              commandptr[i]++;
            }
          MPI_Barrier(comm_mpi);
          waittime = MPI_Wtime() - time;
        }
        if(myid == printid)
          printf("command %d start: %e wait: %e\n", command, starttime, waittime);
        totalstarttime += starttime;
        totalwaittime += waittime;
        command++;
      }
      MPI_Barrier(comm_mpi);
      totaltime = MPI_Wtime() - totaltime;
      if(myid == printid) {
        printf("start %e wait %e other %e\n", totalstarttime, totalwaittime, totaltime - totalstarttime - totalwaittime); 
        printf("total time %e\n", totaltime);
      }
    }
  };
