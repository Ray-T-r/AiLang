fn main() {
    let mut s: i64 = 0;
    for i in 1i64..=100_000_000i64 {
        s = s.wrapping_add(i);
    }
    println!("{}", s);
}
