#!/usr/bin/env python3
"""
Local JSEngine symbol and source server helper.
"""

from __future__ import annotations

import argparse
import http.server
import html
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
ENGINE_ROOT = REPO_ROOT / "JSEngine"
SOLUTION_PATH = REPO_ROOT / "JSEngine.sln"
DEFAULT_SYMBOL_STORE = Path(r"C:\symbols")
PRODUCT_NAME = "JSEngine"
PID_FILE_NAME = ".jse_symbol_http.pid"
LOG_FILE_NAME = ".jse_symbol_http.log"
DASHBOARD_JOB_LOCK = threading.Lock()
DASHBOARD_JOB: dict[str, object] = {
    "running": False,
    "name": "",
    "startedAt": "",
    "exitCode": None,
    "lines": [],
}


def quote_arg(value: str) -> str:
    return f'"{value}"' if any(ch.isspace() for ch in value) else value


def run(args: list[str | Path], *, cwd: Path | None = None, capture: bool = False) -> subprocess.CompletedProcess[str]:
    text_args = [str(arg) for arg in args]
    print("+ " + " ".join(quote_arg(arg) for arg in text_args))
    return subprocess.run(
        text_args,
        cwd=str(cwd or REPO_ROOT),
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
        check=True,
    )


def git_text(*args: str) -> str:
    result = run(["git", "-C", REPO_ROOT, *args], capture=True)
    return (result.stdout or "").strip()


def to_https_git_remote(remote: str) -> str:
    value = remote.strip()
    if value.startswith("git@github.com:"):
        return "https://github.com/" + value.removeprefix("git@github.com:")
    if value.startswith("ssh://git@github.com/"):
        return "https://github.com/" + value.removeprefix("ssh://git@github.com/")
    return value


def find_msbuild() -> Path:
    from_path = shutil.which("MSBuild.exe")
    if from_path:
        return Path(from_path)

    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.exists():
        result = run(
            [
                vswhere,
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.Component.MSBuild",
                "-find",
                r"MSBuild\Current\Bin\amd64\MSBuild.exe",
            ],
            capture=True,
        )
        for line in (result.stdout or "").splitlines():
            candidate = Path(line.strip())
            if candidate.exists():
                return candidate

    roots = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Microsoft Visual Studio",
        Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio",
    ]
    for root in roots:
        if not root.exists():
            continue
        for candidate in sorted(root.glob(r"*\*\MSBuild\Current\Bin\amd64\MSBuild.exe"), reverse=True):
            if candidate.exists():
                return candidate

    raise RuntimeError("MSBuild.exe not found. Run from a Developer PowerShell or install Visual Studio Build Tools.")


def find_debug_tool(name: str) -> Path:
    from_path = shutil.which(name)
    if from_path:
        return Path(from_path)

    kits = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "Debuggers"
    for arch in ("x64", "x86", "arm64"):
        root = kits / arch
        for candidate in (root / name, root / "srcsrv" / name):
            if candidate.exists():
                return candidate

    raise RuntimeError(f"{name} not found. Install Debugging Tools for Windows.")


def target_configs(include_debug: bool) -> list[str]:
    if include_debug:
        return ["Debug", "Release", "GameClientDebug", "GameClientRelease"]
    return ["Release", "GameClientRelease"]


def target_name(config: str) -> str:
    return "JSEngineGame" if config.startswith("GameClient") else "JSEngine"


def build(configs: Iterable[str]) -> None:
    msbuild = find_msbuild()
    for config in configs:
        run([msbuild, SOLUTION_PATH, f"/p:Configuration={config}", "/p:Platform=x64", "/m", "/v:minimal"])


def build_release_package() -> None:
    release_batch = REPO_ROOT / "ReleaseBuild.bat"
    if not release_batch.exists():
        raise RuntimeError(f"{release_batch} not found.")
    run(["cmd.exe", "/d", "/c", release_batch, "--no-pause"], cwd=REPO_ROOT)


def get_pdb_sources(srctool: Path, pdb: Path) -> list[Path]:
    text_args = [str(srctool), "-r", str(pdb)]
    print("+ " + " ".join(quote_arg(arg) for arg in text_args))
    result = subprocess.run(text_args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    sources: list[Path] = []
    for line in (result.stdout or "").splitlines():
        text = line.strip()
        if len(text) >= 3 and text[1:3] == ":\\":
            sources.append(Path(text))
        elif text.startswith("\\\\"):
            sources.append(Path(text))
    if not sources and result.returncode != 0:
        raise RuntimeError(f"srctool failed for {pdb}: {(result.stdout or '').strip()}")
    return sources


def repo_relative(path: Path) -> str | None:
    try:
        return path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return None


def url_path(relative_path: str) -> str:
    return "/".join(urllib.parse.quote(part) for part in relative_path.split("/"))


def source_base_url(port: int) -> str:
    addresses = local_ipv4_addresses()
    host = addresses[0] if addresses else "127.0.0.1"
    return f"http://{host}:{port}"


def publish_source_snapshot(symbol_store: Path, sources: Iterable[tuple[Path, str]], commit: str) -> int:
    copied = 0
    source_root = symbol_store / "src" / commit
    for source, relative in sources:
        destination = source_root / Path(*relative.split("/"))
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or source.stat().st_mtime_ns != destination.stat().st_mtime_ns or source.stat().st_size != destination.stat().st_size:
            shutil.copy2(source, destination)
        copied += 1
    return copied


def collect_indexable_sources(srctool: Path, pdb: Path) -> list[tuple[Path, str]]:
    sources: list[tuple[Path, str]] = []
    seen: set[str] = set()
    for source in get_pdb_sources(srctool, pdb):
        relative = repo_relative(source)
        if relative is None or not source.is_file():
            continue
        key = relative.lower()
        if key in seen:
            continue
        seen.add(key)
        sources.append((source, relative))
    return sources


def write_srcsrv_stream(
    srctool: Path,
    pdb: Path,
    stream_path: Path,
    commit: str,
    source_url: str,
    symbol_store: Path,
) -> tuple[int, int]:
    entries: list[str] = []
    sources = collect_indexable_sources(srctool, pdb)
    for source, relative in sources:
        entries.append(f"{source}*{relative}*{commit}*{source_url}*{url_path(relative)}")

    if not entries:
        raise RuntimeError(f"No repository source paths found in {pdb}.")

    copied = publish_source_snapshot(symbol_store, sources, commit)
    srcsrv_command = (
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"$dst='%targ%\\JSEngineSrc\\%var3%\\%var2%'; "
        "$dir=Split-Path -Parent $dst; "
        "New-Item -ItemType Directory -Force -Path $dir | Out-Null; "
        "Invoke-WebRequest -Uri '%var4%/src/%var3%/%var5%' -OutFile $dst -UseBasicParsing\""
    )
    lines = [
        "SRCSRV: ini ------------------------------------------------",
        "VERSION=2",
        "INDEXVERSION=2",
        "VERCTRL=HTTP",
        "SRCSRV: variables ------------------------------------------",
        r"SRCSRVTRG=%targ%\JSEngineSrc\%var3%\%var2%",
        f"SRCSRVCMD={srcsrv_command}",
        "SRCSRV: source files ---------------------------------------",
        *entries,
        "SRCSRV: end ------------------------------------------------",
    ]
    stream_path.write_text("\n".join(lines) + "\n", encoding="ascii")
    return len(entries), copied


def publish(symbol_store: Path, configs: Iterable[str], *, skip_source_index: bool, port: int) -> None:
    symbol_store.mkdir(parents=True, exist_ok=True)

    symstore = find_debug_tool("symstore.exe")
    srctool = find_debug_tool("srctool.exe")
    pdbstr = find_debug_tool("pdbstr.exe")
    commit = git_text("rev-parse", "HEAD")
    source_url = source_base_url(port)

    print(f"Symbol store: {symbol_store}")
    print(f"Commit      : {commit}")
    print(f"Source URL  : {source_url}/src/{commit}/")

    published = 0
    for config in configs:
        name = target_name(config)
        output_dir = ENGINE_ROOT / "Bin" / config
        exe = output_dir / f"{name}.exe"
        pdb = output_dir / f"{name}.pdb"

        print(f"\n[{config}] {name}")
        if not exe.exists() or not pdb.exists():
            print(f"  Missing {exe.name} or {pdb.name}. Build this configuration first.")
            continue

        if not skip_source_index:
            stream_path = Path(tempfile.gettempdir()) / f"{name}-{config}-srcsrv.stream"
            count, copied = write_srcsrv_stream(srctool, pdb, stream_path, commit, source_url, symbol_store)
            print(f"  Source indexed files: {count}")
            print(f"  Source snapshot files: {copied}")
            run([pdbstr, "-w", f"-p:{pdb}", f"-i:{stream_path}", "-s:srcsrv"])

        comment = f"{PRODUCT_NAME} {config} {commit}"
        for file_path in (pdb, exe):
            run([symstore, "add", "/f", file_path, "/s", symbol_store, "/t", PRODUCT_NAME, "/v", commit, "/c", comment])
            published += 1

    if published == 0:
        raise RuntimeError("No symbols were published.")

    print(f"\nPublished {published} files.")


def local_ipv4_addresses() -> list[str]:
    addresses: set[str] = set()
    host = socket.gethostname()
    for info in socket.getaddrinfo(host, None, socket.AF_INET):
        ip = info[4][0]
        if ip != "127.0.0.1":
            addresses.add(ip)
    return sorted(addresses)


def print_symbol_urls(port: int) -> None:
    print(f"Local URL: http://localhost:{port}/")
    for ip in local_ipv4_addresses():
        print(f"Team URL : http://{ip}:{port}/")
    print(f"VS path  : srv*C:\\SymbolsCache*http://<host-ip>:{port}")


def symbol_urls(port: int) -> list[str]:
    urls = [f"http://localhost:{port}/"]
    urls.extend(f"http://{ip}:{port}/" for ip in local_ipv4_addresses())
    return urls


def serve_foreground(symbol_store: Path, port: int, bind: str) -> None:
    symbol_store.mkdir(parents=True, exist_ok=True)
    os.chdir(symbol_store)
    print("Symbol HTTP server")
    print(f"Root: {symbol_store}")
    print_symbol_urls(port)
    print("Press Ctrl+C to stop.\n")
    server = http.server.ThreadingHTTPServer((bind, port), http.server.SimpleHTTPRequestHandler)
    server.serve_forever()


def pid_file(symbol_store: Path) -> Path:
    return symbol_store / PID_FILE_NAME


def log_file(symbol_store: Path) -> Path:
    return symbol_store / LOG_FILE_NAME


def tail_lines(path: Path, max_lines: int = 160) -> list[str]:
    if not path.exists():
        return []
    try:
        with path.open("r", encoding="utf-8", errors="replace") as file:
            lines = file.readlines()
    except OSError:
        return []
    return [line.rstrip("\r\n") for line in lines[-max_lines:]]


LOG_REQUEST_RE = re.compile(
    r'^(?P<ip>\S+) - - \[(?P<time>[^\]]+)\] "(?P<method>\S+) (?P<path>.*?) HTTP/[^"]+" (?P<status>\d+)'
)


def recent_requests(symbol_store: Path, max_items: int = 80) -> list[dict[str, str]]:
    requests: list[dict[str, str]] = []
    for line in tail_lines(log_file(symbol_store), 300):
        match = LOG_REQUEST_RE.match(line)
        if not match:
            continue
        requests.append(
            {
                "ip": match.group("ip"),
                "time": match.group("time"),
                "method": match.group("method"),
                "path": match.group("path"),
                "status": match.group("status"),
            }
        )
    return requests[-max_items:]


def port_connections(port: int) -> list[dict[str, str]]:
    result = subprocess.run(
        ["netstat", "-ano"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    connections: list[dict[str, str]] = []
    needle = f":{port}"
    for line in (result.stdout or "").splitlines():
        parts = line.split()
        if len(parts) < 5 or parts[0] != "TCP":
            continue
        if not parts[1].endswith(needle):
            continue
        state = parts[3]
        if state == "LISTENING":
            remote = ""
            pid = parts[4]
        else:
            remote = parts[2]
            pid = parts[4] if len(parts) > 4 else ""
        connections.append({"local": parts[1], "remote": remote, "state": state, "pid": pid})
    return connections


def symbol_http_health(port: int) -> dict[str, object]:
    url = f"http://127.0.0.1:{port}/"
    started = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=0.8) as response:
            elapsed_ms = round((time.monotonic() - started) * 1000)
            return {
                "ok": 200 <= response.status < 400,
                "status": response.status,
                "url": url,
                "elapsedMs": elapsed_ms,
                "message": f"HTTP {response.status} in {elapsed_ms} ms",
            }
    except (OSError, urllib.error.URLError) as exc:
        return {
            "ok": False,
            "status": None,
            "url": url,
            "elapsedMs": None,
            "message": str(exc),
        }


def process_is_running(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def read_pid(symbol_store: Path) -> int | None:
    path = pid_file(symbol_store)
    if not path.exists():
        return None
    try:
        return int(path.read_text(encoding="ascii").strip())
    except ValueError:
        return None


def ps_quote(value: str) -> str:
    return "'" + value.replace("'", "''") + "'"


def listening_port_pids(port: int) -> list[int]:
    result = subprocess.run(
        ["netstat", "-ano"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    pids: set[int] = set()
    needle = f":{port}"
    for line in (result.stdout or "").splitlines():
        parts = line.split()
        if len(parts) < 5 or parts[0] != "TCP" or parts[3] != "LISTENING":
            continue
        if not parts[1].endswith(needle):
            continue
        try:
            pid = int(parts[-1])
        except ValueError:
            continue
        if pid != os.getpid():
            pids.add(pid)
    return sorted(pids)


def server_process_pids(port: int | None = None) -> list[int]:
    pids: set[int] = set(listening_port_pids(port)) if port is not None else set()

    if os.name != "nt":
        return sorted(pids)

    script = ps_quote(str(Path(__file__).resolve()))
    command = (
        f"$script = {script}; "
        "Get-CimInstance Win32_Process | "
        "Where-Object { $_.CommandLine -and $_.CommandLine.Contains($script) -and $_.CommandLine -match '\\sserve(\\s|$)' } | "
        "Select-Object -ExpandProperty ProcessId"
    )
    result = subprocess.run(
        ["powershell", "-NoProfile", "-Command", command],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    for line in (result.stdout or "").splitlines():
        try:
            pid = int(line.strip())
        except ValueError:
            continue
        if pid != os.getpid() and process_is_running(pid):
            pids.add(pid)
    return sorted(set(pids))


def terminate_pid(pid: int) -> bool:
    if pid <= 0:
        return False

    if os.name == "nt":
        result = subprocess.run(
            ["powershell", "-NoProfile", "-Command", f"Stop-Process -Id {pid} -Force"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        return result.returncode == 0
    else:
        if not process_is_running(pid):
            return False
        os.kill(pid, 15)
        return True


def start_server(symbol_store: Path, port: int, bind: str) -> None:
    symbol_store.mkdir(parents=True, exist_ok=True)
    existing = set(server_process_pids(port))
    pid_from_file = read_pid(symbol_store)
    if pid_from_file and process_is_running(pid_from_file):
        existing.add(pid_from_file)

    if existing:
        if len(existing) > 1:
            keeper = sorted(existing)[0]
            for duplicate in sorted(existing)[1:]:
                terminate_pid(duplicate)
                print(f"Stopped duplicate symbol server. PID={duplicate}")
            existing = {keeper}
        pid = next(iter(existing))
        pid_file(symbol_store).write_text(str(pid), encoding="ascii")
        print(f"Server is already running. PID={pid}")
        print_symbol_urls(port)
        return

    log = open(log_file(symbol_store), "a", encoding="utf-8")
    args = [
        sys.executable,
        str(Path(__file__).resolve()),
        "serve",
        "--symbol-store",
        str(symbol_store),
        "--port",
        str(port),
        "--bind",
        bind,
    ]
    creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == "nt" else 0
    proc = subprocess.Popen(args, cwd=str(REPO_ROOT), stdout=log, stderr=subprocess.STDOUT, creationflags=creationflags)
    pid_file(symbol_store).write_text(str(proc.pid), encoding="ascii")
    print(f"Started symbol HTTP server. PID={proc.pid}")
    print(f"Log: {log_file(symbol_store)}")
    print_symbol_urls(port)


def stop_server(symbol_store: Path, port: int) -> None:
    pids = set(server_process_pids(port))
    pid = read_pid(symbol_store)
    if pid:
        pids.add(pid)

    stopped = 0
    for server_pid in sorted(pids):
        if terminate_pid(server_pid):
            print(f"Stopped symbol server. PID={server_pid}")
            stopped += 1

    pid_file(symbol_store).unlink(missing_ok=True)
    if stopped == 0:
        print("No symbol server process found.")


def server_status(symbol_store: Path, port: int) -> None:
    pids = set(server_process_pids(port))
    pid = read_pid(symbol_store)
    if pid and process_is_running(pid):
        pids.add(pid)

    if pids:
        print(f"Server running. PID={', '.join(str(pid) for pid in sorted(pids))}")
        print_symbol_urls(port)
    else:
        print("Server not running.")


def dashboard_status(symbol_store: Path, port: int) -> dict[str, object]:
    pids = set(server_process_pids(port))
    pid = read_pid(symbol_store)
    if pid and process_is_running(pid):
        pids.add(pid)
    health = symbol_http_health(port) if pids else {
        "ok": False,
        "status": None,
        "url": f"http://127.0.0.1:{port}/",
        "elapsedMs": None,
        "message": "No listener on symbol port.",
    }
    with DASHBOARD_JOB_LOCK:
        job = {
            "running": DASHBOARD_JOB["running"],
            "name": DASHBOARD_JOB["name"],
            "startedAt": DASHBOARD_JOB["startedAt"],
            "exitCode": DASHBOARD_JOB["exitCode"],
            "lines": list(DASHBOARD_JOB["lines"])[-120:],
        }
    return {
        "running": bool(pids) and bool(health["ok"]),
        "hasListener": bool(pids),
        "pids": sorted(pids),
        "health": health,
        "urls": symbol_urls(port),
        "vsPath": f"srv*C:\\SymbolsCache*http://<host-ip>:{port}",
        "symbolStore": str(symbol_store),
        "logFile": str(log_file(symbol_store)),
        "updatedAt": time.strftime("%Y-%m-%d %H:%M:%S"),
        "requests": recent_requests(symbol_store),
        "rawLog": tail_lines(log_file(symbol_store), 120),
        "connections": port_connections(port),
        "job": job,
    }


def run_dashboard_job(name: str, command: list[str]) -> bool:
    with DASHBOARD_JOB_LOCK:
        if DASHBOARD_JOB["running"]:
            return False
        DASHBOARD_JOB.update(
            {
                "running": True,
                "name": name,
                "startedAt": time.strftime("%Y-%m-%d %H:%M:%S"),
                "exitCode": None,
                "lines": [],
            }
        )

    def worker() -> None:
        proc = subprocess.Popen(
            command,
            cwd=str(REPO_ROOT),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert proc.stdout is not None
        for line in proc.stdout:
            with DASHBOARD_JOB_LOCK:
                lines = DASHBOARD_JOB["lines"]
                assert isinstance(lines, list)
                lines.append(line.rstrip("\r\n"))
                del lines[:-300]
        exit_code = proc.wait()
        with DASHBOARD_JOB_LOCK:
            DASHBOARD_JOB["running"] = False
            DASHBOARD_JOB["exitCode"] = exit_code

    threading.Thread(target=worker, daemon=True).start()
    return True


DASHBOARD_HTML = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>JSEngine Symbol Server</title>
<style>
:root { color-scheme: dark; font-family: Segoe UI, Arial, sans-serif; background: #101114; color: #e7e9ee; }
body { margin: 0; background: #101114; }
main { max-width: 1280px; margin: 0 auto; padding: 24px; }
header { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-bottom: 20px; }
h1 { margin: 0; font-size: 24px; font-weight: 650; }
.sub { color: #9ca3af; margin-top: 4px; font-size: 13px; }
.grid { display: grid; grid-template-columns: 360px 1fr; gap: 16px; }
.panel { background: #181a20; border: 1px solid #2b2f3a; border-radius: 8px; padding: 16px; }
.panel h2 { margin: 0 0 12px; font-size: 15px; font-weight: 650; }
.status { display: inline-flex; align-items: center; gap: 8px; padding: 6px 10px; border-radius: 999px; font-weight: 650; }
.on { background: #11391f; color: #76e39a; }
.off { background: #3b1b1b; color: #ff8f8f; }
.warn { background: #3c2d12; color: #ffd166; }
.buttons { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 14px; }
button { height: 36px; border: 1px solid #3a4050; border-radius: 6px; background: #222631; color: #f3f4f6; font-weight: 600; cursor: pointer; }
button:hover { background: #2b3040; }
button:disabled { opacity: 0.45; cursor: not-allowed; }
button:disabled:hover { background: #222631; }
button.primary { background: #19538f; border-color: #2770bd; }
button.danger { background: #552222; border-color: #793030; }
.kv { display: grid; grid-template-columns: 92px 1fr; gap: 8px; margin: 10px 0; font-size: 13px; }
.key { color: #9ca3af; }
.value { word-break: break-all; }
.hint { color: #9ca3af; font-size: 12px; margin-top: 10px; }
.empty { color: #9ca3af; padding: 18px 8px; font-size: 13px; }
.mono { font-family: Consolas, ui-monospace, monospace; }
code { background: #0d0f13; border: 1px solid #2b2f3a; padding: 2px 5px; border-radius: 4px; }
table { width: 100%; border-collapse: collapse; font-size: 12px; }
th, td { text-align: left; padding: 7px 8px; border-bottom: 1px solid #282c35; vertical-align: top; }
th { color: #9ca3af; font-weight: 650; }
.path { max-width: 520px; word-break: break-all; }
.ok { color: #76e39a; font-weight: 650; }
.bad { color: #ff8f8f; font-weight: 650; }
pre { margin: 0; white-space: pre-wrap; word-break: break-all; font-size: 12px; line-height: 1.45; color: #d1d5db; }
.tabs { display: flex; gap: 8px; margin-bottom: 12px; }
.tab { padding: 7px 10px; border-radius: 6px; border: 1px solid #343946; cursor: pointer; color: #cbd5e1; }
.tab.active { background: #273142; color: #fff; }
.hidden { display: none; }
@media (max-width: 900px) { .grid { grid-template-columns: 1fr; } header { align-items: flex-start; flex-direction: column; } }
</style>
</head>
<body>
<main>
  <header>
    <div>
      <h1>JSEngine Symbol Server</h1>
      <div class="sub">Local dashboard for symbols, requests, and publish jobs.</div>
    </div>
    <div id="statusBadge" class="status off">OFFLINE</div>
  </header>
  <div class="grid">
    <section class="panel">
      <h2>Control</h2>
      <div class="kv"><div class="key">Updated</div><div id="updatedAt" class="value">-</div></div>
      <div class="kv"><div class="key">Health</div><div id="health" class="value">-</div></div>
      <div class="kv"><div class="key">Meaning</div><div id="meaning" class="value">-</div></div>
      <div class="kv"><div class="key">PID</div><div id="pids" class="value">-</div></div>
      <div class="kv"><div class="key">URL</div><div id="urls" class="value">-</div></div>
      <div class="kv"><div class="key">VS Path</div><div id="vsPath" class="value">-</div></div>
      <div class="kv"><div class="key">Store</div><div id="store" class="value">-</div></div>
      <div class="buttons">
        <button id="startBtn" class="primary" onclick="act('start-server')">Start Server</button>
        <button id="restartBtn" onclick="act('restart')">Restart Server</button>
        <button id="stopBtn" class="danger" onclick="act('stop-server')">Stop Server</button>
        <button id="publishBtn" onclick="act('publish')">Publish Symbols</button>
        <button id="allBtn" onclick="act('all')">ReleaseBuild + Publish</button>
        <button onclick="copyTeamUrl()">Copy URL</button>
        <button onclick="refresh(true)">Refresh</button>
      </div>
      <div id="notice" class="hint"></div>
    </section>
    <section class="panel">
      <h2>Publish Job</h2>
      <div class="kv"><div class="key">State</div><div id="jobState" class="value">-</div></div>
      <div class="kv"><div class="key">Started</div><div id="jobStarted" class="value">-</div></div>
      <pre id="jobLog"></pre>
    </section>
  </div>
  <section class="panel" style="margin-top:16px">
    <div class="tabs">
      <div class="tab active" onclick="showTab('requests')">Requests</div>
      <div class="tab" onclick="showTab('connections')">Active Sockets</div>
      <div class="tab" onclick="showTab('raw')">Raw Log</div>
    </div>
    <div id="requestsTab">
      <table>
        <thead><tr><th>Time</th><th>IP</th><th>Method</th><th>Status</th><th>Path</th></tr></thead>
        <tbody id="requests"></tbody>
      </table>
    </div>
    <div id="connectionsTab" class="hidden">
      <div class="hint">Shows the symbol server listener and currently active TCP sessions. Closed sessions such as TIME_WAIT are hidden.</div>
      <table>
        <thead><tr><th>Local</th><th>Remote</th><th>State</th><th>PID</th></tr></thead>
        <tbody id="connections"></tbody>
      </table>
    </div>
    <div id="rawTab" class="hidden"><pre id="rawLog"></pre></div>
  </section>
</main>
<script>
let latest = null;
const esc = (v) => String(v ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
async function refresh() {
  const notice = document.getElementById('notice');
  try {
    const res = await fetch('/api/status?t=' + Date.now(), {cache: 'no-store'});
    latest = await res.json();
    notice.textContent = '';
  } catch (err) {
    const badge = document.getElementById('statusBadge');
    badge.className = 'status warn';
    badge.textContent = 'DASHBOARD LOST';
    notice.textContent = 'Dashboard API refresh failed. Reopen Dashboard.cmd if this keeps happening.';
    return;
  }
  const badge = document.getElementById('statusBadge');
  const broken = latest.hasListener && !latest.running;
  badge.className = 'status ' + (latest.running ? 'on' : (broken ? 'warn' : 'off'));
  badge.textContent = latest.running ? 'ONLINE' : (broken ? 'BROKEN' : 'OFFLINE');
  document.getElementById('updatedAt').textContent = latest.updatedAt || '-';
  document.getElementById('health').innerHTML = `<span class="${latest.running ? 'ok' : 'bad'}">${esc(latest.health?.message || '-')}</span>`;
  document.getElementById('meaning').textContent = latest.running
    ? 'Team members can download symbols from port 8080.'
    : (broken ? 'A process owns port 8080, but HTTP health check failed.' : 'Symbol server is stopped.');
  document.getElementById('pids').textContent = latest.pids.length ? latest.pids.join(', ') : '-';
  document.getElementById('urls').innerHTML = latest.urls.map(u => `<code>${esc(u)}</code>`).join('<br>');
  document.getElementById('vsPath').innerHTML = `<code>${esc(latest.vsPath)}</code>`;
  document.getElementById('store').textContent = latest.symbolStore;
  const job = latest.job;
  const busy = !!job.running;
  document.getElementById('startBtn').disabled = false;
  document.getElementById('restartBtn').disabled = false;
  document.getElementById('stopBtn').disabled = false;
  document.getElementById('publishBtn').disabled = false;
  document.getElementById('allBtn').disabled = false;
  document.getElementById('jobState').textContent = job.running ? `${job.name} running` : (job.exitCode === null ? 'idle' : `${job.name} exited ${job.exitCode}`);
  document.getElementById('jobStarted').textContent = job.startedAt || '-';
  document.getElementById('jobLog').textContent = (job.lines || []).join('\n');
  document.getElementById('requests').innerHTML = latest.requests.slice().reverse().map(r => {
    const cls = r.status.startsWith('2') ? 'ok' : (r.status.startsWith('4') || r.status.startsWith('5') ? 'bad' : '');
    return `<tr><td>${esc(r.time)}</td><td>${esc(r.ip)}</td><td>${esc(r.method)}</td><td class="${cls}">${esc(r.status)}</td><td class="path">${esc(r.path)}</td></tr>`;
  }).join('');
  const activeConnections = latest.connections.filter(c => c.state !== 'TIME_WAIT' && c.state !== 'CLOSE_WAIT');
  document.getElementById('connections').innerHTML = activeConnections.length
    ? activeConnections.map(c => `<tr><td>${esc(c.local)}</td><td>${esc(c.remote || '-')}</td><td>${esc(c.state)}</td><td>${esc(c.pid)}</td></tr>`).join('')
    : `<tr><td colspan="4"><div class="empty">No active socket sessions. LISTENING appears when the symbol server is on; team downloads appear briefly as ESTABLISHED.</div></td></tr>`;
  document.getElementById('rawLog').textContent = latest.rawLog.join('\n');
}
async function act(action) {
  const notice = document.getElementById('notice');
  notice.textContent = action + ' requested...';
  const res = await fetch('/api/action?t=' + Date.now(), {method:'POST', headers:{'Content-Type':'application/json'}, cache:'no-store', body:JSON.stringify({action})});
  const data = await res.json().catch(() => ({}));
  notice.textContent = data.message || (data.ok ? 'Done.' : 'Failed.');
  setTimeout(refresh, 400);
  setTimeout(refresh, 1400);
}
function copyTeamUrl() {
  const url = (latest?.urls || []).find(u => !u.includes('localhost')) || latest?.urls?.[0] || '';
  navigator.clipboard.writeText(url);
}
function showTab(name) {
  for (const tab of document.querySelectorAll('.tab')) tab.classList.remove('active');
  for (const pane of ['requests','connections','raw']) document.getElementById(pane + 'Tab').classList.add('hidden');
  document.querySelector(`.tab[onclick="showTab('${name}')"]`).classList.add('active');
  document.getElementById(name + 'Tab').classList.remove('hidden');
}
refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
"""


class DashboardHandler(http.server.BaseHTTPRequestHandler):
    symbol_store: Path = DEFAULT_SYMBOL_STORE
    symbol_port: int = 8080
    bind_address: str = "0.0.0.0"

    def send_json(self, data: object, status: int = 200) -> None:
        payload = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        route = self.path.split("?", 1)[0]
        if route == "/":
            payload = DASHBOARD_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
            self.send_header("Pragma", "no-cache")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        if route == "/api/status":
            self.send_json(dashboard_status(self.symbol_store, self.symbol_port))
            return
        self.send_error(404)

    def do_POST(self) -> None:
        route = self.path.split("?", 1)[0]
        if route != "/api/action":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length).decode("utf-8", errors="replace") if length else "{}"
        try:
            payload = json.loads(body)
        except json.JSONDecodeError:
            payload = {}
        action = str(payload.get("action", ""))
        if action == "restart":
            stop_server(self.symbol_store, self.symbol_port)
            start_server(self.symbol_store, self.symbol_port, self.bind_address)
            self.send_json({"ok": True, "message": "Symbol server restarted."})
            return
        if action == "start-server":
            start_server(self.symbol_store, self.symbol_port, self.bind_address)
            self.send_json({"ok": True, "message": "Symbol server started."})
            return
        if action == "stop-server":
            stop_server(self.symbol_store, self.symbol_port)
            self.send_json({"ok": True, "message": "Symbol server stopped."})
            return
        if action in ("publish", "all"):
            command = [
                sys.executable,
                str(Path(__file__).resolve()),
                action,
                "--symbol-store",
                str(self.symbol_store),
                "--port",
                str(self.symbol_port),
                "--bind",
                self.bind_address,
            ]
            started = run_dashboard_job(action, command)
            self.send_json({"ok": started, "message": "started" if started else "job already running"})
            return
        self.send_json({"ok": False, "message": "unknown action"}, 400)

    def log_message(self, format: str, *args: object) -> None:
        return


def serve_dashboard(symbol_store: Path, symbol_port: int, dashboard_port: int, bind: str, open_browser: bool) -> None:
    symbol_store.mkdir(parents=True, exist_ok=True)
    url = f"http://127.0.0.1:{dashboard_port}/"
    existing = listening_port_pids(dashboard_port)
    if existing:
        print(f"Dashboard is already running. PID={', '.join(str(pid) for pid in existing)}")
        print(f"Dashboard: {url}")
        if open_browser:
            webbrowser.open(url)
        return

    DashboardHandler.symbol_store = symbol_store
    DashboardHandler.symbol_port = symbol_port
    DashboardHandler.bind_address = bind
    server = http.server.ThreadingHTTPServer(("127.0.0.1", dashboard_port), DashboardHandler)
    print("JSEngine Symbol Server Dashboard")
    print(f"Dashboard: {url}")
    print(f"Symbols  : http://<host-ip>:{symbol_port}/")
    print("Press Ctrl+C to stop the dashboard. The symbol server keeps running.")
    if open_browser:
        webbrowser.open(url)
    server.serve_forever()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build, publish, and serve JSEngine symbols.")
    parser.add_argument(
        "command",
        choices=("build", "publish", "serve", "start-server", "stop-server", "status", "dashboard", "all"),
        nargs="?",
        default="all",
    )
    parser.add_argument("--symbol-store", default=str(DEFAULT_SYMBOL_STORE))
    parser.add_argument("--include-debug", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-source-index", action="store_true")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--dashboard-port", type=int, default=8090)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--no-open", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    symbol_store = Path(args.symbol_store)
    configs = target_configs(args.include_debug)

    try:
        if args.command == "all" and not args.skip_build:
            build_release_package()
            configs = ["Release"]
        elif args.command == "build" and not args.skip_build:
            build(configs)
        if args.command in ("publish", "all"):
            publish(symbol_store, configs, skip_source_index=args.skip_source_index, port=args.port)
        if args.command == "serve":
            serve_foreground(symbol_store, args.port, args.bind)
        elif args.command == "start-server":
            start_server(symbol_store, args.port, args.bind)
        elif args.command == "stop-server":
            stop_server(symbol_store, args.port)
        elif args.command == "status":
            server_status(symbol_store, args.port)
        elif args.command == "dashboard":
            serve_dashboard(symbol_store, args.port, args.dashboard_port, args.bind, not args.no_open)
        elif args.command == "all":
            start_server(symbol_store, args.port, args.bind)
        return 0
    except KeyboardInterrupt:
        print("\nStopped.")
        return 130
    except Exception as exc:
        print(f"\nERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
