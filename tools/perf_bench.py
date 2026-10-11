#!/usr/bin/env python3
"""Repeatable benchmark runs and comparisons for performance experiments.

  python tools/perf_bench.py run --suite render --runs 3 --out base.json
  python tools/perf_bench.py compare base.json new1.json new2.json --target renderworld_us

`run` executes the benchmark cases of the named suites (or `--case
Runner:Exact.Name`) several times and records the median of every metric.
Metrics come from `BENCH-JSON {...}` lines, one-line JSON objects (the
runtime's `--bench-frames` report) and `key=value` pairs on benchmark output
lines. A metric's direction (higher or lower is better) follows its name;
other numbers are reported but never gate.

`compare` prints every directional metric's change. A change counts only when
it exceeds both `--threshold` percent and the run-to-run spread of either
side, the two sides' runs do not overlap, and every new measurement passed
(two independent `run`s are recommended; one is not enough at ~180 metrics).
With `--target`, it prints the gate: PASS when a matching metric improves and
no metric regresses, otherwise FAIL (exit code 1).
"""

import argparse
import datetime
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Exact cases per suite. Benchmarks print results and pass; they do not assert
# speed, so a failing case means the measurement itself broke.
SUITES = {
    "sim": [
        ("IllumoGameTests", "IllumoGame.Sim.MicroBench"),
        ("IllumoGameTests", "IllumoGame.Sim.SparseMicroBench"),
        ("IllumoGameTests", "IllumoGame.Sim.RunnerBench"),
        ("IllumoGameTests", "IllumoGame.Sim.ElementaryHistoryBench"),
    ],
    "topology": [
        ("IllumoGameTests", "IllumoGame.WorldTopology.SoupBench"),
        ("IllumoGameTests", "IllumoGame.WorldTopology.MicroBench"),
    ],
    "canvas": [
        ("IllumoGameTests", "IllumoGame.CanvasInf.DenseSoupPresentationBench"),
        ("IllumoGameTests", "IllumoGame.CanvasInf.FrameLatencyBench"),
        ("IllumoGameTests", "IllumoGame.Sim.FrameLatencyBench"),
    ],
    "render": [
        ("IllumoTests", "Illumo.RenderWorld.Bench"),
        ("IllumoTests", "Illumo.VisualStore.Bench"),
        ("IllumoTests", "Illumo.GameVisual.RebuildBench"),
        ("IllumoTests", "Illumo.SceneGraph.CullingMicroBench"),
    ],
    "package": [
        ("IllumoGameWasmPackageTests", "IllumoGame.Wasm.PackageBench"),
    ],
}

HIGHER = re.compile(r"(/s$|[Gg]ps$|[Ff]ps$|[Tt]ps$|[Gg]ps\b)")
LOWER = re.compile(r"(^ms|Ms|_ms|_us|^us|_ns|[Bb]ytes|^seconds$)")
PAIR = re.compile(r"^([A-Za-z_][\w./-]*)=(-?\d+(?:\.\d+)?(?:[eE]-?\d+)?)%?$")


def direction(key):
    if HIGHER.search(key):
        return "higher"
    if LOWER.search(key):
        return "lower"
    return "info"


def flatten(prefix, value, out):
    if isinstance(value, dict):
        for key, child in value.items():
            flatten(f"{prefix}.{key}" if prefix else key, child, out)
    elif isinstance(value, bool):
        return
    elif isinstance(value, (int, float)):
        out[prefix] = float(value)


def json_metrics(case, record):
    """Strings and integer descriptors identify the line; numbers are metrics."""
    labels = [f"{k}={v}" for k, v in record.items() if isinstance(v, str)]
    for key in ("lanes", "instances", "count"):
        if isinstance(record.get(key), int):
            labels.append(f"{key}={record[key]}")
    numbers = {}
    flatten("", {k: v for k, v in record.items() if not isinstance(v, str)}, numbers)
    head = f"{case} [{' '.join(labels)}]"
    return {f"{head} {key}": value for key, value in numbers.items()
            if key not in ("lanes", "instances", "count")}


def text_metrics(line):
    """Return (head, params, {name: value}) for a key=value line. Integer pairs
    before the first directional one (instances=2000, updates=727) are
    parameters: shown, never part of a name, since some vary between runs."""
    head, params, pending, metrics = [], [], [], {}
    seen_metric = False
    for raw in line.split():
        token = raw.strip("(),;")
        match = PAIR.match(token)
        if not match:
            # Unparsed pairs such as hits/probes=760/768 would make names vary.
            if "=" not in token:
                (pending if (params or seen_metric) else head).append(raw)
            continue
        key, value = match.group(1), match.group(2)
        if not seen_metric and direction(key) == "info" and re.fullmatch(r"-?\d+", value):
            params.append(token)
            continue
        seen_metric = True
        metrics[" ".join(pending + [key])] = float(value)
        pending = []
    return " ".join(head), " ".join(params), metrics


def parse_output(case, output):
    """Return ({metric id: value}, {metric id: parameter text})."""
    metrics, labels, occurrences = {}, {}, {}
    for line in output.splitlines():
        line = line.strip()
        if line.startswith("BENCH-JSON "):
            line = line[len("BENCH-JSON "):]
        if line.startswith("{"):
            try:
                metrics.update(json_metrics(case, json.loads(line)))
            except json.JSONDecodeError:
                pass
            continue
        if "=" not in line or line.startswith(("=", "FAILED", "PASSED")):
            continue
        head, params, values = text_metrics(line)
        if not values:
            continue
        # Lines sharing a head (one per instance count, say) are told apart by
        # their order, which the benchmark fixes.
        occurrences[head] = occurrences.get(head, 0) + 1
        suffix = f" #{occurrences[head]}" if occurrences[head] > 1 else ""
        for name, value in values.items():
            key = f"{case} {head}{suffix} :: {name}"
            metrics[key] = value
            if params:
                labels[key] = params
    return metrics, labels


def git(*arguments):
    result = subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True, text=True)
    return result.stdout.strip()


def resolve_build(build_dir, config):
    if build_dir:
        return Path(build_dir) / config
    for name in ("build-workspace", "build"):
        if (ROOT / name / config).is_dir():
            return ROOT / name / config
    sys.exit(f"no {config} build found; pass --build-dir")


def command_run(arguments):
    cases = []
    for suite in arguments.suite or []:
        if suite not in SUITES:
            sys.exit(f"unknown suite {suite}; known: {', '.join(SUITES)}")
        cases.extend(SUITES[suite])
    for spec in arguments.case or []:
        runner, _, name = spec.partition(":")
        cases.append((runner, name))
    if not cases:
        sys.exit("nothing to run: pass --suite or --case")
    binaries = resolve_build(arguments.build_dir, arguments.config)

    samples, parameters, timings, failures = {}, {}, {}, []
    for run in range(arguments.runs):
        for runner, name in cases:
            executable = binaries / f"{runner}.exe"
            if not executable.exists():
                executable = binaries / runner
            started = time.monotonic()
            result = subprocess.run([str(executable), "--run", name], cwd=executable.parent,
                                    capture_output=True, text=True)
            elapsed = time.monotonic() - started
            timings.setdefault(name, []).append(round(elapsed, 2))
            print(f"run {run + 1}/{arguments.runs} {name}: exit {result.returncode}, {elapsed:.1f} s",
                  flush=True)
            if result.returncode != 0:
                failures.append(f"{name} run {run + 1}: exit {result.returncode}")
                continue
            values, labels = parse_output(name, result.stdout)
            for key, value in values.items():
                samples.setdefault(key, []).append(value)
            parameters.update(labels)

    metrics = {}
    for key, values in sorted(samples.items()):
        median = statistics.median(values)
        spread = (max(values) - min(values)) / abs(median) if median else 0.0
        metrics[key] = {"direction": direction(key.rsplit(" ", 1)[-1]), "values": values,
                        "median": median, "spread": round(spread, 4)}
        if key in parameters:
            metrics[key]["parameters"] = parameters[key]
    report = {
        "created": datetime.datetime.now().isoformat(timespec="seconds"),
        "head": git("rev-parse", "--short", "HEAD"),
        "dirty": bool(git("status", "--porcelain", "--untracked-files=no")),
        "binaries": str(binaries),
        "runs": arguments.runs,
        "cases": [f"{runner}:{name}" for runner, name in cases],
        "seconds": timings,
        "failures": failures,
        "metrics": metrics,
    }
    Path(arguments.out).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    gated = sum(1 for m in metrics.values() if m["direction"] != "info")
    print(f"{len(metrics)} metrics ({gated} directional) -> {arguments.out}")
    if failures:
        print("FAILED cases: " + "; ".join(failures))
        return 1
    return 0


def verdict_of(b, n, threshold):
    """Return (delta percent, floor percent, verdict) for one measurement."""
    delta = (n["median"] - b["median"]) / abs(b["median"]) * 100.0
    gain = delta if b["direction"] == "higher" else -delta
    floor = max(threshold, b["spread"] * 100.0, n["spread"] * 100.0)
    # A real change also separates the runs: every new run beats (or trails)
    # every base run. Medians alone pass identical code at 3 runs.
    up = min(n["values"]) > max(b["values"])
    down = max(n["values"]) < min(b["values"])
    improved, regressed = (up, down) if b["direction"] == "higher" else (down, up)
    if improved and gain > floor:
        return delta, floor, "better"
    if regressed and gain < -floor:
        return delta, floor, "worse"
    return delta, floor, "noise"


def command_compare(arguments):
    base = json.loads(Path(arguments.base).read_text(encoding="utf-8"))["metrics"]
    news = [json.loads(Path(path).read_text(encoding="utf-8"))["metrics"]
            for path in arguments.new]
    new = news[0]
    rows, better, worse, target_better = [], [], [], []
    shared = set(base).intersection(*news)
    for key in sorted(shared):
        b = base[key]
        if b["direction"] == "info" or not b["median"]:
            continue
        # With hundreds of metrics, chance alone separates a few; a verdict
        # stands only when every independent measurement agrees.
        results = [verdict_of(b, n[key], arguments.threshold) for n in news]
        verdicts = {verdict for _, _, verdict in results}
        verdict = verdicts.pop() if len(verdicts) == 1 else "noise"
        delta, floor, _ = results[0]
        gain = delta if b["direction"] == "higher" else -delta
        rows.append((abs(gain), key, b["median"], new[key]["median"], delta, floor, verdict))
        if verdict == "better":
            better.append(key)
            if arguments.target and arguments.target in key:
                target_better.append(key)
        elif verdict == "worse":
            worse.append(key)
    for _, key, b, n, delta, floor, verdict in sorted(rows, reverse=True):
        if verdict != "noise" or arguments.all:
            shown = new[key].get("parameters")
            print(f"{verdict:6} {delta:+8.2f}% (floor {floor:.1f}%) {b:.4g} -> {n:.4g}  {key}"
                  + (f"  ({shown})" if shown else ""))
    missing = sorted(set(base).union(*news) - shared)
    print(f"{len(better)} better, {len(worse)} worse, {len(rows) - len(better) - len(worse)} within noise"
          + (f", {len(missing)} metrics only on one side" if missing else ""))
    if not arguments.target:
        return 0
    passed = bool(target_better) and not worse
    print(f"gate: {'PASS' if passed else 'FAIL'} (target '{arguments.target}': "
          f"{len(target_better)} improved; {len(worse)} regressions)")
    return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    run = commands.add_parser("run", help="run benchmark cases and record medians")
    run.add_argument("--suite", action="append", help=f"one of {', '.join(SUITES)}")
    run.add_argument("--case", action="append", help="Runner:Exact.Case.Name")
    run.add_argument("--runs", type=int, default=3)
    run.add_argument("--config", default="Release")
    run.add_argument("--build-dir", help="build tree (default build-workspace, then build)")
    run.add_argument("--out", required=True)
    compare = commands.add_parser("compare", help="compare two run reports")
    compare.add_argument("base")
    compare.add_argument("new", nargs="+", help="one or more independent measurements")
    compare.add_argument("--threshold", type=float, default=2.0, help="minimum percent change")
    compare.add_argument("--target", help="substring of the metric the experiment must improve")
    compare.add_argument("--all", action="store_true", help="also list changes within noise")
    arguments = parser.parse_args()
    return command_run(arguments) if arguments.command == "run" else command_compare(arguments)


if __name__ == "__main__":
    sys.exit(main())
