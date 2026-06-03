// C++ shim for examples-selfhost/cppinterop.ail. Compiled with clang++ (pulled
// in by the `csrc "shim.cpp"` directive) and linked by the clang++ driver, so
// the C++ standard library is available. Every entry point is `extern "C"`, so
// it has a plain C ABI (no name mangling) that the AiLang side binds to with a
// bare `ex fn` — no header needed, the extern prototype is self-sufficient.
//
// Opaque C++ objects cross the FFI boundary as int64_t handles, exactly like the
// AiLang prelude's own tls_/pg_ handles. Boehm GC does NOT manage `new`-allocated
// memory, so the AiLang side must call Acc_free() to avoid a leak.
#include <cstdint>
#include <algorithm>

extern "C" int64_t cpp_max3(int64_t a, int64_t b, int64_t c) {
    return std::max(a, std::max(b, c));   // a real C++ stdlib call
}

namespace {
struct Accumulator { int64_t total = 0; };   // a stateful C++ object
}

extern "C" int64_t Acc_new(void)                 { return reinterpret_cast<int64_t>(new Accumulator()); }
extern "C" void    Acc_add(int64_t h, int64_t x) { reinterpret_cast<Accumulator*>(h)->total += x; }
extern "C" int64_t Acc_sum(int64_t h)            { return reinterpret_cast<Accumulator*>(h)->total; }
extern "C" void    Acc_free(int64_t h)           { delete reinterpret_cast<Accumulator*>(h); }
