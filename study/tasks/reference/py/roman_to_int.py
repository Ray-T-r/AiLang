import sys
s=sys.stdin.read().split('\n')[0]
v={'I':1,'V':5,'X':10,'L':50,'C':100,'D':500,'M':1000}; t=0
for i,c in enumerate(s):
    if i+1<len(s) and v[c]<v[s[i+1]]: t-=v[c]
    else: t+=v[c]
print(t)
