import sys
d=sys.stdin.read().split('\n')
n=int(d[0].strip(), int(d[1])); b2=int(d[2])
digs='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
if n==0: print('0')
else:
    out=''
    while n>0: out=digs[n%b2]+out; n//=b2
    print(out)
