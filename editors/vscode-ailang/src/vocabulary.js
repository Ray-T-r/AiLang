// Static vocabulary for AiLang completion. Pure data — extension.js turns
// these into vscode.CompletionItem objects. Keep this in sync with the
// `builtins` / `keywords` / `types` patterns in syntaxes/ailang.tmLanguage.json.

// [keyword, one-line description]
const KEYWORDS = [
  ['fn', 'function or lambda'],
  ['if', 'conditional'],
  ['el', 'else branch'],
  ['lp', 'loop — unifies for-in and while'],
  ['rt', 'return (only needed for early return)'],
  ['mt', 'match'],
  ['st', 'struct declaration'],
  ['en', 'enum / algebraic data type'],
  ['tr', 'trait (parsed; runtime support minimal)'],
  ['im', 'import an AiLang module'],
  ['ex', 'extern — C ABI / FFI declaration'],
  ['cinc', 'C #include — pull a C header into the generated code'],
  ['mu', 'mutable binding / mutable param marker'],
  ['br', 'break out of a loop'],
  ['ct', 'continue to next loop iteration'],
  ['as', 'rename in an import'],
  ['in', 'iterator binding in lp'],
];

const CONSTANTS = ['true', 'false', 'nil'];

const TYPES = [
  'i8', 'i16', 'i32', 'i64',
  'u8', 'u16', 'u32', 'u64',
  'f32', 'f64',
  'bool', 'str', 'bytes',
];

// { name, sig, doc, noArgs?, needsImport? }
// `sig` shown inline (detail); `doc` in the expanded panel.
const BUILTINS = [
  // Printing
  { name: 'print',    sig: '(x) -> ()', doc: 'Print x without a trailing newline.' },
  { name: 'println',  sig: '(x) -> ()', doc: 'Print x + newline. Pretty-prints arrays / maps / structs / enums.' },

  // I/O & process
  { name: 'read_file',  sig: '(path:str) -> str',        doc: 'Whole file as a string. "" on error.' },
  { name: 'write_file', sig: '(path:str, body:str) -> bool', doc: 'Overwrite file. true on success.' },
  { name: 'read_line',  sig: '() -> str', noArgs: true,  doc: 'One line from stdin, trailing \\n stripped.' },
  { name: 'args',       sig: '() -> [str]', noArgs: true, doc: 'Command-line args; program name at [0].' },
  { name: 'get_env',    sig: '(name:str) -> str',        doc: 'Env var value, or "" if unset.' },
  { name: 'exit',       sig: '(code:i64) -> ()',         doc: 'Terminate with the given exit code.' },

  // Conversions & formatting
  { name: 'int_to_str',   sig: '(n:i64) -> str' },
  { name: 'str_to_int',   sig: '(s:str) -> i64', doc: 'Returns 0 on parse failure.' },
  { name: 'float_to_str', sig: '(x:f64) -> str' },
  { name: 'str_to_float', sig: '(s:str) -> f64' },
  { name: 'str_to_bool',  sig: '(s:str) -> bool', doc: 'Accepts "true"/"false", case-insensitive.' },
  { name: 'to_str',       sig: '(x) -> str', doc: '_Generic stringify (any primitive). Used by ${} interpolation.' },
  { name: 'format',       sig: '(fmt:str, ...) -> str', doc: 'printf-style: %lld for i64, %s for str, %f for f64.' },

  // String ops
  { name: 'contains',    sig: '(s, sub) -> bool', doc: 'Also polymorphic over [i64] / [str].' },
  { name: 'starts_with', sig: '(s:str, prefix:str) -> bool' },
  { name: 'ends_with',   sig: '(s:str, suffix:str) -> bool' },
  { name: 'index_of',    sig: '(s, sub) -> i64', doc: '-1 if not found. Also works on arrays.' },
  { name: 'to_upper',    sig: '(s:str) -> str' },
  { name: 'to_lower',    sig: '(s:str) -> str' },
  { name: 'trim',        sig: '(s:str) -> str', doc: 'Strip ASCII whitespace both sides.' },
  { name: 'substring',   sig: '(s:str, lo:i64, hi:i64) -> str', doc: 'hi exclusive, byte indices.' },
  { name: 'replace',     sig: '(s, old, new) -> str', doc: 'Replace all non-overlapping occurrences.' },
  { name: 'split',       sig: '(s:str, sep:str) -> [str]', doc: 'Empty sep splits on every byte.' },
  { name: 'repeat',      sig: '(s:str, n:i64) -> str' },
  { name: 'pad_left',    sig: '(s:str, w:i64, pad:str) -> str' },
  { name: 'pad_right',   sig: '(s:str, w:i64, pad:str) -> str' },
  { name: 'chr',         sig: '(n:i64) -> str', doc: 'Code point → 1-char string.' },
  { name: 'ord',         sig: '(s:str) -> i64', doc: 'First byte/code-point of s.' },

  // Containers
  { name: 'len',     sig: '(str | bytes | [T] | {K:V}) -> i64' },
  { name: 'has',     sig: '(m:{K:V}, k:K) -> bool', doc: 'Map membership.' },
  { name: 'push',    sig: '(arr:[T], x:T) -> [T]', doc: 'Returns a NEW array: arr := push(arr, x).' },
  { name: 'pop',     sig: '(arr:[T]) -> [T]', doc: 'Returns new array minus last element.' },
  { name: 'sort',    sig: '(arr:[T]) -> [T]', doc: 'Ascending; [i64] and [str].' },
  { name: 'reverse', sig: '(arr:[T]) -> [T]' },
  { name: 'slice',   sig: '(arr:[T], lo:i64, hi:i64) -> [T]', doc: 'hi exclusive.' },
  { name: 'join',    sig: '(arr:[T], sep:str) -> str' },
  { name: 'keys',    sig: '(m:{K:V}) -> [K]', doc: 'Order undefined.' },
  { name: 'values',  sig: '(m:{K:V}) -> [V]', doc: 'Order undefined.' },

  // Higher-order
  { name: 'map',    sig: '(arr:[T], f:fn(T)->U) -> [U]' },
  { name: 'filter', sig: '(arr:[T], pred:fn(T)->bool) -> [T]' },
  { name: 'reduce', sig: '(arr:[T], init:U, f:fn(U,T)->U) -> U', doc: 'Left fold.' },

  // Regex
  { name: 'regex_match', sig: '(pat:str, s:str) -> bool', doc: 'POSIX extended; matches anywhere.' },
  { name: 'regex_find',  sig: '(pat:str, s:str) -> str', doc: 'First match, or "".' },

  // Math (always available)
  { name: 'abs_i64', sig: '(n:i64) -> i64' },
  { name: 'abs_f64', sig: '(x:f64) -> f64' },
  { name: 'sign',    sig: '(n:i64) -> i64', doc: '-1 / 0 / 1.' },
  { name: 'clamp',   sig: '(n:i64, lo:i64, hi:i64) -> i64' },

  // Result !T + ? propagation
  { name: 'ok',       sig: '(v:T) -> !T', doc: 'Polymorphic success constructor.' },
  { name: 'err_i64',  sig: '(msg:str) -> !i64', doc: 'Error constructor (no generic err()).' },
  { name: 'err_str',  sig: '(msg:str) -> !str' },
  { name: 'err_bool', sig: '(msg:str) -> !bool' },
  { name: 'err_f64',  sig: '(msg:str) -> !f64' },
  { name: 'unwrap',   sig: '(r:!T) -> T', doc: 'Aborts the program if r is err.' },
  { name: 'is_ok',    sig: '(r:!T) -> bool' },
  { name: 'is_err',   sig: '(r:!T) -> bool' },
  { name: 'err_msg',  sig: '(r:!T) -> str' },

  // Bytes
  { name: 'str_to_bytes', sig: '(s:str) -> bytes' },
  { name: 'bytes_to_str', sig: '(b:bytes) -> str' },
  { name: 'bytes_at',     sig: '(b:bytes, i:i64) -> i64', doc: 'Also b[i].' },
  { name: 'bytes_slice',  sig: '(b:bytes, lo:i64, hi:i64) -> bytes' },

  // TCP sockets
  { name: 'tcp_listen',   sig: '(host:str, port:i64) -> i64', doc: 'Listener fd; -1 on error.' },
  { name: 'tcp_accept',   sig: '(fd:i64) -> i64', doc: 'Blocks; returns client fd.' },
  { name: 'tcp_connect',  sig: '(host:str, port:i64) -> i64' },
  { name: 'sock_send',    sig: '(fd:i64, b:bytes) -> i64' },
  { name: 'sock_send_str', sig: '(fd:i64, s:str) -> i64' },
  { name: 'sock_recv',    sig: '(fd:i64, max:i64) -> bytes', doc: 'Empty on EOF.' },
  { name: 'sock_close',   sig: '(fd:i64) -> ()' },

  // Clocks
  { name: 'now_ms',   sig: '() -> i64', noArgs: true, doc: 'Wall clock ms since epoch.' },
  { name: 'now_us',   sig: '() -> i64', noArgs: true },
  { name: 'mono_ms',  sig: '() -> i64', noArgs: true, doc: 'Monotonic — safe for durations.' },
  { name: 'time_iso', sig: '(ms:i64) -> str', doc: '"2026-05-28T17:42:01" form.' },
  { name: 'sleep_ms', sig: '(ms:i64) -> ()' },

  // Common stdlib (require an import — noted)
  { name: 'sqrt',  sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'pow',   sig: '(x:f64, y:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'sin',   sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'cos',   sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'tan',   sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'log',   sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'log2',  sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'log10', sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'exp',   sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'floor', sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'ceil',  sig: '(x:f64) -> f64', needsImport: 'std/math.ail' },
  { name: 'min',   sig: '(a:i64, b:i64) -> i64', needsImport: 'std/math.ail' },
  { name: 'max',   sig: '(a:i64, b:i64) -> i64', needsImport: 'std/math.ail' },
  { name: 'ipow',  sig: '(base:i64, exp:i64) -> i64', needsImport: 'std/math.ail' },
  { name: 'gcd',   sig: '(a:i64, b:i64) -> i64', needsImport: 'std/math.ail' },
  { name: 'parse_int', sig: '(s:str) -> !i64', needsImport: 'std/str.ail' },
  { name: 'eq',    sig: '(a:str, b:str) -> bool', needsImport: 'std/str.ail' },
  { name: 'abs',   sig: '(n:i32) -> i32', needsImport: 'std/math.ail' },
  { name: 'rand',  sig: '() -> i32', noArgs: true, needsImport: 'std/math.ail' },
  { name: 'srand', sig: '(seed:u32) -> ()', needsImport: 'std/math.ail' },
  { name: 'strcmp', sig: '(a:str, b:str) -> i32', needsImport: 'std/str.ail' },
  { name: 'atoi',  sig: '(s:str) -> i32', needsImport: 'std/str.ail' },
];

module.exports = { KEYWORDS, CONSTANTS, TYPES, BUILTINS };
