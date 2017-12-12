import os
import sys


content = open(sys.argv[1], 'r').readlines()

names = dict()

for line in content:
    if "|" not in line or "#" in line:
        continue
    line = line.split("|")
    name = line[1]
    try:
        names[name] += 1
    except:
        #print name
        names[name] = 1
result = sorted( ((v,k) for k,v in names.iteritems()), reverse=True)
for count, element in result:
    if count > 1:
        print "\"%s\":\"%s\", " % (count, element)