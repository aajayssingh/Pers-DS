NVTHREAD_HOME=../../
CC = g++
CFLAGS = -g -O0 
PLIB = -lpthread
NVLIB = $(NVTHREAD_HOME)/src/libnvthread.so -ldl
DLIB = $(NVTHREAD_HOME)/third-parties/dthreads/src/libdthread.so -ldl

#nvthread
INC_DIR = $(NVTHREAD_HOME)/src/include
SRC_DIR = $(NVTHREAD_HOME)/src/source

NVINCLUDE_DIRS = -I$(INC_DIR)
NVSRCS = $(SRC_DIR)/nvrecovery.cpp 

all:	recover_int recover_array recover_aggr recover_list recover_lstag recover_hashtab

recover_int:
	$(CC) $(CFLAGS) -DNVTHREAD $(NVINCLUDE_DIRS) $(NVSRCS) recover_int.c -o recover_int.o -rdynamic $(NVLIB)
recover_array:	
	$(CC) $(CFLAGS) -DNVTHREAD $(NVINCLUDE_DIRS) $(NVSRCS) recover_array.c -o recover_array.o -rdynamic $(NVLIB)
recover_aggr:	
	$(CC) $(CFLAGS) -DNVTHREAD $(NVINCLUDE_DIRS) $(NVSRCS) recover_aggr.c -o recover_aggr.o -rdynamic $(NVLIB)
recover_list:	
	$(CC) $(CFLAGS) -DNVTHREAD $(NVINCLUDE_DIRS) $(NVSRCS) recover_list.c -o recover_list.o -rdynamic $(NVLIB)
recover_lstag:	
	$(CC) $(CFLAGS) -DNVTHREAD $(NVINCLUDE_DIRS) $(NVSRCS) recover_lstag.c -o recover_lstag.o -rdynamic $(NVLIB)
recover_hashtab:	
	$(CC) $(CFLAGS) -DNVTHREAD $(NVINCLUDE_DIRS) $(NVSRCS) recover_hashtab.c -o recover_hashtab.o -rdynamic $(NVLIB)

clean:
	rm *.o MemLog* varmap* _crashed _running /mnt/tmpfs/*
