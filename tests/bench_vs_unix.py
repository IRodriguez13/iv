#!/usr/bin/env python3
"""iv vs GNU sed / awk on the jobs iv claims. Not a product feature.

Scope (RFC): one-pass explicit edits. Pipe to grep/tail/wc; do not bench them.
  -s literal / -s -E / -s inplace  vs GNU sed
  -d -m                            vs GNU sed / awk
  -s -F field replace              vs awk

Env:
  IV_BENCH_TRIALS     default 11
  IV_BENCH_LINES      default 200000,1000000,3000000
  IV_BENCH_SED        path to sed (default: PATH)
  IV_BENCH_AWK        path to awk
  IV_BENCH_TASKSET    e.g. 0-3  (empty = no pin)
  IV_BENCH_WARMUP     discarded runs, default 1
"""

from __future__ import annotations

import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time


def which(name: str) -> str | None:
    if os.path.isfile(name) and os.access(name, os.X_OK):
        return name
    return shutil.which(name)


def median(xs: list[float]) -> float:
    return statistics.median(xs)


def p90(xs: list[float]) -> float:
    if len(xs) == 1:
        return xs[0]
    ys = sorted(xs)
    i = min(len(ys) - 1, max(0, int(round(0.9 * (len(ys) - 1)))))
    return ys[i]


def first_line(cmd: list[str]) -> str:
    try:
        out = subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT)
        return out.splitlines()[0] if out else "?"
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        return "?"


def rss_kb_from_time(stderr: str) -> int | None:
    # GNU time -f '%e %M' is the last "sec rss" line; ignore iv's "Replaced N".
    for line in reversed(stderr.strip().splitlines()):
        parts = line.split()
        if len(parts) == 2:
            try:
                sec = float(parts[0])
                rss = int(float(parts[1]))
                if sec >= 0 and rss >= 0:
                    return rss
            except ValueError:
                continue
    return None


def prefix_taskset(argv: list[str]) -> list[str]:
    pin = os.environ.get("IV_BENCH_TASKSET", "0-3").strip()
    if not pin or not which("taskset"):
        return argv
    return ["taskset", "-c", pin] + argv


def run_once(cmd: list[str], inplace_src: str | None) -> tuple[float, int | None, int]:
    work = None
    argv = list(cmd)
    if inplace_src is not None:
        work = inplace_src + ".work"
        shutil.copy2(inplace_src, work)
        argv = [work if a == "__FILE__" else a for a in argv]
    time_bin = which("/usr/bin/time") or "time"
    wrapped = prefix_taskset([time_bin, "-f", "%e %M", "--"] + argv)
    t0 = time.perf_counter()
    r = subprocess.run(
        wrapped,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    wall = time.perf_counter() - t0
    rss = rss_kb_from_time(r.stderr)
    if work and os.path.exists(work):
        os.remove(work)
    return wall, rss, r.returncode


def bench(name: str, cmd: list[str], trials: int, warmup: int, inplace: str | None) -> dict:
    for _ in range(warmup):
        run_once(cmd, inplace)
    walls: list[float] = []
    rsses: list[int] = []
    rc = 0
    for _ in range(trials):
        w, rss, rc = run_once(cmd, inplace)
        walls.append(w)
        if rss is not None:
            rsses.append(rss)
    return {
        "name": name,
        "cmd": " ".join(cmd),
        "n": trials,
        "min": min(walls),
        "med": median(walls),
        "p90": p90(walls),
        "rss": min(rsses) if rsses else None,
        "rc": rc,
    }


def write_corpus(path: str, nlines: int) -> None:
    with open(path, "w", encoding="ascii") as f:
        for i in range(nlines):
            f.write(f"name{i},foo,{i},DEBUG extra {i}\n")


def tool(env_name: str, fallback: str) -> str:
    return os.environ.get(env_name, fallback)


def main() -> int:
    iv = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./iv")
    trials = int(os.environ.get("IV_BENCH_TRIALS", "11"))
    warmup = int(os.environ.get("IV_BENCH_WARMUP", "1"))
    sizes = [
        int(x)
        for x in os.environ.get("IV_BENCH_LINES", "200000,1000000,3000000").split(",")
        if x
    ]
    sed = tool("IV_BENCH_SED", "sed")
    awk = tool("IV_BENCH_AWK", which("awk") or "awk")
    outdir = tempfile.mkdtemp(prefix="iv-heavy-")

    print(f"iv={iv}")
    print(f"host={platform.node()} {platform.machine()} {platform.release()}")
    print(f"trials={trials} warmup={warmup} sizes={sizes} pin={os.environ.get('IV_BENCH_TASKSET', '0-3')}")
    print(f"tmp={outdir}")
    print(f"iv version: {first_line([iv, '-V'])}")
    print(f"sed: {sed}")
    print(f"     {first_line([sed, '--version'])}")
    print(f"awk: {awk}")
    print(f"     {first_line([awk, '-W', 'version'])}")
    print("peers: sed, awk  (not grep/tail/wc — pipe to those)")
    print()

    rows: list[dict] = []
    try:
        for n in sizes:
            src = os.path.join(outdir, f"n{n}.txt")
            print(f"-- generating {n} lines --", flush=True)
            write_corpus(src, n)
            nbytes = os.path.getsize(src)
            print(f"   bytes={nbytes}", flush=True)

            jobs: list[tuple[str, list[str], str | None]] = [
                # compete
                (f"{n} iv -s stdout lit", [iv, "-s", src, "foo", "FOO", "-g", "--stdout", "-q"], None),
                (f"{n} sed stdout lit", [sed, "s/foo/FOO/g", src], None),
                (f"{n} awk stdout lit", [awk, "{gsub(/foo/,\"FOO\")}1", src], None),
                (f"{n} iv -s inplace lit", [iv, "-s", "__FILE__", "foo", "FOO", "-g", "-q"], src),
                (f"{n} sed -i lit", [sed, "-i", "s/foo/FOO/g", "__FILE__"], src),
                (f"{n} iv -s -E stdout", [iv, "-s", src, "name([0-9]+)", r"id\1", "-E", "--stdout", "-q"], None),
                (f"{n} sed -E stdout", [sed, "-E", r"s/name([0-9]+)/id\1/", src], None),
                (f"{n} iv -d -m stdout", [iv, "-d", src, "-m", "DEBUG", "--stdout", "-q"], None),
                (f"{n} sed /DEBUG/d", [sed, "/DEBUG/d", src], None),
                (f"{n} awk !/DEBUG/", [awk, "!/DEBUG/", src], None),
                (f"{n} iv -s -F stdout", [iv, "-s", src, "-F", ",", "3", "X", "--stdout", "-q"], None),
                (f"{n} awk -F, field", [awk, "-F,", 'BEGIN{OFS=","} {$3="X"}1', src], None),
                (f"{n} iv -d -3--1 inplace", [iv, "-d", "__FILE__", "-3--1", "-q"], src),
            ]
            for name, cmd, inplace in jobs:
                if not which(cmd[0]) and not os.path.isfile(cmd[0]):
                    print(f"SKIP {name}: no {cmd[0]}", flush=True)
                    continue
                rec = bench(name, cmd, trials, warmup, inplace)
                rec["lines"] = n
                rec["bytes"] = nbytes
                rows.append(rec)
                rss = f"{rec['rss']}KB" if rec["rss"] else "?"
                print(
                    f"{rec['name']:<36} min={rec['min']:.3f}s  med={rec['med']:.3f}s  "
                    f"p90={rec['p90']:.3f}s  rss={rss}  rc={rec['rc']}",
                    flush=True,
                )
            print(flush=True)
    finally:
        shutil.rmtree(outdir, ignore_errors=True)

    by_size: dict[int, dict[str, dict]] = {}
    for r in rows:
        by_size.setdefault(r["lines"], {})[r["name"].split(" ", 1)[1]] = r

    compete = [
        ("iv -s stdout lit", "sed stdout lit"),
        ("iv -s stdout lit", "awk stdout lit"),
        ("iv -s inplace lit", "sed -i lit"),
        ("iv -s -E stdout", "sed -E stdout"),
        ("iv -d -m stdout", "sed /DEBUG/d"),
        ("iv -d -m stdout", "awk !/DEBUG/"),
        ("iv -s -F stdout", "awk -F, field"),
    ]

    print("=== iv vs sed/awk  (median; <1 iv faster) ===")
    for n in sizes:
        print(f"-- {n} lines --")
        bag = by_size.get(n, {})
        for a, b in compete:
            if a in bag and b in bag and bag[b]["med"] > 0:
                ratio = bag[a]["med"] / bag[b]["med"]
                print(
                    f"  {a:22s} / {b:18s} = {ratio:.2f}"
                    f"   ({bag[a]['med']*1000:.0f}ms / {bag[b]['med']*1000:.0f}ms)"
                )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
