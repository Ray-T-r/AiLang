//! AiLang runtime — linked into every compiled program.
//!
//! In M3 this becomes a `no_std` staticlib providing GC init, panic handlers,
//! and print intrinsics. For M0 it's a placeholder so the workspace builds.
