#!/usr/bin/env node
// ailang-mcp — a Model Context Protocol server that gives ANY coding agent a
// closed compile / check / run / test loop for AiLang, out of the box.
//
// It is thin glue: every tool shells out to the `ailc` compiler on PATH (which
// must be installed — see https://github.com/Ray-T-r/AiLang) and marshals the
// result. The interesting work lives in `ailc` itself; this just exposes it over
// MCP so an agent can iterate without a human running a terminal.
//
//   Tools:
//     ailang_check(source)              → JSON diagnostics (fast: no clang link)
//     ailang_run(source, stdin?, args?) → { stdout, stderr, exit_code, timed_out }
//     ailang_test(source)               → assert-based PASS/FAIL summary
//     ailang_stdlib_search(query)       → matching std function signatures
//     ailang_skill()                    → the AiLang language context pack (SKILL.md)
//
// SANDBOX — HONEST SCOPE: ailang_run/test COMPILE AND EXECUTE arbitrary code,
// and the AiLang stdlib can open sockets / read+write files / spawn processes.
// This server applies only a wall-clock timeout + a per-call temp dir that is
// removed afterward. That is a *convenience* guard against runaways, NOT a
// security boundary. Run untrusted snippets inside a container or disposable VM
// (e.g. set AILANG_MCP_DENY_RUN=1 to expose only check + stdlib_search + skill).

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { execFile } from "node:child_process";
import { mkdtemp, writeFile, rm, readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const AILC = process.env.AILC || "ailc";
const TIMEOUT_MS = Number(process.env.AILANG_MCP_TIMEOUT_MS || 10000);
const MAX_OUTPUT = Number(process.env.AILANG_MCP_MAX_OUTPUT || 256 * 1024);
const DENY_RUN = process.env.AILANG_MCP_DENY_RUN === "1";

// Resolve the bundled assets (npm package layout) or fall back to the repo layout.
function findAsset(...candidates) {
  for (const c of candidates) if (c && existsSync(c)) return c;
  return null;
}
const STDLIB_JSON = findAsset(
  process.env.AILANG_STDLIB_JSON,
  join(HERE, "stdlib.json"),
  join(HERE, "..", "stdlib.json"),
);
const SKILL_MD = findAsset(
  process.env.AILANG_SKILL_MD,
  join(HERE, "SKILL.md"),
  join(HERE, "..", "skill", "SKILL.md"),
);

// Run `ailc <args>`, optionally feeding stdin; resolves with the captured result.
function ailc(args, { stdin } = {}) {
  return new Promise((resolve) => {
    const child = execFile(
      AILC,
      args,
      { timeout: TIMEOUT_MS, maxBuffer: MAX_OUTPUT, killSignal: "SIGKILL" },
      (err, stdout, stderr) => {
        const timed_out = !!(err && err.killed && err.signal === "SIGKILL");
        let exit_code = 0;
        if (err) exit_code = typeof err.code === "number" ? err.code : 1;
        resolve({ stdout: stdout || "", stderr: stderr || "", exit_code, timed_out });
      },
    );
    if (stdin != null) { child.stdin.end(stdin); }
  });
}

// Write `source` to a throwaway .ail, run `fn(file)`, always clean up the dir.
async function withSource(source, fn) {
  const dir = await mkdtemp(join(tmpdir(), "ailang-mcp-"));
  const file = join(dir, "snippet.ail");
  await writeFile(file, source, "utf8");
  try { return await fn(file); }
  finally { await rm(dir, { recursive: true, force: true }); }
}

const text = (s) => ({ content: [{ type: "text", text: s }] });

const server = new McpServer({ name: "ailang", version: "0.1.0" });

server.registerTool(
  "ailang_check",
  {
    title: "Type-check AiLang",
    description:
      "Parse + type-check AiLang source WITHOUT linking (fast). Returns machine-readable JSONL diagnostics: one {severity,code,line,col,message,source} object per error, or {\"severity\":\"ok\"} when clean. Use this as the inner loop while fixing errors before paying to compile.",
    inputSchema: { source: z.string().describe("AiLang (.ail) source code") },
  },
  async ({ source }) =>
    withSource(source, async (file) => {
      const r = await ailc(["check", "--json", file]);
      return text(r.stdout.trim() || r.stderr.trim() || "(no diagnostics)");
    }),
);

if (!DENY_RUN) {
  server.registerTool(
    "ailang_run",
    {
      title: "Compile + run AiLang",
      description:
        "Compile AiLang source to a native binary and run it, returning stdout, stderr, exit code, and whether it timed out. Optional stdin and argv. NOTE: executes arbitrary code under only a timeout guard — not a security sandbox.",
      inputSchema: {
        source: z.string().describe("AiLang (.ail) source code"),
        stdin: z.string().optional().describe("data piped to the program's stdin"),
        args: z.array(z.string()).optional().describe("command-line arguments passed after --"),
      },
    },
    async ({ source, stdin, args }) =>
      withSource(source, async (file) => {
        const argv = ["run", file, ...(args && args.length ? ["--", ...args] : [])];
        const r = await ailc(argv, { stdin });
        return text(JSON.stringify(r, null, 2));
      }),
  );

  server.registerTool(
    "ailang_test",
    {
      title: "Run AiLang assertions",
      description:
        "Compile + run AiLang source as a test: it should call assert(cond, msg). Exits 0 if all asserts pass; on the first failure prints 'assertion failed: <msg>' and exits nonzero. Returns the captured output + result.",
      inputSchema: { source: z.string().describe("AiLang source using assert(cond, msg)") },
    },
    async ({ source }) =>
      withSource(source, async (file) => {
        const r = await ailc(["test", file]);
        const status = r.timed_out ? "TIMEOUT" : r.exit_code === 0 ? "PASS" : "FAIL";
        return text(`${status}\n${r.stdout}${r.stderr}`.trim());
      }),
  );
}

server.registerTool(
  "ailang_stdlib_search",
  {
    title: "Search the AiLang stdlib",
    description:
      "Find standard-library function signatures by name or keyword, so you can ground on the real API instead of guessing. Returns matching 'module: signature' lines from the 25-module stdlib.",
    inputSchema: {
      query: z.string().describe("substring matched against function names, signatures, and module names"),
    },
  },
  async ({ query }) => {
    if (!STDLIB_JSON) return text("(stdlib.json not found — set AILANG_STDLIB_JSON)");
    const idx = JSON.parse(await readFile(STDLIB_JSON, "utf8"));
    const q = query.toLowerCase();
    const hits = [];
    for (const m of idx.modules) {
      for (const f of m.fns) {
        if (f.name.toLowerCase().includes(q) || f.sig.toLowerCase().includes(q) || m.module.toLowerCase().includes(q)) {
          hits.push(`${m.module}: ${f.sig}`);
        }
      }
    }
    if (!hits.length) return text(`no stdlib functions match "${query}"`);
    return text(hits.slice(0, 60).join("\n") + (hits.length > 60 ? `\n… (${hits.length - 60} more)` : ""));
  },
);

server.registerTool(
  "ailang_skill",
  {
    title: "AiLang language guide",
    description:
      "Return the full AiLang context pack (SKILL.md): syntax, keywords, the run/check/test/--json CLI loop, stdlib tables, and the top gotchas an LLM gets wrong. Read this before writing AiLang.",
    inputSchema: {},
  },
  async () => {
    if (!SKILL_MD) return text("(SKILL.md not found — set AILANG_SKILL_MD)");
    return text(await readFile(SKILL_MD, "utf8"));
  },
);

const transport = new StdioServerTransport();
await server.connect(transport);
console.error(`ailang-mcp ready (ailc=${AILC}, run=${DENY_RUN ? "disabled" : "enabled"}, timeout=${TIMEOUT_MS}ms)`);
