seed = "the quick brown fox jumps over the lazy dog "
text = seed * 500000
words = text.split(" ")
counts = {}
for w in words:
    counts[w] = counts.get(w, 0) + 1
print(len(counts))
print(counts["the"])
