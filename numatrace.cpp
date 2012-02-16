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
 * 
 * A memory trace (Ip of memory accessing instruction and address of memory access - see
 * struct MEMREF) is collected by inserting Pin buffering API code into the application code, 
 * via calls to INS_InsertFillBuffer. This analysis code writes a MEMREF into the 
 * buffer being filled, and calls the registered BufferFull function (see call to 
 * PIN_DefineTraceBuffer which defines the buffer and registers the BufferFull function) 
 * when the buffer becomes full.
 * The BufferFull function processes the buffer and returns it to Pin to be filled again.
 *
 * Each application thread has it's own buffer - so multiple application threads do NOT
 * block each other on buffer accesses
 *
 * This tool is similar to memtrace_simple, but uses the Pin Buffering API
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include <sched.h>

#include "pin.H"
#include "portability.H"

#include <sys/time.h>
#include <vector>
#include <map>
#include <vector>
#include <set>
#include <unistd.h>
#include <numaif.h>


#include <iostream>
#include <fstream>
#ifdef COMPRESS_STREAM

// sudo aptget install libboost-iostreams-dev
// -lboost_iostreams
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/device/file.hpp> // for file_sink

#endif

using namespace std;

PIN_LOCK lock;

/*
 * Knobs for tool
 */


KNOB<BOOL> KnobProcessBuffer(KNOB_MODE_WRITEONCE, "pintool", "process_buffs", "1", "process the filled buffers");
// 256*4096=1048576 - same size buffer in memtrace_simple, membuffer_simple, membuffer_multi
KNOB<UINT32> KnobNumPagesInBuffer(KNOB_MODE_WRITEONCE, "pintool", "events", "10000", "approximate number of events to buffer");

KNOB<UINT32> KnobNumEventsInBuffer(KNOB_MODE_WRITEONCE, "pintool", "events", "10000", "approximate number of events to buffer");

#define PADSIZE 64
class thread_data_t {
public:
	
#ifdef COMPRESS_STREAM
	boost::iostreams::filtering_ostream ThreadStream;
#else
	ofstream ThreadStream;
#endif
	UINT8 _pad[PADSIZE];
};
std::vector<thread_data_t*> localStore;

int pagesize;

/* Struct of memory reference written to the buffer
 */
struct MEMREF
{
    BOOL read;
    ADDRINT ea;
};

struct MEMCNT {
	int read;
	int write;
};

// The buffer ID returned by the one call to PIN_DefineTraceBuffer
BUFFER_ID bufId;

// the Pin TLS slot that an application-thread will use to hold the APP_THREAD_REPRESENTITVE
// object that it owns
TLS_KEY appThreadRepresentitiveKey;
struct timeval start;

UINT32 totalBuffersFilled = 0;
UINT64 totalElementsProcessed = 0;

/*
 *
 * APP_THREAD_REPRESENTITVE
 *
 * Each application thread, creates an object of this class and saves it in it's Pin TLS
 * slot (appThreadRepresentitiveKey).
 */
class APP_THREAD_REPRESENTITVE
{
 
  public:
    APP_THREAD_REPRESENTITVE(THREADID tid);
    ~APP_THREAD_REPRESENTITVE();

    VOID ProcessBuffer(VOID *buf, UINT64 numElements);
    UINT32 NumBuffersFilled() {return _numBuffersFilled;}

    UINT32 NumElementsProcessed() {return _numElementsProcessed;}

  private:
    UINT32 _numBuffersFilled;
    UINT32 _numElementsProcessed;
    
};


APP_THREAD_REPRESENTITVE::APP_THREAD_REPRESENTITVE(THREADID tid)
{
    _numBuffersFilled = 0;
    _numElementsProcessed = 0;
}



APP_THREAD_REPRESENTITVE::~APP_THREAD_REPRESENTITVE()
{
}


//http://stackoverflow.com/questions/2333728/stdmap-default-value
template <typename K, typename V>
V GetWithDef(const  std::map <K,V> & m, const K & key, const V & defval ) {
	typename std::map<K,V>::const_iterator it = m.find( key );
	if ( it == m.end() ) {
		return defval;
	} else {
		return it->second;
	}
}


VOID APP_THREAD_REPRESENTITVE::ProcessBuffer(VOID *buf, UINT64 numElements)
{

     //printf ("numElements processed %d\n", (UINT32)numElements);
}




/*
 * Insert code to write data to a thread-specific buffer for instructions
 * that access memory.
 */
VOID Trace(TRACE trace, VOID *v)
{
    // Insert a call to record the effective address.
    for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl=BBL_Next(bbl))
    {
        for(INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins=INS_Next(ins))
        {
            UINT32 memOperands = INS_MemoryOperandCount(ins);

            // Iterate over each memory operand of the instruction.
            for (UINT32 memOp = 0; memOp < memOperands; memOp++)
            {
				if (INS_MemoryOperandIsRead(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                             IARG_BOOL, TRUE, offsetof(struct MEMREF, read),
                                             IARG_MEMORYOP_EA, memOp, offsetof(struct MEMREF, ea),
		                                     IARG_END);
				}
				if (INS_MemoryOperandIsWritten(ins, memOp)) {
                    INS_InsertFillBuffer(ins, IPOINT_BEFORE, bufId,
                                             IARG_BOOL, FALSE, offsetof(struct MEMREF, read),
                                             IARG_MEMORYOP_EA, memOp, offsetof(struct MEMREF, ea),
		                                  IARG_END);
				}
            }
        }
    }
}


/**************************************************************************
 *
 *  Callback Routines
 *
 **************************************************************************/

/*!
 * Called when a buffer fills up, or the thread exits, so we can process it or pass it off
 * as we see fit.
 * @param[in] id		buffer handle
 * @param[in] tid		id of owning thread
 * @param[in] ctxt		application context
 * @param[in] buf		actual pointer to buffer
 * @param[in] numElements	number of records
 * @param[in] v			callback value
 * @return  A pointer to the buffer to resume filling.
 */
VOID * BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT *ctxt, VOID *buf,
                  UINT64 numElements, VOID *v)
{
	thread_data_t* tdata = localStore[tid];
	#ifdef COMPRESS_STREAM
		boost::iostreams::filtering_ostream& ThreadStream = tdata->ThreadStream;
	#else
		ofstream& ThreadStream = tdata->ThreadStream;
	#endif
		int cpuid = sched_getcpu();
		struct timeval stamp;
		gettimeofday(&stamp, NULL);
		// print core and time stamp
		ThreadStream << cpuid << "\t" << stamp.tv_sec - start.tv_sec << "\t" << stamp.tv_usec << endl;
  //  APP_THREAD_REPRESENTITVE * appThreadRepresentitive = static_cast<APP_THREAD_REPRESENTITVE*>( PIN_GetThreadData( appThreadRepresentitiveKey, tid ) );


//	_numBuffersFilled++;
    //printf ("numElements %d\n", (UINT32)numElements);
    
    if (!KnobProcessBuffer )
    {
        return buf;
    }
    if (numElements < 1) {
		return buf;
	}
	
    struct MEMREF * memref=(struct MEMREF*)buf;
    std::map<void*, MEMCNT> pages;
	UINT64 until = numElements;
    for(UINT64 i=0; i<until; i++, memref++)
    {
		void* page = (void*)((unsigned long long)(memref->ea) & ~(pagesize-1));
		std::map<void*, MEMCNT>::iterator it = pages.find( page );
		if ( it == pages.end() ) {
			MEMCNT tmp;
			tmp.read = 0;
			tmp.write = 0;
			pages[page] = tmp;
			it = pages.find( page );
		} 
		MEMCNT& pageCnt = it->second;
		if (memref->read) {
			pageCnt.read += 1;
		} else {
			pageCnt.write += 1;
		}
        
    }
	for (std::map<void*, MEMCNT>::iterator it = pages.begin(); it != pages.end(); it++) {
		int status[1];
		status[0]=-1;
		void * ptr_to_check = it->first;
		move_pages(0 /*self memory */, 1, &ptr_to_check,  NULL, status, 0);

		ThreadStream << ((unsigned long long)(it->first))/pagesize << "\t" << status[0] << "\t" << it->second.read << "\t" << it->second.write << "\n";
	}
  //  _numElementsProcessed += (UINT32)until;
    
    return buf;
}




VOID ThreadStart(THREADID tid, CONTEXT *ctxt, INT32 flags, VOID *v)
{
	GetLock(&lock, tid+1);
    // There is a new APP_THREAD_REPRESENTITVE for every thread.
    APP_THREAD_REPRESENTITVE * appThreadRepresentitive = new APP_THREAD_REPRESENTITVE(tid);

    // A thread will need to look up its APP_THREAD_REPRESENTITVE, so save pointer in TLS
    PIN_SetThreadData(appThreadRepresentitiveKey, appThreadRepresentitive, tid);

	localStore.resize(tid+1);
	localStore[tid] = new thread_data_t();
	thread_data_t* tdata = localStore[tid];
		char file[80];
	#ifdef COMPRESS_STREAM
		sprintf(file, "thread_%i.dat.gz", tid);
		boost::iostreams::filtering_ostream& ThreadStream = tdata->ThreadStream;
		ThreadStream.push(boost::iostreams::gzip_compressor());
		ThreadStream.push(boost::iostreams::file_sink(file, ios_base::out | ios_base::binary));
	#else
		sprintf(file, "thread_%i.dat", tid);
		tdata->ThreadStream.open(file);
	#endif
		ReleaseLock(&lock);
}


VOID ThreadFini(THREADID tid, const CONTEXT *ctxt, INT32 code, VOID *v)
{
    APP_THREAD_REPRESENTITVE * appThreadRepresentitive = static_cast<APP_THREAD_REPRESENTITVE*>(PIN_GetThreadData(appThreadRepresentitiveKey, tid));
    totalBuffersFilled += appThreadRepresentitive->NumBuffersFilled();
    totalElementsProcessed +=  appThreadRepresentitive->NumElementsProcessed();

    delete appThreadRepresentitive;

    PIN_SetThreadData(appThreadRepresentitiveKey, 0, tid);

		thread_data_t* tdata = localStore[tid];
	#ifdef COMPRESS_STREAM
		boost::iostreams::filtering_ostream& ThreadStream = tdata->ThreadStream;
		boost::iostreams::close(ThreadStream);
	#else
		tdata->ThreadStream.close();
	#endif
}

VOID Fini(INT32 code, VOID *v)
{
    printf ("totalBuffersFilled %u  totalElementsProcessed %14.0f\n", (totalBuffersFilled),  
           static_cast<double>(totalElementsProcessed));
}

INT32 Usage()
{
    printf( "This tool demonstrates simple pin-tool buffer managing\n");
    printf ("The following command line options are available:\n");
    printf ("-num_pages_in_buffer <num>   :number of (4096byte) pages allocated in each buffer,         default 256\n");
    printf ("-process_buffs <0 or 1>      :specify 0 to disable processing of the buffers,              default   1\n");
    return -1;
}


/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

	pagesize = getpagesize();
    // Initialize the pin lock
	InitLock(&lock);
    // Initialize the memory reference buffer
    //printf ("buffer size in bytes 0x%x\n", KnobNumPagesInBuffer.Value()*4096);
    //	fflush (stdout);
    
	UINT32 bufferPages = (UINT32) ((KnobNumEventsInBuffer * sizeof(MEMREF)) / pagesize);
	if (bufferPages == 0) {
		bufferPages = 1;
	}

    bufId = PIN_DefineTraceBuffer(sizeof(struct MEMREF), bufferPages,
                                  BufferFull, 0);

    if(bufId == BUFFER_ID_INVALID)
    {
        printf ("Error: could not allocate initial buffer\n");
        return 1;
    }

    // Initialize thread-specific data not handled by buffering api.
    appThreadRepresentitiveKey = PIN_CreateThreadDataKey(0);
   
    // add an instrumentation function
    TRACE_AddInstrumentFunction(Trace, 0);

    // add callbacks
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);
    PIN_AddFiniFunction(Fini, 0);


	gettimeofday(&start, NULL);
    // Start the program, never returns
    PIN_StartProgram();
    
    return 0;
}


