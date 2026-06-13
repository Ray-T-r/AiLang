# ailang-mcp

A [Model Context Protocol](https://modelcontextprotocol.io) server that gives any
coding agent (Claude Code, Cursor, …) a **closed check / run / test loop** over the
AiLang compiler — write AiLang, get structured diagnostics, run it, assert on it,
all without a human at a terminal.

It is thin glue: every tool shells out to the `ailc` compiler on your `PATH`
(install it from <https://github.com/Ray-T-r/AiLang>). The compiler does the work;
this exposes `ailc run` / `ailc check --json` / `ailc test` over MCP.

## Tools

| Tool | What it does |
|------|--------------|
| `ailang_check(source)` | Parse + type-check only (no clang link). Returns JSONL diagnostics: `{severity,code,line,col,message,source}` per error, or `{"severity":"ok"}`. The fast inner loop. |
| `ailang_run(source, stdin?, args?)` | Compile + execute. Returns `{stdout, stderr, exit_code, timed_out}`. |
| `ailang_test(source)` | Run `assert(cond,msg)`-based source; `PASS`/`FAIL`/`TIMEOUT` + output. |
| `ailang_stdlib_search(query)` | Match std function signatures by name/keyword (from the bundled `stdlib.json`, 25 modules). |
| `ailang_skill()` | Return the AiLang context pack (`SKILL.md`) — read it before writing AiLang. |

## Install

```bash
# 1. install the ailc compiler first (must be on PATH)
curl -fsSL https://github.com/Ray-T-r/AiLang/releases/latest/download/install.sh | bash
# 2. register the MCP server (zero-install via npx once published)
```

Add to your MCP client config (e.g. Claude Code):

```json
{ "mcpServers": { "ailang": { "command": "npx", "args": ["-y", "ailang-mcp"] } } }
```

Or run from this checkout:

```bash
cd mcp && npm install && npm run bundle && node index.mjs
```

## Configuration (env vars)

| Var | Default | Meaning |
|-----|---------|---------|
| `AILC` | `ailc` | path to the compiler |
| `AILANG_MCP_TIMEOUT_MS` | `10000` | wall-clock cap per compile/run |
| `AILANG_MCP_MAX_OUTPUT` | `262144` | max captured bytes |
| `AILANG_MCP_DENY_RUN` | unset | set `1` to expose only `check` + `stdlib_search` + `skill` (no code execution) |

## Sandbox — read this

`ailang_run` / `ailang_test` **compile and execute arbitrary code**, and the AiLang
stdlib can open sockets, read/write files, and spawn processes. This server applies
only a **wall-clock timeout + a per-call temp directory** that is deleted afterward.
That is a *convenience* guard against runaways, **not a security boundary**. For
untrusted input, run the server inside a container or disposable VM, or set
`AILANG_MCP_DENY_RUN=1` to disable execution entirely.
