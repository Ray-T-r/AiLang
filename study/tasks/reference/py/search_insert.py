import sys
d=sys.stdin.read().split('\n')
a=list(map(int,d[0].split())) if d[0].strip() else []
t=int(d[1]); lo,hi=0,len(a)
while lo<hi:
    m=(lo+hi)//2
    if a[m]<t: lo=m+1
    else: hi=m
print(lo)
