import sys, collections, os, gzip

MILLION = 1000000

[configFileName, inputDir, timeStep, smooth] = sys.argv[1:5]

timeStep = int(timeStep)
configFile = open(configFileName)
nodeMapping = dict()
nodes = set()

class Node:
	def __init__(self):
		self.writes = 0
		self.reads = 0

class Core:
	def __init__(self):
		self.node = collections.defaultdict(Node)

class Thread:
	def __init__(self):
		self.core = collections.defaultdict(Core)
	
class Frame:
	def __init__(self):
		self.nodeWritePages = collections.defaultdict(set)
		self.nodeReadPages = collections.defaultdict(set)
		self.nodeWrites = collections.defaultdict(int)
		self.nodeReads = collections.defaultdict(int)
		self.thread = collections.defaultdict(Thread)
	

frame = collections.defaultdict(Frame)

for line in configFile:
	[node, corelist] = line[:-1].split("\t")
	nodes.add(node)
	cores = corelist.split(",")
	for c in cores:
		nodeMapping[c] = node

def threadFromFile(filename):
	num = filename[filename.find("_")+1:]
	num = num[0:num.find(".")]
	return num

def processDataFile(inputFileName):
	inputFile = gzip.open(inputFileName)
	tid = threadFromFile(inputFileName)
	for line in inputFile:
		parts = line[:-1].split("\t")
		if len(parts) == 3:
			[cpu, sec, usec] = parts
			f = (int(sec)*MILLION+int(usec))/timeStep
		elif len(parts) == 4:
			[page, node, reads, writes] = parts
			cpu_node = nodeMapping[cpu]
			if int(node) >= 0:
				if int(writes) > 0:
					frame[f].nodeWritePages[node].add(page)
					#frame[f].thread[tid].core[cpu].node[cpu_node].writes += int(writes)
					frame[f].nodeWrites[(cpu_node, node)] += int(writes)
				if int(reads) > 0:
					frame[f].nodeReadPages[node].add(page)
					#frame[f].thread[tid].core[cpu].node[cpu_node].reads += int(reads)
					frame[f].nodeReads[(cpu_node, node)] += int(reads)

for filename in os.listdir(inputDir):
	processDataFile(inputDir + "/" + filename)

smooth = int(smooth)
for i in range(len(frame) - 1):
	for k in range(smooth):
		aIdx = i
		bIdx = i+1
		f = i*smooth + k
		print f
		for n in nodes:
			aReads = len(frame[aIdx].nodeReadPages[n]) 
			bReads = len(frame[bIdx].nodeReadPages[n]) 
			aWrites = len(frame[aIdx].nodeWritePages[n])
			bWrites = len(frame[bIdx].nodeWritePages[n])
			print n, "r", (aReads*(smooth-k) + bReads*k)/smooth
			print n, "w", (aWrites*(smooth-k) + bWrites*k)/smooth
		for cpuNode in nodes:
			for memNode in nodes:
				aReads = frame[aIdx].nodeReads[(cpuNode, memNode)]
				bReads = frame[bIdx].nodeReads[(cpuNode, memNode)]
				aWrites = frame[aIdx].nodeWrites[(cpuNode, memNode)]
				bWrites = frame[bIdx].nodeWrites[(cpuNode, memNode)]
				print cpuNode, memNode, "r", (aReads*(smooth-k) + bReads*k)/smooth
				print cpuNode, memNode, "w", (aWrites*(smooth-k) + bWrites*k)/smooth


