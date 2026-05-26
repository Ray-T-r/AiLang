function isPrime(n) {
    if (n < 2) return 0;
    if (n === 2) return 1;
    if (n % 2 === 0) return 0;
    let i = 3;
    while (i*i <= n) {
        if (n % i === 0) return 0;
        i += 2;
    }
    return 1;
}

let c = 0;
for (let k = 2; k <= 500000; k++) c += isPrime(k);
console.log(c);
