use std::collections::HashMap;
fn main() {
    let seed = "the quick brown fox jumps over the lazy dog ";
    let text = seed.repeat(500000);
    let words: Vec<&str> = text.split(' ').collect();
    let mut counts: HashMap<&str, i64> = HashMap::new();
    for w in &words {
        *counts.entry(*w).or_insert(0) += 1;
    }
    println!("{}", counts.len());
    println!("{}", counts["the"]);
}
