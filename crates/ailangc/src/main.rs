//! `ailangc` — the AiLang compiler CLI.

use std::io::{self, BufWriter, Write};
use std::path::PathBuf;
use std::process::ExitCode;

use clap::{Parser, Subcommand, ValueEnum};

#[derive(Copy, Clone, Debug, ValueEnum)]
enum BackendArg {
    /// Default: AST → C99/C11, link via clang.
    C,
    /// M6+: AST → LLVM IR text (.ll). Subset only.
    Ir,
}
impl From<BackendArg> for ailang_driver::Backend {
    fn from(b: BackendArg) -> Self {
        match b {
            BackendArg::C => ailang_driver::Backend::C,
            BackendArg::Ir => ailang_driver::Backend::Ir,
        }
    }
}

#[derive(Parser)]
#[command(name = "ailangc", version, about = "AiLang compiler")]
struct Cli {
    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Dump the token stream of a source file (lexer debug aid).
    Tokens { file: PathBuf },

    /// Dump the parsed AST of a source file (parser debug aid).
    Parse { file: PathBuf },

    /// Compile a `.ail` source to a native binary.
    Compile {
        file: PathBuf,
        /// Output binary path (default: same as source without extension).
        #[arg(short, long)]
        output: Option<PathBuf>,
        /// Emit the generated source (C or LLVM IR depending on `--backend`)
        /// and stop (do not invoke the linker).
        #[arg(long)]
        emit_c: bool,
        /// Keep the generated C (or LLVM IR) file next to the binary. By
        /// default it's deleted once the native binary is built.
        #[arg(long)]
        keep_c: bool,
        /// Codegen backend. `c` (default) goes through C99; `ir` emits
        /// LLVM IR text (M6+, subset only).
        #[arg(long, value_enum, default_value_t = BackendArg::C)]
        backend: BackendArg,
        /// Print every external command run.
        #[arg(short, long)]
        verbose: bool,
    },

    /// Compile and immediately execute the result. Forwards the binary's exit code.
    Run {
        file: PathBuf,
        /// Keep the generated C (or LLVM IR) file next to the binary. By
        /// default it's deleted once the native binary is built.
        #[arg(long)]
        keep_c: bool,
        #[arg(long, value_enum, default_value_t = BackendArg::C)]
        backend: BackendArg,
        #[arg(short, long)]
        verbose: bool,
    },
}

fn main() -> ExitCode {
    let cli = Cli::parse();
    let stdout = io::stdout();
    let mut out = BufWriter::new(stdout.lock());

    let result: Result<i32, String> = match cli.cmd {
        Cmd::Tokens { file } => match ailang_driver::dump_tokens(&file, &mut out) {
            Ok(()) => Ok(0),
            Err(e) => Err(format!("{e}")),
        },
        Cmd::Parse { file } => match ailang_driver::dump_ast(&file, &mut out) {
            Ok(()) => Ok(0),
            Err(e) => Err(format!("{e}")),
        },
        Cmd::Compile {
            file,
            output,
            emit_c,
            keep_c,
            backend,
            verbose,
        } => {
            let opts = ailang_driver::CompileOptions {
                output,
                emit_c_only: emit_c,
                keep_c,
                verbose,
                backend: backend.into(),
                ..Default::default()
            };
            match ailang_driver::compile(&file, &opts) {
                Ok(res) => {
                    if emit_c {
                        let _ = out.write_all(res.c_source.as_bytes());
                    } else if let Some(b) = res.binary {
                        let _ = writeln!(out, "compiled: {}", b.display());
                    }
                    Ok(0)
                }
                Err(e) => Err(format!("{e}")),
            }
        }
        Cmd::Run {
            file,
            keep_c,
            backend,
            verbose,
        } => {
            let opts = ailang_driver::CompileOptions {
                keep_c,
                verbose,
                backend: backend.into(),
                ..Default::default()
            };
            // Flush our own buffer before exec'ing the child — otherwise its
            // output races with ours on the terminal.
            let _ = out.flush();
            match ailang_driver::run(&file, &opts) {
                Ok(code) => Ok(code),
                Err(e) => Err(format!("{e}")),
            }
        }
    };

    let _ = out.flush();
    match result {
        Ok(code) => {
            if code == 0 {
                ExitCode::SUCCESS
            } else {
                ExitCode::from(code as u8)
            }
        }
        Err(msg) => {
            eprintln!("ailangc: {msg}");
            ExitCode::FAILURE
        }
    }
}
