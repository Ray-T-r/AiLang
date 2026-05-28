fn main() {
    let mut count = 0i64;
    let mut total = 2i64;
    for i in 0..50000i64 {
        let age = 18 + i % 52;
        if age >= 40 {
            let rec = format!("{{\"id\":{},\"name\":\"user_{}\",\"age\":{}}}", i, i, age);
            if count > 0 { total += 1; }
            total += rec.len() as i64;
            count += 1;
        }
    }
    println!("{}", count);
    println!("{}", total);
}
