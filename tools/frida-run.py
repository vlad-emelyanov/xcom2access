#!/usr/bin/env python
"""Drive Frida directly, without the frida-mcp wrapper.

Why this exists
---------------
`frida-mcp` proved unreliable for RE work: it runs each snippet with a short fixed
timeout, keeps no state between calls, and offers no way to cancel or detach. A single
`Memory.scanSync` over the game's RW ranges wedged the agent so hard that even a freshly
created session timed out, while the game itself stayed perfectly healthy. The Python
bindings have none of those limits.

What this gives us that the MCP did not
  * we choose the timeout, and long scans can run in the background instead of blocking;
  * the script is a FILE — no JS-inside-JSON escaping, and clangd/eslint can see it;
  * state persists for the whole run, so an Interceptor can accumulate across frames;
  * `rpc.exports` gives structured request/response instead of scraping a console;
  * clean detach on every exit path, so a bad script cannot poison the next attempt.

Usage
  python tools/frida-run.py --script probe.js                       # call rpc.exports.main()
  python tools/frida-run.py --script probe.js --call readName -a 1234
  python tools/frida-run.py --script watch.js --stay 30 --out log.txt   # stream messages

The script may either export `rpc.exports` (preferred) or simply `send()` results.
Anything the script `send()`s is printed, and also appended to --out when given.

Exit codes: 0 ok, 2 process not found, 3 script error, 4 timeout.
"""

from __future__ import annotations

import argparse
import json
import sys
import time

try:
    import frida
except ImportError:
    sys.exit("frida python bindings not installed: python -m pip install frida")


def main() -> int:
    ap = argparse.ArgumentParser(description="Attach to a process and run a Frida script.")
    ap.add_argument("--process", "-p", default="XCom2.exe",
                    help="target process name or pid (default: XCom2.exe)")
    ap.add_argument("--script", "-s", required=True, help="path to the .js agent")
    ap.add_argument("--call", "-c", default="main",
                    help="rpc.exports function to call (default: main); '-' to skip")
    ap.add_argument("--arg", "-a", action="append", default=[],
                    help="argument for the rpc call; numbers are passed as numbers (repeatable)")
    ap.add_argument("--stay", type=float, default=0.0,
                    help="after the call, keep the session alive N seconds streaming messages")
    ap.add_argument("--timeout", type=float, default=60.0, help="rpc call timeout in seconds")
    ap.add_argument("--out", help="also append printed output to this file")
    args = ap.parse_args()

    with open(args.script, "r", encoding="utf-8") as fh:
        source = fh.read()

    sink = open(args.out, "a", encoding="utf-8") if args.out else None

    def emit(text: str) -> None:
        print(text, flush=True)
        if sink:
            sink.write(text + "\n")
            sink.flush()

    # Attach by pid when the target is numeric, else by name.
    target: object = int(args.process) if args.process.isdigit() else args.process
    try:
        session = frida.attach(target)
    except frida.ProcessNotFoundError:
        emit(f"[error] process not found: {args.process}")
        return 2
    except frida.PermissionDeniedError as exc:
        emit(f"[error] permission denied attaching to {args.process}: {exc} "
             f"(run the shell as Administrator)")
        return 2

    rc = 0
    script = None
    try:
        script = session.create_script(source)

        def on_message(message: dict, data: object) -> None:
            if message.get("type") == "send":
                payload = message.get("payload")
                emit(payload if isinstance(payload, str) else json.dumps(payload, indent=1))
            else:  # 'error' — include the JS stack, which the MCP used to swallow
                emit("[script error] " + json.dumps(message, indent=1))

        script.on("message", on_message)
        script.load()

        if args.call != "-":
            fn = getattr(script.exports_sync, args.call, None)
            if fn is None:
                emit(f"[error] script has no rpc.exports.{args.call}; "
                     f"exports: {dir(script.exports_sync)}")
                return 3
            parsed = []
            for a in args.arg:
                try:
                    parsed.append(float(a) if ("." in a or "e" in a.lower()) else int(a))
                except ValueError:
                    parsed.append(a)
            # NOTE: frida 17's sync rpc proxy takes no timeout kwarg; the guard is that we
            # always detach in `finally`, so a slow agent cannot wedge the next run.
            result = fn(*parsed)
            emit(result if isinstance(result, str) else json.dumps(result, indent=1))

        if args.stay > 0:
            emit(f"[staying attached {args.stay}s — streaming messages]")
            deadline = time.time() + args.stay
            while time.time() < deadline:
                time.sleep(0.2)
    except frida.InvalidOperationError as exc:
        emit(f"[error] {exc}")
        rc = 3
    except frida.TimedOutError:
        emit(f"[timeout] rpc call exceeded {args.timeout}s — the agent is still loaded; "
             f"detaching cleanly")
        rc = 4
    finally:
        # Always unload+detach. A wedged agent was the exact MCP failure mode.
        try:
            if script is not None:
                script.unload()
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass
        if sink:
            sink.close()
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
