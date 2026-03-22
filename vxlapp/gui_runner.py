"""GUI runner for VxlApp using Dear PyGui.

If dearpygui is not installed, automatically falls back to headless mode.
"""
import json
import threading
import time
from collections import deque


def _try_import_dpg():
    """Try to import Dear PyGui; return (module, error_msg)."""
    try:
        import dearpygui.dearpygui as dpg
        return dpg, None
    except ImportError as exc:
        return None, str(exc)


def _try_import_vxl():
    """Try to import the vxl package; return (module, error_msg)."""
    try:
        import vxl
        return vxl, None
    except ImportError as exc:
        return None, str(exc)


# ---------------------------------------------------------------------------
# Shared mutable state between GUI thread and pipeline thread
# ---------------------------------------------------------------------------
def _make_state():
    return {
        "running": False,
        "loop_mode": False,
        "cycle": 0,
        "last_result": "---",
        "last_time_ms": 0.0,
        "step_results": [],        # list of (status_str, elapsed_s)
        "total_ok": 0,
        "total_ng": 0,
        "stop_requested": False,
        "log_lines": deque(maxlen=200),
        "fps": 0.0,
        "_cycle_times": deque(maxlen=10),
    }


# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------
_COL_GREEN  = (0, 220, 60, 255)
_COL_RED    = (220, 40, 40, 255)
_COL_GREY   = (180, 180, 180, 255)
_COL_YELLOW = (255, 200, 0, 255)
_COL_WHITE  = (240, 240, 240, 255)


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------
def run_gui(pkg: dict) -> int:
    """Run the pipeline with a Dear PyGui GUI.

    Falls back to headless mode if dearpygui is not available.

    Args:
        pkg: Package dict from vxap_loader.load_package().

    Returns:
        Exit code: 0 if all OK, 1 if any NG or error.
    """
    dpg, dpg_err = _try_import_dpg()
    if dpg is None:
        print(f"Dear PyGui not available ({dpg_err}), falling back to headless mode.")
        from vxlapp.headless_runner import run_headless
        return run_headless(pkg, run_once=False)

    manifest = pkg["manifest"]
    with open(pkg["pipeline_path"], "r", encoding="utf-8") as f:
        pipeline_data = json.load(f)

    steps = pipeline_data.get("steps", [])
    vxl, vxl_err = _try_import_vxl()

    state = _make_state()

    # --- Dear PyGui context --------------------------------------------------
    dpg.create_context()

    app_title = f"VxlApp - {manifest.get('name', '?')}"

    # -- Font / theme ---------------------------------------------------------
    with dpg.font_registry():
        default_font = dpg.add_font(dpg.mvFontRangeHint_Default, 16) if False else None

    with dpg.theme() as ok_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, _COL_GREEN)
    with dpg.theme() as ng_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, _COL_RED)
    with dpg.theme() as idle_theme:
        with dpg.theme_component(dpg.mvAll):
            dpg.add_theme_color(dpg.mvThemeCol_Text, _COL_GREY)

    # -- Main window ----------------------------------------------------------
    with dpg.window(label=app_title, tag="main_window"):
        # Top info
        dpg.add_text(
            f"Package: {manifest.get('name', '?')}  "
            f"v{manifest.get('version', '?')}  "
            f"{manifest.get('description', '')}",
            tag="pkg_info",
        )
        dpg.add_separator()

        # Two-column layout via groups
        with dpg.group(horizontal=True):
            # ===== LEFT: Control panel ===================================
            with dpg.child_window(width=200, tag="left_panel"):
                dpg.add_text("Control", color=_COL_WHITE)
                dpg.add_separator()

                dpg.add_button(
                    label="Run 1x", tag="btn_once", width=-1,
                    callback=lambda: _start_pipeline(
                        state, pkg, steps, vxl, dpg, once=True),
                )
                dpg.add_button(
                    label="Loop", tag="btn_loop", width=-1,
                    callback=lambda: _start_pipeline(
                        state, pkg, steps, vxl, dpg, once=False),
                )
                dpg.add_button(
                    label="Stop", tag="btn_stop", width=-1, enabled=False,
                    callback=lambda: _stop_pipeline(state, dpg),
                )
                dpg.add_separator()

                # Stats
                dpg.add_text("Stats:", color=_COL_WHITE)
                dpg.add_text("Total: 0", tag="stat_total")
                dpg.add_text("Pass:  0", tag="stat_pass")
                dpg.add_text("NG:    0", tag="stat_ng")
                dpg.add_text("Yield: -", tag="stat_yield")
                dpg.add_separator()

                # Pipeline steps list
                dpg.add_text("Pipeline Steps:", color=_COL_WHITE)
                for i, step in enumerate(steps):
                    name = step.get("name", f"step_{i}")
                    stype = step.get("type", "unknown")
                    dpg.add_text(f" {i+1}. {name}", tag=f"step_list_{i}")

                # vxl availability warning
                if vxl is None:
                    dpg.add_separator()
                    dpg.add_text("[!] vxl not available", color=_COL_YELLOW)
                    dpg.add_text("    Dry-run mode", color=_COL_YELLOW)

            # ===== RIGHT: Result display =================================
            with dpg.child_window(tag="right_panel"):
                dpg.add_text("Result Display", color=_COL_WHITE)
                dpg.add_separator()

                # Big OK/NG indicator
                with dpg.child_window(height=80, border=True, tag="result_box"):
                    dpg.add_text("---", tag="result_big")

                dpg.add_spacer(height=8)

                # Step results detail
                dpg.add_text("Step Results:", color=_COL_WHITE)
                for i, step in enumerate(steps):
                    name = step.get("name", f"step_{i}")
                    dpg.add_text(f"  {name}: ---", tag=f"step_result_{i}")

                dpg.add_spacer(height=8)
                dpg.add_separator()

                # Log area
                dpg.add_text("Log:", color=_COL_WHITE)
                with dpg.child_window(tag="log_area", autosize_x=True, height=-1):
                    dpg.add_text("", tag="log_text", wrap=0)

        # ===== BOTTOM: Status bar ========================================
    # Status bar as a separate window at the bottom
    with dpg.window(
        label="##statusbar", tag="status_bar_window",
        no_title_bar=True, no_resize=True, no_move=True,
        no_scrollbar=True, no_collapse=True, no_close=True,
    ):
        dpg.add_text("Status: Idle | Cycle: 0 | FPS: -", tag="status_bar")

    # -- Viewport & show ------------------------------------------------------
    dpg.create_viewport(title=app_title, width=900, height=650)
    dpg.setup_dearpygui()
    dpg.show_viewport()
    dpg.set_primary_window("main_window", True)

    # -- Main render loop -----------------------------------------------------
    while dpg.is_dearpygui_running():
        _update_gui(state, steps, dpg, ok_theme, ng_theme, idle_theme)
        _layout_status_bar(dpg)
        dpg.render_dearpygui_frame()

    state["stop_requested"] = True
    dpg.destroy_context()
    return 1 if state["total_ng"] > 0 else 0


# ---------------------------------------------------------------------------
# Status bar positioning
# ---------------------------------------------------------------------------
def _layout_status_bar(dpg):
    """Keep the status bar pinned at viewport bottom."""
    try:
        vw = dpg.get_viewport_width()
        vh = dpg.get_viewport_height()
        bar_h = 30
        dpg.configure_item("status_bar_window",
                           pos=[0, vh - bar_h - 40],
                           width=vw, height=bar_h)
    except Exception:
        pass


# ---------------------------------------------------------------------------
# Pipeline control
# ---------------------------------------------------------------------------
def _start_pipeline(state, pkg, steps, vxl, dpg, once=True):
    """Start pipeline execution in a background thread."""
    if state["running"]:
        return
    state["stop_requested"] = False
    state["running"] = True
    state["loop_mode"] = not once

    dpg.configure_item("btn_once", enabled=False)
    dpg.configure_item("btn_loop", enabled=False)
    dpg.configure_item("btn_stop", enabled=True)

    _log(state, "INFO", f"Pipeline {'loop' if not once else 'single-run'} started")

    t = threading.Thread(
        target=_pipeline_thread,
        args=(state, pkg, steps, vxl, once),
        daemon=True,
    )
    t.start()


def _stop_pipeline(state, dpg):
    """Request pipeline to stop."""
    state["stop_requested"] = True
    _log(state, "INFO", "Stop requested")


def _log(state, level, msg):
    """Append a log line to shared state."""
    ts = time.strftime("%H:%M:%S")
    state["log_lines"].append(f"[{ts}] [{level}] {msg}")


# ---------------------------------------------------------------------------
# Background pipeline thread
# ---------------------------------------------------------------------------
def _pipeline_thread(state, pkg, steps, vxl, once):
    """Background thread that runs the pipeline."""
    pipeline = None
    if vxl is not None:
        try:
            pipeline = vxl.Pipeline()
            pipeline.load_json(pkg["pipeline_path"])
            _log(state, "INFO", "Pipeline loaded from C++ engine")
        except Exception as exc:
            _log(state, "WARN", f"C++ pipeline load failed: {exc}")
            pipeline = None

    while not state["stop_requested"]:
        state["cycle"] += 1
        cycle_num = state["cycle"]
        state["last_result"] = "RUNNING"
        step_results = []

        _log(state, "INFO", f"Cycle {cycle_num} started")
        t0 = time.time()

        if pipeline is not None:
            try:
                ctx = pipeline.run()
                for i, step in enumerate(steps):
                    name = step.get("name", f"step_{i}")
                    step_results.append(("OK", 0.0))
                overall_ok = ctx.ok if hasattr(ctx, "ok") else True
            except Exception as exc:
                for i, step in enumerate(steps):
                    step_results.append(("ERR", 0.0))
                overall_ok = False
                _log(state, "ERROR", f"Pipeline error: {exc}")
        else:
            # Dry-run simulation
            for i, step in enumerate(steps):
                name = step.get("name", f"step_{i}")
                stype = step.get("type", "unknown")
                t_step = time.time()
                # Simulate some work
                time.sleep(0.02)
                elapsed_step = time.time() - t_step
                step_results.append(("OK", elapsed_step))
                _log(state, "INFO", f"  {name} ({stype}): OK ({elapsed_step:.3f}s)")
            overall_ok = True

        elapsed = (time.time() - t0) * 1000
        state["last_time_ms"] = elapsed
        state["step_results"] = step_results

        # Track cycle time for FPS
        state["_cycle_times"].append(time.time() - t0)
        if len(state["_cycle_times"]) >= 2:
            avg = sum(state["_cycle_times"]) / len(state["_cycle_times"])
            state["fps"] = 1.0 / avg if avg > 0 else 0.0

        if overall_ok:
            state["last_result"] = "OK"
            state["total_ok"] += 1
            _log(state, "INFO", f"Cycle {cycle_num} result: OK ({elapsed:.1f}ms)")
        else:
            state["last_result"] = "NG"
            state["total_ng"] += 1
            _log(state, "WARN", f"Cycle {cycle_num} result: NG ({elapsed:.1f}ms)")

        if once or state["stop_requested"]:
            break

        time.sleep(0.1)

    state["running"] = False
    _log(state, "INFO", "Pipeline stopped")


# ---------------------------------------------------------------------------
# GUI update (called each frame from main thread)
# ---------------------------------------------------------------------------
def _update_gui(state, steps, dpg, ok_theme, ng_theme, idle_theme):
    """Update GUI elements from shared state (called each frame)."""
    try:
        # -- Buttons ---
        if not state["running"]:
            dpg.configure_item("btn_once", enabled=True)
            dpg.configure_item("btn_loop", enabled=True)
            dpg.configure_item("btn_stop", enabled=False)

        # -- Big result indicator ---
        result = state["last_result"]
        if result == "OK":
            dpg.set_value("result_big", "    OK")
            dpg.bind_item_theme("result_big", ok_theme)
        elif result == "NG":
            dpg.set_value("result_big", "    NG")
            dpg.bind_item_theme("result_big", ng_theme)
        elif result == "RUNNING":
            dpg.set_value("result_big", "    RUNNING...")
            dpg.bind_item_theme("result_big", idle_theme)
        else:
            dpg.set_value("result_big", "    ---")
            dpg.bind_item_theme("result_big", idle_theme)

        # -- Stats ---
        total = state["total_ok"] + state["total_ng"]
        dpg.set_value("stat_total", f"Total: {total}")
        dpg.set_value("stat_pass", f"Pass:  {state['total_ok']}")
        dpg.set_value("stat_ng", f"NG:    {state['total_ng']}")
        if total > 0:
            yield_pct = state["total_ok"] / total * 100.0
            dpg.set_value("stat_yield", f"Yield: {yield_pct:.1f}%")
        else:
            dpg.set_value("stat_yield", "Yield: -")

        # -- Step results ---
        for i, step in enumerate(steps):
            name = step.get("name", f"step_{i}")
            if i < len(state["step_results"]):
                status, elapsed_s = state["step_results"][i]
                dpg.set_value(f"step_result_{i}",
                              f"  {name}: {status} ({elapsed_s:.3f}s)")
            else:
                dpg.set_value(f"step_result_{i}", f"  {name}: ---")

        # -- Log ---
        log_text = "\n".join(state["log_lines"])
        dpg.set_value("log_text", log_text)

        # -- Status bar ---
        status_str = "Running" if state["running"] else "Idle"
        mode_str = "(loop)" if state["loop_mode"] and state["running"] else ""
        fps_str = f"{state['fps']:.1f}" if state["fps"] > 0 else "-"
        dpg.set_value(
            "status_bar",
            f"  Status: {status_str} {mode_str} | "
            f"Cycle: {state['cycle']} | "
            f"Time: {state['last_time_ms']:.0f}ms | "
            f"FPS: {fps_str}",
        )

    except Exception:
        pass  # GUI may be shutting down
