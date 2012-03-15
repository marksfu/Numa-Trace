#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <map>
#include <bitset>


#define MAX_LINE 100
#define MAX_OTHER_WORDS 3

#define MILLION 1000000
#define DEFAULT_TIME_WINDOW_LENGTH_uS 1000000
#define MAX_THREADS 32

typedef unsigned long long pageID_t;
typedef int timeWindow_t;


using namespace std;

struct PageRecords_t {
    map<pageID_t, bitset<MAX_THREADS> > readByThreads;
    map<pageID_t, bitset<MAX_THREADS> > writeByThreads;
};

int timeWindowLength(DEFAULT_TIME_WINDOW_LENGTH_uS);
int activeThread(-1);
timeWindow_t activeTimeWindow(-1);
map<timeWindow_t, PageRecords_t> timeWindows;
PageRecords_t* activePageRecords;

void processMemoryEntry(pageID_t page, int numaID, int reads, int writes) {
    assert(activeTimeWindow >= 0 && "time window not set");
    auto& writeByThreads = (*activePageRecords).writeByThreads;
    auto& readByThreads = (*activePageRecords).readByThreads;
    
    if (writes > 0) {
	writeByThreads[page][activeThread] = 1;
	readByThreads[page][activeThread] = 1;
    } else if (reads > 0) {
	readByThreads[page][activeThread] = 1;
    } else {
	assert(0 && "memory entry should have at least 1 read or write");
    }
}

void processThreadEntry(int pid) {
    assert((pid < MAX_THREADS) && "pid greater than bit count");
    activeThread = pid;
}

void processTimeStampEntry(int core, int sec, int usec) {
    assert((activeThread >= 0) && "thread id is not set");
    unsigned long long time = MILLION*sec + usec;
    activeTimeWindow = (timeWindow_t)(time / TIME_WINDOW_LENGTH_uS);
    activePageRecords = &(timeWindows[activeTimeWindow]);
}

int main() {
    char input_line[MAX_LINE];
    char *result;

    while((result = fgets(input_line, MAX_LINE, stdin )) != NULL) {
	uint64_t word1;
	int otherWords[MAX_OTHER_WORDS];
	word1 = atol(input_line);
	int i = 0;
	for (int w = 0; w < MAX_OTHER_WORDS; w++) {
	    i++;
	    while ((input_line[i] != '\t') && (i < MAX_LINE)) {
		i++;
	    }
	    assert((i < MAX_LINE) && "i < MAX_LINE");
	    otherWords[w] = atoi(input_line + i);
	}
	//cout << word1 << '\t' << otherWords[0] << '\t' << otherWords[1] << '\t' << otherWords[2] << endl;
	if (otherWords[2] /* 4th column */ != -1) {
	    processMemoryEntry((pageID_t)word1, otherWords[0], otherWords[1], otherWords[2]);
	} else if (otherWords[1] /* 3rd column */ != -1) {
	    processTimeStampEntry((int)word1, otherWords[0], otherWords[1]);
	} else {
	    assert((otherWords[0] == -1) && "2nd column should be -1");
	    processThreadEntry((int)word1);
	}
	
	//cout << word1 << '\t' << otherWords[0] << '\t' << otherWords[1] << '\t' << otherWords[2] << endl;
    }
	
    if (ferror(stdin))
	perror("Error reading stdin.");

    cout << "Time Frame\tPages Read\tPages Written\tPrivate Read Only\tShared Read Only\tPrivate Write\tShared Write" << endl;
    for (auto timeFrame : timeWindows) {
	int privateWrite = 0;
	int privateRead = 0;
	int sharedWrite = 0;
	int sharedRead = 0;
	
	auto frameID = timeFrame.first;
	auto& pageEntries = timeFrame.second;
	// must store here as [] accessor creates entry if none present;
	int pageReads =  pageEntries.readByThreads.size();
	int pageWrites = pageEntries.writeByThreads.size();
	
	for (auto threadWrites : pageEntries.writeByThreads) {
	    auto& pageID = threadWrites.first;
	    auto& threadsWriteToPage = threadWrites.second;
	    auto& threadsReadToPage = pageEntries.readByThreads[pageID];
	    if (threadsWriteToPage.count() > 0) { // this should always be true
		int activeThreads = (threadsWriteToPage | threadsReadToPage).count();
		if (activeThreads == 1) {
		    privateWrite++;
		} else {
		    sharedWrite++;
		}
	    } 
	}
	for (auto threadReads : pageEntries.readByThreads) {
	    auto& pageID = threadReads.first;
	    auto& threadsReadToPage = threadReads.second;
	    if (threadsReadToPage.count() > 0) {
		if (pageEntries.writeByThreads[pageID].count() > 0) {
		    continue;
		} else {
		    if (threadsReadToPage.count() == 1) {
			privateRead++;
		    } else {
			sharedRead++;
		    }
		}
	    }
	}
	cout << frameID << '\t' << pageReads << '\t' << pageWrites << '\t';
	cout << privateRead << '\t' << sharedRead << '\t' << privateWrite << '\t' << sharedWrite  << endl;
    }
}

