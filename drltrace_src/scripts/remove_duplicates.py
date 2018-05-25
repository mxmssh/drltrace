import os
import sys


content = open(sys.argv[1], 'r').readlines()

names = dict()

for line in content:
    line = line[:-1]
    if "|" not in line or "#" in line:
        print(line)
        continue
    name = line.split("|")[1]
    try:
        names[name] += 1
        continue
    except:
        #print name
        names[name] = 1
    print(line)

