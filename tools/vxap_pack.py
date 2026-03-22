#!/usr/bin/env python3
"""Package a project directory into a .vxap archive.

Usage:
    python tools/vxap_pack.py project_dir/ output.vxap

The project directory must contain at least:
    manifest.json   - Package metadata
    pipeline.json   - Pipeline definition

Optional contents:
    params.default.json
    scripts/
    models/
    assets/
    plugins/
"""
import argparse
import json
import os
import sys
import zipfile


REQUIRED_FILES = ("manifest.json", "pipeline.json")
REQUIRED_MANIFEST_FIELDS = ("name", "version")

# Directories to include (if they exist)
OPTIONAL_DIRS = ("scripts", "models", "assets", "plugins")

# Additional files to include (if they exist)
OPTIONAL_FILES = ("params.default.json",)


def validate_project(project_dir: str) -> list[str]:
    """Validate project directory structure.

    Returns list of error strings (empty if valid).
    """
    errors: list[str] = []

    for filename in REQUIRED_FILES:
        path = os.path.join(project_dir, filename)
        if not os.path.isfile(path):
            errors.append(f"Missing required file: {filename}")

    # Validate manifest content
    manifest_path = os.path.join(project_dir, "manifest.json")
    if os.path.isfile(manifest_path):
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                manifest = json.load(f)
            for field in REQUIRED_MANIFEST_FIELDS:
                if field not in manifest:
                    errors.append(f"manifest.json missing required field: {field}")
        except json.JSONDecodeError as exc:
            errors.append(f"manifest.json is not valid JSON: {exc}")

    # Validate pipeline content
    pipeline_path = os.path.join(project_dir, "pipeline.json")
    if os.path.isfile(pipeline_path):
        try:
            with open(pipeline_path, "r", encoding="utf-8") as f:
                pipeline = json.load(f)
            if "steps" not in pipeline:
                errors.append("pipeline.json missing 'steps' array")
        except json.JSONDecodeError as exc:
            errors.append(f"pipeline.json is not valid JSON: {exc}")

    return errors


def pack_vxap(project_dir: str, output_path: str, *, verbose: bool = False) -> None:
    """Pack a project directory into a .vxap file.

    Args:
        project_dir: Path to the project directory.
        output_path: Path for the output .vxap file.
        verbose: Print files being added.

    Raises:
        SystemExit: If validation fails.
    """
    project_dir = os.path.abspath(project_dir)
    output_path = os.path.abspath(output_path)

    if not os.path.isdir(project_dir):
        print(f"ERROR: Not a directory: {project_dir}", file=sys.stderr)
        sys.exit(1)

    # Validate
    errors = validate_project(project_dir)
    if errors:
        print("Validation errors:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        sys.exit(1)

    # Build file list
    files_to_add: list[tuple[str, str]] = []  # (absolute_path, archive_name)

    for filename in REQUIRED_FILES + OPTIONAL_FILES:
        path = os.path.join(project_dir, filename)
        if os.path.isfile(path):
            files_to_add.append((path, filename))

    for dirname in OPTIONAL_DIRS:
        dirpath = os.path.join(project_dir, dirname)
        if os.path.isdir(dirpath):
            for root, _dirs, filenames in os.walk(dirpath):
                for fn in filenames:
                    abs_path = os.path.join(root, fn)
                    arc_name = os.path.relpath(abs_path, project_dir)
                    files_to_add.append((abs_path, arc_name))

    # Create .vxap
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for abs_path, arc_name in files_to_add:
            if verbose:
                print(f"  + {arc_name}")
            zf.write(abs_path, arc_name)

    # Print summary
    with open(os.path.join(project_dir, "manifest.json"), "r", encoding="utf-8") as f:
        manifest = json.load(f)

    size_kb = os.path.getsize(output_path) / 1024
    print(f"Packed: {manifest.get('name')} v{manifest.get('version')}")
    print(f"  Files : {len(files_to_add)}")
    print(f"  Size  : {size_kb:.1f} KB")
    print(f"  Output: {output_path}")


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="vxap_pack",
        description="Package a project directory into a .vxap archive",
    )
    parser.add_argument(
        "project_dir",
        help="Path to the project directory",
    )
    parser.add_argument(
        "output",
        help="Output .vxap file path",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Print files being added",
    )
    args = parser.parse_args()

    pack_vxap(args.project_dir, args.output, verbose=args.verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
