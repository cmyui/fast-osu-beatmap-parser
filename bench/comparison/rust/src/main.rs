use std::{env, fs, hint::black_box, io::{self, BufRead, Write}, time::Instant};
use serde_json::{json, Value};

fn parse_and_count(name: &str, data: &[u8], path: &str, file: bool) -> io::Result<usize> {
    if name == "rosu-map" {
        let map: rosu_map::Beatmap = if file {
            rosu_map::from_path(path)?
        } else {
            rosu_map::from_bytes(data)?
        };
        let count = black_box(&map).hit_objects.len();
        drop(map);
        Ok(count)
    } else {
        let map = if file { rosu_pp::Beatmap::from_path(path)? }
                  else { rosu_pp::Beatmap::from_bytes(data)? };
        let count = black_box(&map).hit_objects.len();
        drop(map);
        Ok(count)
    }
}

fn run(name: &str, request: &Value) -> io::Result<Value> {
    let path = request["path"].as_str().unwrap();
    let file = request["workload"] == "file";
    let data = fs::read(path)?;
    let mut count = 0;
    let mut samples = Vec::new();
    for _ in 0..request["reps"].as_u64().unwrap() {
        let start = Instant::now();
        count = black_box(parse_and_count(name, &data, path, file)?);
        samples.push(start.elapsed().as_nanos() as u64);
    }
    Ok(json!({"count": count, "ns": samples}))
}

fn main() {
    let name = env::args().nth(1).unwrap();
    assert!(name == "rosu-map" || name == "rosu-pp");
    for line in io::stdin().lock().lines() {
        let request: Value = serde_json::from_str(&line.unwrap()).unwrap();
        let response = match run(&name, &request) {
            Ok(value) => value,
            Err(error) => json!({"error": "DecodeError", "detail": error.to_string()}),
        };
        println!("{response}");
        io::stdout().flush().unwrap();
    }
}
