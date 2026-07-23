import sys
d=sys.stdin.read().split('\n'); n=int(d[0]); iv=[]
for i in range(1,n+1):
    a,b=map(int,d[i].split()); iv.append((a,b))
iv.sort(); res=[]
for a,b in iv:
    if res and a<=res[-1][1]: res[-1]=(res[-1][0], max(res[-1][1],b))
    else: res.append((a,b))
print('\n'.join(f'{a} {b}' for a,b in res))
