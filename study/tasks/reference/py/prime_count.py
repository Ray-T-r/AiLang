import sys
n=int(sys.stdin.read().split('\n')[0])
if n<2: print(0)
else:
    s=bytearray([1])*(n+1); s[0]=s[1]=0; i=2
    while i*i<=n:
        if s[i]: s[i*i::i]=bytearray(len(s[i*i::i]))
        i+=1
    print(sum(s))
