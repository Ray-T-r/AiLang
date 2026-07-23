import sys
d=sys.stdin.read().split('\n')
k=int(d[0]); s=d[1] if len(d)>1 else ''
print(''.join(chr((ord(c)-97+k)%26+97) for c in s))
