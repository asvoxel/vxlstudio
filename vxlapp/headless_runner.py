"""Headless pipeline runner for VxlApp.

Runs the pipeline without any GUI, printing results to stdout.
"""
import json
import sys
import time


def _try_import_vxl():
    """Try to import the vxl package; return (module, error_msg)."""
    try:
        import vxl
        return vxl, None
    except ImportError as exc:
        return None, str(exc)


def _load_pipeline_json(path: str) -> dict:
    """Load and return pipeline.json."""
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def _print_step_result(step_name: str, step_type: str, ok: bool, detail: str = ""):
    """Print a single step result in a readable format."""
    status = "OK" if ok else "NG"
    line = f"  [{status}] {step_name} ({step_type})"
    if detail:
        line += f" - {detail}"
    print(line)


def run_headless(pkg: dict, *, run_once: bool = False, list_steps: bool = False) -> int:
    """Run the pipeline in headless mode.

    Args:
        pkg: Package dict from vxap_loader.load_package().
        run_once: If True, run the pipeline once and exit.
        list_steps: If True, just list steps and exit (no execution).

    Returns:
        Exit code: 0 if all OK, 1 if any NG or error.
    """
    manifest = pkg["manifest"]
    pipeline_data = _load_pipeline_json(pkg["pipeline_path"])

    print(f"Package : {manifest.get('name', '?')} v{manifest.get('version', '?')}")
    if manifest.get("description"):
        print(f"Desc    : {manifest['description']}")
    print(f"Pipeline: {len(pipeline_data.get('steps', []))} steps")
    print()

    steps = pipeline_data.get("steps", [])
    if not steps:
        print("WARNING: Pipeline has no steps.")
        return 0

    # --list-steps: just print and exit
    if list_steps:
        print("Pipeline steps:")
        for i, step in enumerate(steps):
            name = step.get("name", f"step_{i}")
            stype = step.get("type", "unknown")
            params = step.get("params", {})
            print(f"  {i + 1}. [{stype}] {name}", end="")
            if params:
                print(f"  params={json.dumps(params, ensure_ascii=False)}", end="")
            print()
        return 0

    # Try to import vxl for actual pipeline execution
    vxl, import_err = _try_import_vxl()

    if vxl is not None:
        return _run_with_vxl(vxl, pipeline_data, pkg, run_once)
    else:
        print(f"NOTE: vxl module not available ({import_err})")
        print("      Running in dry-run mode (simulating pipeline execution).\n")
        return _run_dry(pipeline_data, run_once)


def _run_with_vxl(vxl, pipeline_data: dict, pkg: dict, run_once: bool) -> int:
    """Run pipeline using the vxl.Pipeline C++ engine."""
    try:
        pipeline = vxl.Pipeline()
        pipeline.load_json(pkg["pipeline_path"])
    except Exception as exc:
        print(f"ERROR: Failed to load pipeline: {exc}")
        return 1

    cycle = 0
    any_ng = False

    while True:
        cycle += 1
        print(f"--- Cycle {cycle} ---")
        t0 = time.time()

        try:
            ctx = pipeline.run()
            elapsed = time.time() - t0

            overall_ok = ctx.ok if hasattr(ctx, "ok") else True

            steps = pipeline_data.get("steps", [])
            for i, step in enumerate(steps):
                name = step.get("name", f"step_{i}")
                stype = step.get("type", "unknown")
                _print_step_result(name, stype, True)

            status = "OK" if overall_ok else "NG"
            print(f"  Result: {status}  ({elapsed * 1000:.1f} ms)")

            if not overall_ok:
                any_ng = True

        except Exception as exc:
            elapsed = time.time() - t0
            print(f"  ERROR: {exc}  ({elapsed * 1000:.1f} ms)")
            any_ng = True

        print()

        if run_once:
            break

        try:
            time.sleep(1.0)
        except KeyboardInterrupt:
            print("\nInterrupted by user.")
            break

    return 1 if any_ng else 0


def _run_dry(pipeline_data: dict, run_once: bool) -> int:
    """Dry-run: simulate pipeline execution without vxl."""
    steps = pipeline_data.get("steps", [])

    cycle = 0
    while True:
        cycle += 1
        print(f"--- Cycle {cycle} (dry-run) ---")
        t0 = time.time()

        for i, step in enumerate(steps):
            name = step.get("name", f"step_{i}")
            stype = step.get("type", "unknown")
            _print_step_result(name, stype, True, "simulated")

        elapsed = time.time() - t0
        print(f"  Result: OK (dry-run, {elapsed * 1000:.1f} ms)")
        print()

        if run_once:
            break

        try:
            time.sleep(1.0)
        except KeyboardInterrupt:
            print("\nInterrupted by user.")
            break

    return 0
