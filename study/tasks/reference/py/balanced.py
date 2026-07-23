import sys
s=sys.stdin.read().split('\n')[0]
st=[]; pair={')':'(',']':'[','}':'{'}; ok=True
for c in s:
    if c in '([{': st.append(c)
    elif c in ')]}':
        if not st or st[-1]!=pair[c]: ok=False; break
        st.pop()
print('YES' if ok and not st else 'NO')
