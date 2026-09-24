#!/usr/bin/env python3
"""Repeatable iv vs sed/awk/grep/wc/tail bench. Not a product feature."""

from __future__ import annotations

import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time


def which(name: str) -> str | None:
    return shutil.which(name)


def median(xs: list[float]) -> float:
    return statistics.median(xs)


def p90(xs: list[float]) -> float:
    if len(xs) == 1:
        return xs[0]
    ys = sorted(xs)
    i = min(len(ys) - 1, max(0, int(round(0.9 * (len(ys) - 1)))))
    return ys[i]


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


def run_once(cmd: list[str], inplace_src: str | None) -> tuple[float, int | None, int]:
    work = None
    argv = list(cmd)
    if inplace_src is not None:
        work = inplace_src + ".work"
        shutil.copy2(inplace_src, work)
        argv = [work if a == "__FILE__" else a for a in argv]
    else:
        argv = [a for a in argv]
    time_bin = which("/usr/bin/time") or "time"
    wrapped = [time_bin, "-f", "%e %M", "--"] + argv
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


def bench(name: str, cmd: list[str], trials: int, inplace: str | None) -> dict:
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
    # ~27 bytes/line typical; mixed fields + a token for subst/filter
    with open(path, "w", encoding="ascii") as f:
        for i in range(nlines):
            f.write(f"name{i},foo,{i},DEBUG extra {i}\n")


def main() -> int:
    iv = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./iv")
    trials = int(os.environ.get("IV_BENCH_TRIALS", "7"))
    sizes = [int(x) for x in os.environ.get("IV_BENCH_LINES", "200000,1000000,3000000").split(",") if x]
    outdir = tempfile.mkdtemp(prefix="iv-heavy-")
    print(f"iv={iv}")
    print(f"trials={trials} sizes={sizes} tmp={outdir}")
    print(f"sed={subprocess.check_output(['sed', '--version'], text=True).splitlines()[0]}")
    awk = which("awk")
    print(f"awk={subprocess.check_output([awk, '-W', 'version'], text=True).splitlines()[0] if awk else '?'}")
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
                (f"{n} iv -s stdout lit", [iv, "-s", src, "foo", "FOO", "-g", "--stdout", "-q"], None),
                (f"{n} sed stdout lit", ["sed", "s/foo/FOO/g", src], None),
                (f"{n} awk stdout lit", ["awk", "{gsub(/foo/,\"FOO\")}1", src], None),
                (f"{n} iv -s inplace lit", [iv, "-s", "__FILE__", "foo", "FOO", "-g", "--no-backup", "-q"], src),
                (f"{n} sed -i lit", ["sed", "-i", "s/foo/FOO/g", "__FILE__"], src),
                (f"{n} iv -s -E stdout", [iv, "-s", src, "name([0-9]+)", r"id\1", "-E", "--stdout", "-q"], None),
                (f"{n} sed -E stdout", ["sed", "-E", r"s/name([0-9]+)/id\1/", src], None),
                (f"{n} iv -d -m stdout", [iv, "-d", src, "-m", "DEBUG", "--stdout", "-q"], None),
                (f"{n} sed /DEBUG/d", ["sed", "/DEBUG/d", src], None),
                (f"{n} awk !/DEBUG/", ["awk", "!/DEBUG/", src], None),
                (f"{n} iv -s -F stdout", [iv, "-s", src, "-F", ",", "3", "X", "--stdout", "-q"], None),
                (f"{n} awk -F, field", ["awk", "-F,", 'BEGIN{OFS=","} {$3="X"}1', src], None),
                (f"{n} iv -d -3--1 inplace", [iv, "-d", "__FILE__", "-3--1", "--no-backup", "-q"], src),
                (f"{n} iv -wc", [iv, "-wc", src], None),
                (f"{n} wc -l", ["wc", "-l", src], None),
                (f"{n} iv -nv", [iv, "-nv", src, "DEBUG", "--no-numbers"], None),
                (f"{n} grep -F", ["grep", "-F", "DEBUG", src], None),
                (f"{n} iv -va -3--1", [iv, "-va", "-3--1", src, "--no-numbers"], None),
                (f"{n} tail -n 3", ["tail", "-n", "3", src], None),
            ]
            for name, cmd, inplace in jobs:
                # skip missing binaries
                if not which(cmd[0]) and not os.path.isfile(cmd[0]):
                    print(f"SKIP {name}: no {cmd[0]}", flush=True)
                    continue
                rec = bench(name, cmd, trials, inplace)
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

    print("=== ratio iv/peer (median, same size; <1 iv faster) ===")
    by_size: dict[int, dict[str, dict]] = {}
    for r in rows:
        by_size.setdefault(r["lines"], {})[r["name"].split(" ", 1)[1]] = r

    pairs = [
        ("iv -s stdout lit", "sed stdout lit"),
        ("iv -s stdout lit", "awk stdout lit"),
        ("iv -s inplace lit", "sed -i lit"),
        ("iv -s -E stdout", "sed -E stdout"),
        ("iv -d -m stdout", "sed /DEBUG/d"),
        ("iv -d -m stdout", "awk !/DEBUG/"),
        ("iv -s -F stdout", "awk -F, field"),
        ("iv -wc", "wc -l"),
        ("iv -nv", "grep -F"),
        ("iv -va -3--1", "tail -n 3"),
    ]
    for n in sizes:
        print(f"-- {n} lines --")
        bag = by_size.get(n, {})
        for a, b in pairs:
            if a in bag and b in bag and bag[b]["med"] > 0:
                ratio = bag[a]["med"] / bag[b]["med"]
                print(f"  {a:22s} / {b:18s} = {ratio:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
