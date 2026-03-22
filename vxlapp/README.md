# VxlApp

VxlStudio runtime application. Loads `.vxap` packages and executes inspection pipelines.

## Quick Start

```bash
# Run a .vxap package (GUI mode, falls back to headless if dearpygui not installed)
python -m vxlapp.main package.vxap

# Run headless (no GUI)
python -m vxlapp.main package.vxap --headless

# Run once and exit (headless)
python -m vxlapp.main package.vxap --once

# List pipeline steps without running
python -m vxlapp.main package.vxap --list-steps
```

## Command Line Options

| Option | Description |
|--------|-------------|
| `package` | Path to `.vxap` package file (required) |
| `--headless` | Run without GUI, print results to stdout |
| `--once` | Run pipeline once then exit |
| `--list-steps` | List pipeline steps and exit (no execution) |

## .vxap Package Format

A `.vxap` file is a ZIP archive with the following structure:

```
my_project.vxap
├── manifest.json         # Package metadata (required)
├── pipeline.json         # Pipeline definition (required)
├── params.default.json   # Default parameters (optional)
├── scripts/              # Python scripts (optional)
│   ├── ui_main.py
│   ├── precheck.py
│   └── postprocess.py
├── assets/               # Static assets (optional)
├── models/               # AI model files (optional)
│   └── defect_xx.onnx
└── plugins/              # Plugin extensions (optional)
```

### manifest.json

Required fields:
- `name` (string) - Package name
- `version` (string) - Semantic version

Optional fields:
- `description` - Human-readable description
- `author` - Author name
- `min_vxl_version` - Minimum vxl library version required
- `created` - ISO 8601 timestamp

### pipeline.json

Defines the inspection pipeline as a sequence of steps:

```json
{
  "version": "1.0",
  "camera": "SIM-001",
  "recipe": "pcb_smt/pcb_model_a.json",
  "steps": [
    {"type": "capture",      "name": "grab",     "params": {"camera": "SIM-001"}},
    {"type": "reconstruct",  "name": "build_3d", "params": {"method": "structured_light"}},
    {"type": "inspect_3d",   "name": "check_3d", "params": {"recipe": "pcb_model_a.json"}},
    {"type": "output",       "name": "result",   "params": {"save_ng": true}}
  ]
}
```

Step types: `capture`, `reconstruct`, `inspect_3d`, `inspect_2d`, `output`.

## Creating Packages

Use `vxap_pack.py` to create `.vxap` packages from a project directory:

```bash
python tools/vxap_pack.py project_dir/ output.vxap

# Verbose mode (list files being added)
python tools/vxap_pack.py project_dir/ output.vxap -v
```

The tool validates the project structure before packing.

## Runtime Modes

- **GUI mode** (default): Uses Dear PyGui for a minimal UI showing OK/NG status, step results, and cycle statistics. Requires `pip install dearpygui`.
- **Headless mode** (`--headless`): Prints results to stdout. Suitable for production deployment on headless Linux systems.
- **Dry-run mode**: If the `vxl` native module is not available, the pipeline runs in simulation mode for testing.

## Examples

See `vxlapp/examples/` for sample projects:

- `pcb_demo/` - PCB solder joint inspection using SimCamera

Build and run the demo:

```bash
python tools/vxap_pack.py vxlapp/examples/pcb_demo/ vxlapp/examples/pcb_demo.vxap
python -m vxlapp.main vxlapp/examples/pcb_demo.vxap --once
```
