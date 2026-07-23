import sys
data=sys.stdin.read()
line=data.split('\n')[0] if data else ''
out=[]; i=0
while i<len(line):
    j=i
    while j<len(line) and line[j]==line[i]: j+=1
    out.append(line[i]+str(j-i)); i=j
print(''.join(out))
