use serde_json::{json, Value};
use std::{
    env, fs,
    hint::black_box,
    io::{self, BufRead, Write},
    time::Instant,
};

fn parse_and_count(data: &[u8], path: &str, file: bool) -> io::Result<usize> {
    let map: rosu_map::Beatmap = if file {
        rosu_map::from_path(path)?
    } else {
        rosu_map::from_bytes(data)?
    };
    let count = black_box(&map).hit_objects.len();
    drop(map);
    Ok(count)
}

fn run(request: &Value) -> io::Result<Value> {
    let path = request["path"].as_str().unwrap();
    let file = request["workload"] == "file";
    let data = fs::read(path)?;
    let mut count = 0;
    let mut samples = Vec::new();
    for _ in 0..request["reps"].as_u64().unwrap() {
        let start = Instant::now();
        count = black_box(parse_and_count(&data, path, file)?);
        samples.push(start.elapsed().as_nanos() as u64);
    }
    Ok(json!({"count": count, "ns": samples}))
}

fn main() {
    let name = env::args().nth(1).unwrap();
    assert!(name == "rosu-map");
    for line in io::stdin().lock().lines() {
        let request: Value = serde_json::from_str(&line.unwrap()).unwrap();
        let response = match run(&request) {
            Ok(value) => value,
            Err(error) => json!({"error": "DecodeError", "detail": error.to_string()}),
        };
        println!("{response}");
        io::stdout().flush().unwrap();
    }
}
