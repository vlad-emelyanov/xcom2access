---
name: frida
description: Inspect or patch the LIVE XCOM 2 WOTC process with Frida — read engine memory, discover offsets, call engine functions, hook, or byte-patch. Use for reverse-engineering work that needs the running game rather than static Ghidra analysis.
argument-hint: [what you want to read/call/patch]
allowed-tools: Bash, PowerShell, Read, Write, Edit, Glob, Grep
---

Drive Frida through `tools/frida-run.py`. **Do not use any `frida-mcp` MCP server** — see
"Why not the MCP" below. This project's UE3 offsets (GNames, GObjects, the class layout in
memory `project_ue3_offsets.md`) were originally discovered with exactly this workflow.

## Decide the tool FIRST — Frida is for discovery, not shipping

We ship two hooks from the ASI (`process_event_hook.cpp` vtable swap, `input_arbiter.cpp`
WNDPROC subclass) and log to `X2Access.log`. That instrumentation, plus static Ghidra
analysis, answers most questions. Reach for Frida ONLY when the answer needs the running
process and neither Ghidra nor our own log can give it:

- reading arbitrary memory / confirming an offset before we hard-code it (offset discovery);
- calling an engine function we have not bound yet, to test a recipe before writing C++;
- byte-patching `.text` for a reversible live experiment;
- hooking a code path our ASI does not own yet, to see when/whether it fires.

If the answer will be needed more than once, **fold it into the ASI** (a handler in
`hooks.cpp`, a log line, an `event_dispatch` watch) — that is the durable home. Frida is the
scratchpad you use before committing to a build.

## How to run

Write the agent to a `.js` file (scratchpad dir), then:

```bash
python tools/frida-run.py --script agent.js                          # calls rpc.exports.main()
python tools/frida-run.py --script agent.js --call readName -a 1234
python tools/frida-run.py --script agent.js --stay 30 --out hits.txt  # stream Interceptor messages
```

The agent should expose `rpc.exports` (returned value is printed as JSON) and/or `send()`.
The runner always unloads and detaches in a `finally`, so a bad run cannot poison the next
one. Attaching may need an **Administrator** shell (error code 2 = process not found or
permission denied).

## Frida 17 API — do not guess

These DO NOT exist (pre-17; a repeated time sink):

- `Module.findBaseAddress(name)` → use `Process.getModuleByName(name).base`
- `Module.getExportByName(mod, sym)` → use `Process.getModuleByName(mod).getExportByName(sym)`

These DO exist: `Interceptor.detachAll`, `Process.getModuleByName`, module-instance
`getExportByName`, `File` (works inside Interceptor callbacks), `Memory.protect`,
`NativeFunction`. Verify any doubtful API with a one-line probe rather than assuming.

## Addresses — XCOM 2 WOTC (x64)

- **ImageBase 0x140000000**, so `runtime = Process.getModuleByName('XCom2.exe').base + (VA − 0x140000000)`.
  Ghidra reports VAs against this base directly, so a Ghidra VA `0x140143B50` is
  `base + 0x143B50` at runtime.
- `.text` is `0x140001000 – 0x141363FFF`. Bound `.text` byte-scans to this range.
- Our ASI resolves the module base from `GetModuleHandleA(nullptr)`; Frida's
  `getModuleByName('XCom2.exe').base` is the same value.

## THE gotcha: a hook only fires while the script is loaded

Under `frida-mcp` the script was unloaded when each call returned, so **any hook on a
function that fires later than the current call was silently dead** — which produces
confident, WRONG "this never fires" conclusions. If a hook seems not to fire, **suspect
script lifetime first**: arm it with `--stay N`, then trigger the code path (a keypress, a
menu change, a later frame) while the script is still attached. Prove a negative with a
control hook on something hot AND a delayed re-check, never a single snapshot.

## ⛔ Do NOT hook high-frequency Windows message functions in this process

`user32!PeekMessageW` / `DispatchMessageW` and friends run at ~millions of calls/sec. A live
Interceptor hook on one can wedge Frida's Interceptor for the rest of the session: later
`Interceptor.attach` calls return without throwing but never write the patch (read the entry
bytes back — still the original prologue, no `e9`), the agent's own `setInterval` stops
ticking, and the runner can hang past its timeout. This matters doubly here because
`input_arbiter.cpp` subclasses the game's WNDPROC — when probing input, hook the WNDPROC
entry or a specific `WM_*` branch you found in Ghidra, **not** the message-pump primitives.

Rules that follow: **install every hook in ONE transaction inside `main()`**, then
`Interceptor.flush()` once; never add hooks later from a timer; and keep any single hook's
call rate survivable (a per-frame function is fine, a per-message one is not).

## A zero-hit hook usually means the TARGET IS COLD, not that hooking is broken

A resolvable address does not mean the build's control flow enters that function (LTCG may
have inlined the body into its callers). Before concluding anything about Frida, put a
**known-live sibling hook in the same script/transaction** as the control that rules out
systemic causes, and make sure the target actually has a trigger.

**Candidate hot control for THIS game (verify with `--stay` before trusting):**
`ProcessEvent` at `base + 0x143B50` fires thousands of times/sec — it is the function our
own vtable hook already rides, so it is the natural positive control. Arm a counter on it
with `--stay 10`; if it counts and your target counts 0, the target is cold, not the hook.
Do NOT record any address as a "verified live control" until you have actually seen it hit
in a `--stay` run — this project has no pre-verified control list yet.

## Bound every `Memory.scanSync`

An unbounded scan over all RW ranges is genuinely slow (GBs mapped) and under the MCP it
wedged the whole server so even a *fresh* session timed out. Scan a specific module range
(e.g. `.text` above for code, a known `.data` sub-range for globals), and prefer a targeted
read over a scan whenever you already have an offset.

## x64 ABI (for `NativeFunction` / reading register args in hooks)

- Integer/pointer args: **RCX, RDX, R8, R9**, then stack. Float args: **XMM0–XMM3**
  positionally (a float in arg slot 3 lands in XMM2, not XMM3).
- Most engine methods are effectively `__thiscall`/`__fastcall`: **`this` is RCX**.
- `ProcessEvent(this, UFunction* fn, void* params)` → RCX=this, RDX=fn, R8=params. Reading
  `fn->Name` gives the script function name (FNameEntry.Name is UTF-8 at `+0x14`).

## Useful live handles (XCOM 2 WOTC, x64 — from `project_ue3_offsets.md`)

Runtime address = `base + offset`.

- **GNames** TArray: `base + 0x1DAF450`  •  **GObjects** TArray: `base + 0x1DAF4A8`
- **ProcessEvent**: `base + 0x143B50`  (UObject vtable **slot 72**)
- **GNatives** dispatch table: `base + 0x1C9DC40`
- **FNameEntry.Name**: `+0x14`, UTF-8 (not wide) — resolve an FName index against GNames to
  get a function/object name string.
- **UObject** fields: InternalIndex `+0x38`, Outer `+0x40`, Name `+0x48`, Class `+0x50`.
- **UStruct**: SuperField `+0x78`, Children `+0x80`, PropertySize `+0x88`.
- **UProperty**: Offset `+0x94`, ElementSize `+0x6C`, PropertyFlags `+0x70`.
- **UFunction**: FunctionFlags `+0xD0`, iNative `+0xD4`, ParmsSize `+0xE2`, Func `+0xF0`.
- Full class PropertySizes and field offsets: see memory `project_ue3_offsets.md`. If a read
  looks wrong, re-verify the offset live before trusting the table — it is a point-in-time
  dump, and these are the values baked into CodeRed's `GameDefines.hpp`.

## Byte-patching rules

Reversible experiments only, and only with the user's agreement:

1. Read and **save the original bytes** before writing.
2. `Memory.protect(addr, len, 'rwx')` then `writeU8`.
3. Restore on every exit path, and verify the current byte still matches what you wrote
   before restoring (another patch may have moved things).
4. Never leave a patch in place across a session; never patch anything the user has not
   approved.
5. Anything we intend to SHIP becomes a hook/handler in the ASI, not a byte patch — patches
   are for experiments, the ASI is for features.

## Why not the MCP

`frida-mcp` is a proof-of-concept (v0.1.1, both releases published minutes apart in early
2025, nothing since). In practice it gave a fixed short timeout with no cancel, no state
between calls, JS wrapped in JSON, swallowed error detail, and an agent that stayed wedged
for the rest of the session. `tools/frida-run.py` has none of those limits and can simply be
killed if a scan runs long.
