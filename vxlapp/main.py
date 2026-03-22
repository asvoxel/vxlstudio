#!/usr/bin/env python3
"""VxlApp - VxlStudio runtime application.

Loads a .vxap package and executes the pipeline.

Usage:
    python -m vxlapp.main package.vxap [--headless] [--once] [--list-steps]
"""
import argparse
import sys

from vxlapp.vxap_loader import load_package, cleanup_package, VxapLoadError
from vxlapp.headless_runner import run_headless


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="vxlapp",
        description="VxlApp - load and run .vxap packages",
    )
    parser.add_argument(
        "package",
        nargs="?",
        help="Path to .vxap package file",
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="Run without GUI (print results to stdout)",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Run pipeline once then exit",
    )
    parser.add_argument(
        "--list-steps",
        action="store_true",
        help="List pipeline steps and exit",
    )

    args = parser.parse_args()

    if args.package is None:
        parser.print_help()
        return 1

    # Load the .vxap package
    try:
        pkg = load_package(args.package)
    except VxapLoadError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    try:
        if args.list_steps:
            return run_headless(pkg, list_steps=True)
        elif args.headless or args.once:
            return run_headless(pkg, run_once=args.once)
        else:
            # Try GUI mode; falls back to headless if dearpygui unavailable
            from vxlapp.gui_runner import run_gui
            return run_gui(pkg)
    finally:
        cleanup_package(pkg)


if __name__ == "__main__":
    sys.exit(main())
