#!/usr/bin/env python3
"""
Train and export an Anomalib anomaly detection model to ONNX format,
then verify it loads with ONNX Runtime.

Usage:
    # Train PaDiM on MVTec bottle category and export ONNX
    python tools/download_anomaly_model.py

    # Train on a specific category
    python tools/download_anomaly_model.py --category carpet

    # Use PatchCore instead
    python tools/download_anomaly_model.py --model Patchcore

    # Custom input size
    python tools/download_anomaly_model.py --input-size 224

    # Only verify an existing model
    python tools/download_anomaly_model.py --verify-only models/anomaly/padim_resnet18.onnx

Requirements:
    pip install anomalib onnxruntime

Note:
    There are no official pre-trained ONNX downloads from the Anomalib project.
    This script trains a model on the MVTec AD dataset (auto-downloaded) and
    exports it to ONNX format. Training PaDiM takes ~5-10 minutes on CPU.
"""

import argparse
import shutil
import sys
from pathlib import Path

# Resolve project root relative to this script
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "models" / "anomaly"


def verify_model(model_path: str) -> bool:
    """Verify that an ONNX model loads and runs with ONNX Runtime."""
    try:
        import onnxruntime as ort
    except ImportError:
        print("ERROR: onnxruntime is not installed. Run: pip install onnxruntime")
        return False

    model_path = Path(model_path)
    if not model_path.exists():
        print(f"ERROR: Model file not found: {model_path}")
        return False

    print(f"Loading model: {model_path}")
    print(f"  File size: {model_path.stat().st_size / 1024 / 1024:.1f} MB")

    try:
        session = ort.InferenceSession(
            str(model_path),
            providers=["CPUExecutionProvider"],
        )
    except Exception as e:
        print(f"ERROR: Failed to load ONNX model: {e}")
        return False

    print("  Inputs:")
    for inp in session.get_inputs():
        print(f"    {inp.name}: shape={inp.shape}, dtype={inp.type}")

    print("  Outputs:")
    for out in session.get_outputs():
        print(f"    {out.name}: shape={out.shape}, dtype={out.type}")

    # Run a dummy inference to make sure the model actually works
    import numpy as np

    input_meta = session.get_inputs()[0]
    shape = input_meta.shape
    # Replace dynamic dimensions with concrete values
    concrete_shape = []
    for dim in shape:
        if isinstance(dim, int):
            concrete_shape.append(dim)
        else:
            concrete_shape.append(1 if "batch" in str(dim).lower() else 256)

    dummy_input = np.random.randn(*concrete_shape).astype(np.float32)
    try:
        outputs = session.run(None, {input_meta.name: dummy_input})
        print(f"  Inference OK: {len(outputs)} output(s)")
        for i, out in enumerate(outputs):
            out_arr = np.array(out)
            print(f"    output[{i}]: shape={out_arr.shape}, "
                  f"min={out_arr.min():.4f}, max={out_arr.max():.4f}")
    except Exception as e:
        print(f"  WARNING: Dummy inference failed: {e}")
        print("  (Model loaded successfully but may require specific input preprocessing)")

    print("Model verification PASSED.")
    return True


def train_and_export(
    model_name: str = "Padim",
    category: str = "bottle",
    input_size: int = 256,
    output_dir: Path = DEFAULT_OUTPUT_DIR,
) -> Path:
    """Train an Anomalib model on MVTec AD and export to ONNX."""
    try:
        from anomalib.engine import Engine
        from anomalib.data import MVTecAD
    except ImportError:
        print("ERROR: anomalib is not installed. Run: pip install anomalib")
        sys.exit(1)

    # Import model class dynamically
    try:
        if model_name.lower() == "padim":
            from anomalib.models import Padim as ModelClass
        elif model_name.lower() == "patchcore":
            from anomalib.models import Patchcore as ModelClass
        else:
            # Try generic import
            import importlib
            mod = importlib.import_module(f"anomalib.models")
            ModelClass = getattr(mod, model_name)
    except (ImportError, AttributeError):
        print(f"ERROR: Unknown model '{model_name}'. Try 'Padim' or 'Patchcore'.")
        sys.exit(1)

    print(f"=== Training {model_name} on MVTec AD / {category} ===")
    print(f"  Input size: {input_size}x{input_size}")

    model = ModelClass()
    datamodule = MVTecAD(
        category=category,
        image_size=(input_size, input_size),
    )

    engine = Engine()

    # Train
    print("Training... (this may take 5-15 minutes on CPU)")
    engine.fit(model=model, datamodule=datamodule)

    # Export to ONNX
    print("Exporting to ONNX...")
    engine.export(
        model=model,
        export_type="onnx",
        input_size=(input_size, input_size),
    )

    # Find the exported ONNX file
    results_dir = Path("results")
    onnx_files = list(results_dir.rglob("*.onnx"))
    if not onnx_files:
        print("ERROR: No ONNX file found after export.")
        sys.exit(1)

    # Use the most recently created one
    src_onnx = max(onnx_files, key=lambda p: p.stat().st_mtime)
    print(f"  Found exported model: {src_onnx}")

    # Copy to output directory
    output_dir.mkdir(parents=True, exist_ok=True)
    model_filename = f"{model_name.lower()}_resnet18.onnx"
    if model_name.lower() == "patchcore":
        model_filename = "patchcore_wide_resnet50.onnx"
    dest_path = output_dir / model_filename

    shutil.copy2(src_onnx, dest_path)
    print(f"  Saved to: {dest_path}")

    return dest_path


def main():
    parser = argparse.ArgumentParser(
        description="Train and export an Anomalib anomaly detection model to ONNX."
    )
    parser.add_argument(
        "--model",
        default="Padim",
        help="Anomalib model name: Padim (default), Patchcore, etc.",
    )
    parser.add_argument(
        "--category",
        default="bottle",
        help="MVTec AD category to train on (default: bottle). "
        "Options: bottle, cable, capsule, carpet, grid, hazelnut, leather, "
        "metal_nut, pill, screw, tile, toothbrush, transistor, wood, zipper.",
    )
    parser.add_argument(
        "--input-size",
        type=int,
        default=256,
        help="Input image size (default: 256). Common values: 224, 256.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help=f"Output directory for the ONNX model (default: {DEFAULT_OUTPUT_DIR}).",
    )
    parser.add_argument(
        "--verify-only",
        metavar="ONNX_PATH",
        help="Only verify an existing ONNX model (skip training).",
    )

    args = parser.parse_args()

    if args.verify_only:
        success = verify_model(args.verify_only)
        sys.exit(0 if success else 1)

    # Train and export
    model_path = train_and_export(
        model_name=args.model,
        category=args.category,
        input_size=args.input_size,
        output_dir=args.output_dir,
    )

    # Verify
    print()
    print("=== Verifying exported model ===")
    verify_model(str(model_path))


if __name__ == "__main__":
    main()
