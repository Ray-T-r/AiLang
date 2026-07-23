import sys
s=sys.stdin.read().split('\n')[0]; toks=[]; i=0
while i<len(s):
    c=s[i]
    if c==' ': i+=1; continue
    if c.isdigit():
        j=i
        while j<len(s) and s[j].isdigit(): j+=1
        toks.append(s[i:j]); i=j
    else: toks.append(c); i+=1
pos=0
def peek():
    return toks[pos] if pos<len(toks) else None
def trunc(a,b):
    q=abs(a)//abs(b)
    return -q if (a<0)!=(b<0) else q
def expr():
    global pos; v=term()
    while peek() in ('+','-'):
        op=toks[pos]; pos+=1; r=term(); v=v+r if op=='+' else v-r
    return v
def term():
    global pos; v=fac()
    while peek() in ('*','/'):
        op=toks[pos]; pos+=1; r=fac(); v=v*r if op=='*' else trunc(v,r)
    return v
def fac():
    global pos; t=toks[pos]
    if t=='(':
        pos+=1; v=expr(); pos+=1; return v
    pos+=1; return int(t)
print(expr())
