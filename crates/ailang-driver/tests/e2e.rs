//! End-to-end tests: take an `.ail` source, compile it, run the binary,
//! compare stdout against an `.out` fixture.
//!
//! Each test depends on the system `clang` (or `cc`) being on PATH.

use std::path::{Path, PathBuf};
use std::process::Command;

fn project_root() -> PathBuf {
    // CARGO_MANIFEST_DIR points at the crate; go up two levels to repo root.
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .to_path_buf()
}

fn run_case(name: &str) {
    let root = project_root();
    let src = root.join("examples").join(format!("{name}.ail"));
    let expected_path = root.join("tests/e2e").join(format!("{name}.out"));
    let expected = std::fs::read_to_string(&expected_path)
        .unwrap_or_else(|e| panic!("missing fixture {}: {e}", expected_path.display()));

    let out_dir = std::env::temp_dir().join("ailang-e2e");
    std::fs::create_dir_all(&out_dir).unwrap();
    let bin = out_dir.join(name);

    let opts = ailang_driver::CompileOptions {
        output: Some(bin.clone()),
        ..Default::default()
    };
    let result = ailang_driver::compile(&src, &opts)
        .unwrap_or_else(|e| panic!("compile failed for {name}: {e}"));
    let bin = result.binary.expect("expected a binary");

    let output = Command::new(&bin)
        .output()
        .unwrap_or_else(|e| panic!("could not run {}: {e}", bin.display()));
    assert!(
        output.status.success(),
        "binary exited non-zero for {name}: status={:?} stderr={}",
        output.status,
        String::from_utf8_lossy(&output.stderr),
    );

    let got = String::from_utf8(output.stdout).unwrap();
    assert_eq!(
        got, expected,
        "stdout mismatch for {name}\n--- expected ---\n{expected}--- got ---\n{got}",
    );
}

fn project_has(file: &str) -> bool {
    Path::new(&project_root().join(file)).exists()
}

#[test]
fn e2e_hello() {
    if !project_has("examples/hello.ail") { return; }
    run_case("hello");
}

#[test]
fn e2e_fib() {
    if !project_has("examples/fib.ail") { return; }
    run_case("fib");
}

#[test]
fn e2e_fizzbuzz_simple() {
    if !project_has("examples/fizzbuzz_simple.ail") { return; }
    run_case("fizzbuzz_simple");
}

#[test]
fn e2e_arith_loop() {
    if !project_has("examples/arith_loop.ail") { return; }
    run_case("arith_loop");
}

#[test]
fn e2e_fizzbuzz_with_match() {
    if !project_has("examples/fizzbuzz.ail") { return; }
    run_case("fizzbuzz");
}

#[test]
fn e2e_closure_block() {
    if !project_has("examples/closure_block.ail") { return; }
    run_case("closure_block");
}

#[test]
fn e2e_string_interp() {
    if !project_has("examples/string_interp.ail") { return; }
    run_case("string_interp");
}

#[test]
fn e2e_pipe() {
    if !project_has("examples/pipe.ail") { return; }
    run_case("pipe");
}

#[test]
fn e2e_match_pipe() {
    if !project_has("examples/match_pipe.ail") { return; }
    run_case("match_pipe");
}

#[test]
fn e2e_greet_with_string_concat_and_gc() {
    if !project_has("examples/greet.ail") { return; }
    run_case("greet");
}

#[test]
fn e2e_ffi_libc() {
    if !project_has("examples/ffi_libc.ail") { return; }
    run_case("ffi_libc");
}

#[test]
fn e2e_arrays() {
    if !project_has("examples/arrays.ail") { return; }
    run_case("arrays");
}

#[test]
fn e2e_maps() {
    if !project_has("examples/maps.ail") { return; }
    run_case("maps");
}

#[test]
fn e2e_stdlib_demo() {
    if !project_has("examples/stdlib_demo.ail") { return; }
    run_case("stdlib_demo");
}

#[test]
fn e2e_io_demo() {
    if !project_has("examples/io_demo.ail") { return; }
    run_case("io_demo");
}

#[test]
fn e2e_strings_demo() {
    if !project_has("examples/strings_demo.ail") { return; }
    run_case("strings_demo");
}

#[test]
fn e2e_word_count() {
    if !project_has("examples/word_count.ail") { return; }
    run_case("word_count");
}

#[test]
fn e2e_map_inference() {
    if !project_has("examples/map_inference.ail") { return; }
    run_case("map_inference");
}

#[test]
fn e2e_structs_demo() {
    if !project_has("examples/structs_demo.ail") { return; }
    run_case("structs_demo");
}

#[test]
fn e2e_struct_positional() {
    if !project_has("examples/struct_positional.ail") { return; }
    run_case("struct_positional");
}

#[test]
fn e2e_auto_import_demo() {
    if !project_has("examples/auto_import_demo.ail") { return; }
    run_case("auto_import_demo");
}

#[test]
fn e2e_hof_demo() {
    if !project_has("examples/hof_demo.ail") { return; }
    run_case("hof_demo");
}

#[test]
fn e2e_map_iter_demo() {
    if !project_has("examples/map_iter_demo.ail") { return; }
    run_case("map_iter_demo");
}

#[test]
fn e2e_str_map_demo() {
    if !project_has("examples/str_map_demo.ail") { return; }
    run_case("str_map_demo");
}

#[test]
fn e2e_bytes_demo() {
    if !project_has("examples/bytes_demo.ail") { return; }
    run_case("bytes_demo");
}

#[test]
fn e2e_time_demo() {
    if !project_has("examples/time_demo.ail") { return; }
    run_case("time_demo");
}

#[test]
fn e2e_http_format_demo() {
    if !project_has("examples/http_format_demo.ail") { return; }
    run_case("http_format_demo");
}

#[test]
fn e2e_http_parse_demo() {
    if !project_has("examples/http_parse_demo.ail") { return; }
    run_case("http_parse_demo");
}

#[test]
fn e2e_json_demo() {
    if !project_has("examples/json_demo.ail") { return; }
    run_case("json_demo");
}

#[test]
fn e2e_quick_wins_demo() {
    if !project_has("examples/quick_wins_demo.ail") { return; }
    run_case("quick_wins_demo");
}

#[test]
fn e2e_qol_demo() {
    if !project_has("examples/qol_demo.ail") { return; }
    run_case("qol_demo");
}

#[test]
fn e2e_medium_demo() {
    if !project_has("examples/medium_demo.ail") { return; }
    run_case("medium_demo");
}

#[test]
fn e2e_result_demo() {
    if !project_has("examples/result_demo.ail") { return; }
    run_case("result_demo");
}

#[test]
fn e2e_adt_demo() {
    if !project_has("examples/adt_demo.ail") { return; }
    run_case("adt_demo");
}

#[test]
fn e2e_tail_if_demo() {
    if !project_has("examples/tail_if_demo.ail") { return; }
    run_case("tail_if_demo");
}
