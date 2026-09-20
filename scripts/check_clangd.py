"""Fail on clangd's editor diagnostics for project-owned C++ files.

clangd --check does not report all LSP diagnostics (notably unused includes),
so this opens each file through the language-server protocol instead.
"""

import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading


class Clangd:
    def __init__(self, executable: str, compile_commands: Path, root: Path):
        self.process = subprocess.Popen(
            [
                executable,
                "--enable-config",
                "--clang-tidy",
                "--background-index=false",
                "--log=error",
                f"--compile-commands-dir={compile_commands}",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=sys.stderr,
        )
        self.messages = queue.Queue()
        self.reader = threading.Thread(target=self.read_messages, daemon=True)
        self.reader.start()
        self.next_id = 1
        self.send(
            "initialize",
            {
                "processId": os.getpid(),
                "rootUri": root.as_uri(),
                "capabilities": {},
            },
            request_id=0,
        )
        self.until(lambda message: message.get("id") == 0)
        self.send("initialized", {})

    def send(self, method: str, params: dict, request_id: int | None = None):
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        if request_id is not None:
            message["id"] = request_id
        data = json.dumps(message).encode()
        self.process.stdin.write(f"Content-Length: {len(data)}\r\n\r\n".encode() + data)
        self.process.stdin.flush()

    def read_messages(self):
        try:
            while True:
                headers = {}
                while line := self.process.stdout.readline():
                    if line == b"\r\n":
                        break
                    name, value = line.decode("ascii").split(":", 1)
                    headers[name.lower()] = value.strip()
                else:
                    raise RuntimeError("clangd closed its output")
                body = self.process.stdout.read(int(headers["content-length"]))
                self.messages.put(json.loads(body))
        except Exception as exc:
            self.messages.put(exc)

    def receive(self) -> dict:
        try:
            message = self.messages.get(timeout=30)
        except queue.Empty as exc:
            raise TimeoutError("clangd did not respond within 30 seconds") from exc
        if isinstance(message, Exception):
            raise message
        return message

    def until(self, predicate):
        while True:
            message = self.receive()
            if predicate(message):
                return message

    def diagnostics(self, path: Path) -> list[dict]:
        uri = path.as_uri()
        self.send(
            "textDocument/didOpen",
            {
                "textDocument": {
                    "uri": uri,
                    "languageId": "cpp",
                    "version": 1,
                    "text": path.read_text(encoding="utf-8"),
                }
            },
        )
        message = self.until(
            lambda m: (
                m.get("method") == "textDocument/publishDiagnostics"
                and m["params"]["uri"] == uri
            )
        )
        self.send("textDocument/didClose", {"textDocument": {"uri": uri}})
        return message["params"]["diagnostics"]

    def close(self):
        self.send("shutdown", {}, request_id=self.next_id)
        self.until(lambda message: message.get("id") == self.next_id)
        self.send("exit", {})
        self.process.wait(timeout=10)
        if self.process.returncode:
            raise RuntimeError(f"clangd exited with status {self.process.returncode}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clangd", default="clangd")
    parser.add_argument("--compile-commands", type=Path, required=True)
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    if args.files:
        files = [path.resolve() for path in args.files]
    else:
        # These command-line tools use POSIX APIs and are not built on Windows.
        windows_only_exclusions = {
            b"bench/library_compare.cc",
            b"bench/profile_parse.cc",
            b"tests/reference/native.cc",
            b"tests/validate_corpus.cc",
            b"tests/fuzz_parser.cc",
            # Standalone support headers otherwise inherit the fuzz target's
            # sanitizer flags, which clangd cannot combine with the MSVC CRT.
            b"tests/support/canonical_dump.h",
            b"tests/support/equality.h",
            b"tests/support/scalar_engine.h",
        }
        tracked = subprocess.check_output(
            ["git", "ls-files", "-z", "--", "*.h", "*.cc", "*.cpp"], cwd=root
        )
        files = [
            root / os.fsdecode(path)
            for path in tracked.split(b"\0")
            if path
            and not path.startswith(b"src/fosu/engine/third_party/")
            # This standalone comparison worker has separate downloaded headers.
            and path != b"bench/comparison/native_worker.cc"
            # These headers are intentionally included in a specific source context.
            and path not in (b"src/fosu/bindings/records.h", b"tests/support/test.h")
            and (sys.platform != "win32" or path not in windows_only_exclusions)
        ]
    client = Clangd(args.clangd, args.compile_commands.resolve(), root)
    failures = 0
    try:
        for index, path in enumerate(files, 1):
            for diagnostic in client.diagnostics(path):
                if diagnostic.get("severity", 1) > 2:
                    continue
                line = diagnostic["range"]["start"]["line"] + 1
                code = diagnostic.get("code", "unknown")
                print(
                    f"{path.relative_to(root)}:{line}: {code}: {diagnostic['message']}"
                )
                failures += 1
            if index % 10 == 0:
                print(f"clangd checked {index}/{len(files)} files", flush=True)
    finally:
        client.close()
    print(f"clangd checked {len(files)} files; {failures} diagnostics")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
