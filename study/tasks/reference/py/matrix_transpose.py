import sys
d=sys.stdin.read().split('\n'); r,c=map(int,d[0].split())
m=[list(map(int,d[1+i].split())) for i in range(r)]
for j in range(c):
    print(' '.join(str(m[i][j]) for i in range(r)))
