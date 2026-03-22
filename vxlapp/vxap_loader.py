"""VxAP package loader.

A .vxap file is a ZIP archive with the following structure:
    manifest.json       - Package metadata (required)
    pipeline.json       - Pipeline definition (required)
    params.default.json - Default parameters (optional)
    scripts/            - Python scripts (optional)
    models/             - AI model files (optional)
    assets/             - Static assets (optional)
    plugins/            - Plugin extensions (optional)
"""
import json
import os
import shutil
import tempfile
import zipfile


# Required fields in manifest.json
_REQUIRED_MANIFEST_FIELDS = ("name", "version")

# Required files in the package
_REQUIRED_FILES = ("manifest.json", "pipeline.json")


class VxapLoadError(Exception):
    """Raised when a .vxap package cannot be loaded."""


def validate_manifest(manifest: dict) -> list[str]:
    """Validate manifest.json contents.

    Returns a list of error strings (empty if valid).
    """
    errors: list[str] = []
    for field in _REQUIRED_MANIFEST_FIELDS:
        if field not in manifest:
            errors.append(f"Missing required field: {field}")
    if "name" in manifest and not isinstance(manifest["name"], str):
        errors.append("Field 'name' must be a string")
    if "version" in manifest and not isinstance(manifest["version"], str):
        errors.append("Field 'version' must be a string")
    return errors


def load_package(path: str, extract_dir: str | None = None) -> dict:
    """Load a .vxap package and extract it.

    Args:
        path: Path to the .vxap file.
        extract_dir: Directory to extract into. If None, a temporary directory
                     is created (caller is responsible for cleanup).

    Returns:
        dict with keys:
            manifest      - parsed manifest.json dict
            pipeline_path - absolute path to pipeline.json
            scripts_dir   - absolute path to scripts/ (may not exist)
            models_dir    - absolute path to models/ (may not exist)
            assets_dir    - absolute path to assets/ (may not exist)
            extract_dir   - root directory of extracted files
            temp          - True if extract_dir was auto-created (needs cleanup)

    Raises:
        VxapLoadError: If the file is invalid or missing required contents.
    """
    if not os.path.isfile(path):
        raise VxapLoadError(f"Package file not found: {path}")

    if not zipfile.is_zipfile(path):
        raise VxapLoadError(f"Not a valid ZIP/vxap file: {path}")

    temp_created = extract_dir is None
    if temp_created:
        extract_dir = tempfile.mkdtemp(prefix="vxap_")

    try:
        with zipfile.ZipFile(path, "r") as zf:
            # Check for required files
            names = zf.namelist()
            for req in _REQUIRED_FILES:
                if req not in names:
                    raise VxapLoadError(f"Package missing required file: {req}")

            # Extract all
            zf.extractall(extract_dir)

        # Parse manifest
        manifest_path = os.path.join(extract_dir, "manifest.json")
        with open(manifest_path, "r", encoding="utf-8") as f:
            manifest = json.load(f)

        errors = validate_manifest(manifest)
        if errors:
            raise VxapLoadError(
                "Invalid manifest.json:\n  " + "\n  ".join(errors)
            )

        pipeline_path = os.path.join(extract_dir, "pipeline.json")

        return {
            "manifest": manifest,
            "pipeline_path": pipeline_path,
            "scripts_dir": os.path.join(extract_dir, "scripts"),
            "models_dir": os.path.join(extract_dir, "models"),
            "assets_dir": os.path.join(extract_dir, "assets"),
            "extract_dir": extract_dir,
            "temp": temp_created,
        }

    except VxapLoadError:
        if temp_created:
            shutil.rmtree(extract_dir, ignore_errors=True)
        raise
    except Exception as exc:
        if temp_created:
            shutil.rmtree(extract_dir, ignore_errors=True)
        raise VxapLoadError(f"Failed to load package: {exc}") from exc


def cleanup_package(pkg: dict) -> None:
    """Remove temporary extraction directory if it was auto-created."""
    if pkg.get("temp") and pkg.get("extract_dir"):
        shutil.rmtree(pkg["extract_dir"], ignore_errors=True)
