let count = 0;
let total = 2;
for (let i = 0; i < 50000; i++) {
    const age = 18 + i % 52;
    if (age >= 40) {
        const rec = `{"id":${i},"name":"user_${i}","age":${age}}`;
        if (count > 0) total += 1;
        total += rec.length;
        count += 1;
    }
}
console.log(count);
console.log(total);
