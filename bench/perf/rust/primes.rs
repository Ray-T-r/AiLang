fn is_prime(n: i64) -> i64 {
    if n < 2 { return 0; }
    if n == 2 { return 1; }
    if n % 2 == 0 { return 0; }
    let mut i: i64 = 3;
    while i*i <= n {
        if n % i == 0 { return 0; }
        i += 2;
    }
    1
}

fn main() {
    let mut c: i64 = 0;
    for k in 2..=500000i64 {
        c += is_prime(k);
    }
    println!("{}", c);
}
