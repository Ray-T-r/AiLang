const seed = "the quick brown fox jumps over the lazy dog ";
const text = seed.repeat(500000);
const words = text.split(" ");
const counts = new Map();
for (const w of words) counts.set(w, (counts.get(w) || 0) + 1);
console.log(counts.size);
console.log(counts.get("the"));
