import sys
d=sys.stdin.read().split('\n'); c=int(d[0]); tot=0
for line in d[1:]:
    if line.strip()=='': continue
    tot+=int(line.split(',')[c])
print(tot)
