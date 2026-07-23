import sys
from collections import Counter
c=Counter(sys.stdin.read().split())
print(sorted(c.items(), key=lambda kv:(-kv[1], kv[0]))[0][0])
