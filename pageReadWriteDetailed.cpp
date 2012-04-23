#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sstream>

#include <map>
#include <vector>


#define MAX_LINE 100
#define MAX_OTHER_WORDS 3

#define MILLION 1000000
#define DEFAULT_TIME_WINDOW_LENGTH_uS 1000000


using namespace std;

typedef unsigned long long pageID_t;
typedef unsigned long long timeIndex_t;
typedef uint Core_t;
typedef	uint Node_t;
struct readWrite_t{
    uint reads;
    uint writes;
};

std::vector<std::string> &split(const std::string &s, char delim, std::vector<std::string> &elems) {
    std::stringstream ss(s);
    std::string item;
    while(std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}


std::vector<std::string> split(const std::string &s, const char delim) {
    std::vector<std::string> elems;
    return split(s, delim, elems);
}




int timeWindowLength(DEFAULT_TIME_WINDOW_LENGTH_uS);
map<timeIndex_t, map<pageID_t, readWrite_t> >  timeWindows;
map<pageID_t, readWrite_t>* activeTimeWindow;


void processMemoryEntry(pageID_t page, Node_t numaID, int reads, int writes) {
    assert(activeTimeWindow != NULL && "time window not set");
    (*activeTimeWindow)[page].writes += writes;
    (*activeTimeWindow)[page].reads += reads;
}

void processThreadEntry(int pid) {
    // Do nothing, only care about nodes
}

void processTimeStampEntry(Core_t core, int sec, int usec) {
    unsigned long long time = MILLION*sec + usec;
    timeIndex_t activeTimeIndex = (timeIndex_t)(time / timeWindowLength);
    activeTimeWindow = &(timeWindows[activeTimeIndex]);
}

void processInputStream() {
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
	    processMemoryEntry((pageID_t)word1, (Node_t)otherWords[0], otherWords[1], otherWords[2]);
	} else if (otherWords[1] /* 3rd column */ != -1) {
	    processTimeStampEntry((Core_t)word1, otherWords[0], otherWords[1]);
	} else {
	    assert((otherWords[0] == -1) && "2nd column should be -1");
	    processThreadEntry((int)word1);
	}
	
	//cout << word1 << '\t' << otherWords[0] << '\t' << otherWords[1] << '\t' << otherWords[2] << endl;
    }
    if (ferror(stdin))
	perror("Error reading stdin.");
}


void printOutput() {
    cout << "frame" << '\t' << "page" << '\t' << "reads" << '\t' << "writes" << endl;
    for (auto& frame : timeWindows) {
	auto& timeStamp = frame.first;
	auto& pages = frame.second;
	for (auto& page : pages) {
	    auto pageAddress = page.first;
	    auto& rw = page.second; 
	    cout << timeStamp << '\t' << pageAddress << '\t' <<  rw.reads << '\t' << rw.writes << endl;
	}    
    }
}




int main(int argc, char* argv[]) {
    processInputStream();
    printOutput();
}

