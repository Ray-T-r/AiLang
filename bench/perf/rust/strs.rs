fn main() {
    let mut acc: String = String::from("x");
    for _ in 1..=100000 {
        acc = acc + "y";
    }
    println!("{}", acc.len());
}
