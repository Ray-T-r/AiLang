import sys
from collections import defaultdict
g=defaultdict(list)
for x in sys.stdin.read().split(): g[''.join(sorted(x))].append(x)
groups=sorted((sorted(v) for v in g.values()), key=lambda v:v[0])
print('\n'.join(' '.join(v) for v in groups))
