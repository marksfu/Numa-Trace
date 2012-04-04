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
typedef int timeWindow_t;
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
map<timeWindow_t, map<Node_t, map<Node_t, readWrite_t> > > timeWindows;
map<Node_t, readWrite_t>* activeSourceNode(NULL);


void processMemoryEntry(pageID_t page, Node_t numaID, int reads, int writes) {
    assert(activeSourceNode != NULL && "time window not set");
    (*activeSourceNode)[numaID].writes += writes;
    (*activeSourceNode)[numaID].reads += reads;
}

void processThreadEntry(int pid) {
    // Do nothing, only care about nodes
}

void processTimeStampEntry(Core_t core, int sec, int usec, const map<Core_t, Node_t> numaMap) {
    unsigned long long time = MILLION*sec + usec;
    timeWindow_t activeTimeWindow = (timeWindow_t)(time / timeWindowLength);
    auto it = numaMap.find(core);
    if (it == numaMap.end()) {
	cerr << "Core not found in numa map" << endl;
	exit(-1);
    }
    Node_t sourceNode = it->second;
    activeSourceNode = &(timeWindows[activeTimeWindow][sourceNode]);
}

void processInputStream(const map<Core_t, Node_t> numaMap) {
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
	    processTimeStampEntry((Core_t)word1, otherWords[0], otherWords[1], numaMap);
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
    cout << "frame" << '\t' << "sourceNode" << '\t' << "destNode" << '\t' << "reads" << '\t' << "writes" << endl;
    for (auto& frame : timeWindows) {
	for (auto& sourceNode : frame.second) {
	    for (auto& destNode : sourceNode.second) {
		// frame# sourceNode destNode reads writes
		auto& rw = destNode.second; 
		cout << frame.first << '\t' << sourceNode.first << '\t' << destNode.first << '\t' << rw.reads << '\t' << rw.writes << endl;
	    } 
	}
    }
}



/**
 * Initializes NUMA layout from configuration file.
 * Format:
 * node\tcore,core,core,...
 * node\tcore,core,core,...
 */
void loadNumaConfigurationFile(const char* filename, map<Core_t, Node_t>* _numaMap) {
    auto& numaMap = *_numaMap;
    ifstream numaFile(filename);
    string line;
    if (!numaFile.is_open()) {
	cerr << "Unable to open numa configuration file" << endl;
	exit(-1);
    }
    while (numaFile.good()) {
	getline(numaFile, line);
	if (line.length() < 1) {
	    continue;
	}
	string nodeStr = line.substr(0,line.find('\t'));
	Node_t n = (Node_t)atoi(nodeStr.c_str());
	string coresLineStr = line.substr(line.find('\t')+1);
	
	auto cores = split(coresLineStr, ',');
	for (auto cStr : cores) {
	    Core_t c = (Core_t)atoi(cStr.c_str());
	    numaMap[c] = n;
	    //cout << c << "\t" << n << endl;
	}
	
    }
    numaFile.close();
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
	cerr << "Error no configuration file given" << endl;
	exit(-1);
    }
    map<Core_t, Node_t> numaMap;
    loadNumaConfigurationFile(argv[1], &numaMap);
    processInputStream(numaMap);
    printOutput();
}

