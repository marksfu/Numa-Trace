import sys, collections, os, gzip

[configFileName, inputDir] = sys.argv[1:3]
configFile = open(configFileName)
nodeMapping = dict()
nodes = set()

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

nodeWritePages = collections.defaultdict(set)
nodeReadPages = collections.defaultdict(set)

def processDataFile(inputFileName):
	writeCount = collections.defaultdict(int)
	readCount = collections.defaultdict(int)
	inputFile = gzip.open(inputFileName)
	for line in inputFile:
		parts = line[:-1].split("\t")
		if len(parts) == 3:
			[cpu, _, _] = parts
		elif len(parts) == 4:
			[page, node, reads, writes] = parts
			if int(node) >= 0:
				writeCount[(nodeMapping[cpu], node)] += int(writes)
				if int(writes) > 0:
					nodeWritePages[node].add(page)
				readCount[(nodeMapping[cpu], node)] += int(reads)
				if int(reads) > 0:
					nodeReadPages[node].add(page)
	tid = threadFromFile(inputFileName)
	for threadNode in nodes:
		for dataNode in nodes:
			writes = writeCount[(threadNode,dataNode)]
			print "\t".join([tid, threadNode, dataNode, "w", str(writes)])
			reads = readCount[(threadNode,dataNode)]
			print "\t".join([tid, threadNode, dataNode, "r", str(reads)])



print "----BEGIN THREAD SUMMARY----"
print "\t".join(["thread", "threadNode","dataNode","rw","value"])
for filename in os.listdir(inputDir):
	processDataFile(inputDir + "/" + filename)
print "----END THREAD SUMMARY----"
print
print "----BEGIN NODE SIZE SUMMARY----"
print "\t".join(["node","rw","pages"])
for node in nodeWritePages:
	print "\t".join([node,"w", str(len(nodeWritePages[node]))])
for node in nodeReadPages:
	print "\t".join([node,"r", str(len(nodeReadPages[node]))])
print "----END NODE SIZE SUMMARY----"


