/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2011 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */



/*

Will write a separate trace file for each thread. Output format
is the following. Each line will contain either 3 or 4 items
that are tab deliminated.

If there are three items, the format is as follows:

CPU_ID	Sec	uSec

If there are four items:

PAGE_ID	Status[-ERROR]/NUMA_NODE[0-N]	READS	WRITES

*/

#include "pin.H"
#include <iostream>
#include <string.h>
#include <fstream>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sched.h>
#include <unistd.h>
#include <numaif.h>

#include <assert.h>
#include <map>
#include <vector>
#include <set>


// sudo aptget install libboost-iostreams-dev
// -lboost_iostreams
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file.hpp> // for file_sink

/* ===================================================================== */
/* Names of malloc and free */
/* ===================================================================== */
#if defined(TARGET_MAC)
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#endif




// Force each thread's data to be in its own data cache line so that
// multiple threads do not contend for the same data cache line.
// This avoids the false sharing problem.
#define PADSIZE 56  // 64 byte line size: 64-8

// a running count of the instructions
class thread_data_t
{
  public:
    thread_data_t() : _count(0) {}
    UINT64 _count;
	boost::iostreams::filtering_ostream ThreadStream;
    UINT8 _pad[PADSIZE];
};

typedef struct RtnName
{
    string _name;
    string _image;
    ADDRINT _address;
    RTN _rtn;
    struct RtnName * _next;
} RTN_NAME;

struct MemEvent {
	bool read;
	ADDRINT  addr;
};


const char * StripPath(const char * path)
{
    const char * file = strrchr(path,'/');
    if (file)
        return file+1;
    else
        return path;
}

int pagesize;
struct timeval start;

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::vector<MemEvent*> memEvents;

#define MAX_THREADS 1024

thread_data_t local[MAX_THREADS];
#define MAX_EVENTS 10000
#define SAFTY_CUT 9900

std::ofstream MallocFile;
PIN_LOCK lock;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "m", "malloctrace.out", "specify malloc trace file name");

/* ===================================================================== */


/* ===================================================================== */
/* Analysis routines                                                     */
/* ===================================================================== */


// Note that opening a file in a callback is only supported on Linux systems.
// See buffer-win.cpp for how to work around this issue on Windows.
//
// This routine is executed every time a thread is created.
VOID ThreadStart(THREADID threadid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
    GetLock(&lock, threadid+1);
	thread_data_t& tdata = local[threadid];
	tdata._count = 0;	
	boost::iostreams::filtering_ostream& ThreadStream = tdata.ThreadStream;
	char file[80];
	sprintf(file, "thread_%i.dat.gz", threadid);

	ThreadStream.push(boost::iostreams::gzip_compressor()); 
	ThreadStream.push(boost::iostreams::file_sink(file, ios_base::out | ios_base::binary));
	MallocFile << threadid << " basePtr " << PIN_GetContextReg(ctxt, REG_STACK_PTR) << endl;
	memEvents.push_back( (MemEvent*)malloc(MAX_EVENTS * sizeof(MemEvent)) );
    
    ReleaseLock(&lock);
}

VOID ThreadStop(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
	thread_data_t& tdata = local[tid];
	boost::iostreams::filtering_ostream& ThreadStream = tdata.ThreadStream;
	boost::iostreams::close(ThreadStream);
	
}

 




//http://stackoverflow.com/questions/2333728/stdmap-default-value
template <typename K, typename V>
V GetWithDef(const  std::map <K,V> & m, const K & key, const V & defval ) {
   typename std::map<K,V>::const_iterator it = m.find( key );
   if ( it == m.end() ) {
      return defval;
   }
   else {
      return it->second;
   }
}

ADDRINT CheckCutoff(THREADID tid) {
	thread_data_t& tdata = local[tid];
	return (tdata._count > SAFTY_CUT);
}

VOID timestamp(THREADID tid) { 
	thread_data_t& tdata = local[tid];
	assert((tdata._count < MAX_EVENTS) && "buffer overflow for page access; increase MAX_EVENTS constant");
   	boost::iostreams::filtering_ostream& ThreadStream = tdata.ThreadStream;
		std::set<void*> pages;
		std::map<void*,int> page_reads;
		std::map<void*,int> page_writes;
		for (int i = 0; i < (int)tdata._count; i++) {
			// http://stackoverflow.com/questions/6387771/get-starting-address-of-a-memory-page-in-linux
			void* page = (void*)((unsigned long long)memEvents[tid][i].addr & ~(pagesize-1));
			pages.insert(page);
			if (memEvents[tid][i].read) {
				page_reads[page] = GetWithDef(page_reads, page, 0) + 1;
			} else {
				page_writes[page] = GetWithDef(page_writes, page, 0) + 1;
			}
		}
		int cpuid = sched_getcpu();
		struct timeval stamp;
		gettimeofday(&stamp, NULL);
		// print core and time stamp
		ThreadStream << cpuid << "\t" << stamp.tv_sec - start.tv_sec << "\t" << stamp.tv_usec << endl;
		// print page node read writes
		for (std::set<void*>::iterator it = pages.begin(); it != pages.end(); it++) {
			int status[1];
			status[0]=-1;
			void * ptr_to_check = *it;
			move_pages(0 /*self memory */, 1, &ptr_to_check,  NULL, status, 0);
			
			ThreadStream << ((unsigned long long)(*it))/pagesize << "\t" << status[0] << "\t" << GetWithDef(page_reads, *it, 0) << "\t" << GetWithDef(page_writes, *it, 0) << "\n";
		}
		
		tdata._count = 0;

}


// Print a memory read access
VOID PIN_FAST_ANALYSIS_CALL RecordMemRead(ADDRINT  addr, THREADID threadid) {
	thread_data_t& tdata = local[threadid];
    memEvents[threadid][tdata._count].read = 1;
	memEvents[threadid][tdata._count].addr = addr;
	tdata._count = (tdata._count + 1) ;
}

VOID PIN_FAST_ANALYSIS_CALL RecordMemWrite(ADDRINT  addr, THREADID threadid) {
	thread_data_t& tdata = local[threadid];
//	thread_data_t* tdata = get_tls(threadid);
    memEvents[threadid][tdata._count].read = 0;
	memEvents[threadid][tdata._count].addr = addr;
	tdata._count = (tdata._count + 1) ;

}

/* ===================================================================== */
/* Instrumentation routines                                              */
/* ===================================================================== */


VOID Routine(RTN rtn, VOID *v)
{
	// Allocate a counter for this routine
    RtnName * rc = new RTN_NAME;

    // The RTN goes away when the image is unloaded, so save it now
    // because we need it in the fini
    rc->_name = RTN_Name(rtn);
    rc->_image = StripPath(IMG_Name(SEC_Img(RTN_Sec(rtn))).c_str());
    rc->_address = RTN_Address(rtn);
	MallocFile << rc->_address << " " << rc->_image.c_str() << " " << rc->_name.c_str() << endl;
}

VOID Trace(TRACE trace, VOID *v)
{
	TRACE_InsertIfCall(trace, IPOINT_BEFORE, (AFUNPTR)CheckCutoff, IARG_THREAD_ID, IARG_END);
	TRACE_InsertThenCall(trace, IPOINT_BEFORE, (AFUNPTR)timestamp, IARG_THREAD_ID, IARG_END);
}

   
// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v)
{
    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // The IA-64 architecture has explicitly predicated instructions. 
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
				IARG_FAST_ANALYSIS_CALL,
	            IARG_MEMORYOP_EA, memOp, 
				IARG_THREAD_ID,
				IARG_END);
        }
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
				IARG_FAST_ANALYSIS_CALL,
			    IARG_MEMORYOP_EA, memOp, 
				IARG_THREAD_ID,
				IARG_END);
        }
    }
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID *v)
{
    MallocFile.close();

	for (int i = 0; i < (int)memEvents.size(); i++) {
		free(memEvents[i]);
	}
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage()
{
    cerr << "This tool produces a trace of calls to malloc." << endl;
    cerr << endl << KNOB_BASE::StringKnobSummary() << endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{

	pagesize = getpagesize();
    // Initialize pin & symbol manager
    PIN_InitSymbols();
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

	// Initialize the pin lock
    InitLock(&lock);

 
    // Write to a file since cout and cerr maybe closed by the application
    MallocFile.open(KnobOutputFile.Value().c_str());
  //  MallocFile << hex;
  //  MallocFile.setf(ios::showbase);
    
	MallocFile << "Page size: " << pagesize << "\n";

    // Register Image to be called to instrument functions.
    INS_AddInstrumentFunction(Instruction, 0);
	TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);

	// Register Analysis routines to be called when a thread begins/ends
    PIN_AddThreadStartFunction(ThreadStart, 0);
	PIN_AddThreadFiniFunction(ThreadStop, 0);



    // Never returns
	gettimeofday(&start, NULL);
    PIN_StartProgram();
    
    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
