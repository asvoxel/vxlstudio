#!/usr/bin/env python3
"""Download and export YOLO models to ONNX for VxlStudio.

Usage examples:
    # Export YOLOv11n detection model (default)
    python tools/download_yolo_model.py

    # Export classification model
    python tools/download_yolo_model.py --task classify --model yolo11n

    # Export segmentation model with custom input size
    python tools/download_yolo_model.py --task segment --model yolo11n --input-size 640

    # Export to a custom directory
    python tools/download_yolo_model.py --output-dir /path/to/models

Requirements:
    pip install ultralytics onnxruntime
"""

import argparse
import os
import shutil
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Download and export YOLO models to ONNX for VxlStudio."
    )
    parser.add_argument(
        "--task",
        choices=["detect", "classify", "segment"],
        default="detect",
        help="Model task type (default: detect)",
    )
    parser.add_argument(
        "--model",
        default="yolo11n",
        help="Base model name, e.g. yolo11n, yolo11s, yolo11m (default: yolo11n)",
    )
    parser.add_argument(
        "--output-dir",
        default="models/yolo",
        help="Output directory for ONNX files (default: models/yolo)",
    )
    parser.add_argument(
        "--input-size",
        type=int,
        default=None,
        help="Input image size (default: 640 for detect/segment, 224 for classify)",
    )
    args = parser.parse_args()

    # Determine default input size per task
    if args.input_size is None:
        args.input_size = 224 if args.task == "classify" else 640

    # Determine model filename
    task_suffix = {"detect": "", "classify": "-cls", "segment": "-seg"}
    model_name = f"{args.model}{task_suffix[args.task]}.pt"

    print(f"Task:       {args.task}")
    print(f"Model:      {model_name}")
    print(f"Input size: {args.input_size}")
    print(f"Output dir: {args.output_dir}")
    print()

    try:
        from ultralytics import YOLO
    except ImportError:
        print("ERROR: ultralytics not installed. Run: pip install ultralytics", file=sys.stderr)
        sys.exit(1)

    # Load and export
    print(f"Loading {model_name}...")
    model = YOLO(model_name)

    print(f"Exporting to ONNX (imgsz={args.input_size})...")
    onnx_path = model.export(format="onnx", imgsz=args.input_size)

    if not onnx_path or not os.path.exists(onnx_path):
        print("ERROR: Export failed -- no ONNX file produced.", file=sys.stderr)
        sys.exit(1)

    # Move to output directory
    os.makedirs(args.output_dir, exist_ok=True)
    dest_name = os.path.basename(onnx_path)
    dest_path = os.path.join(args.output_dir, dest_name)
    shutil.move(onnx_path, dest_path)
    print(f"Exported to {dest_path}")

    # Verify with onnxruntime
    try:
        import onnxruntime

        sess = onnxruntime.InferenceSession(dest_path)
        inputs = sess.get_inputs()
        outputs = sess.get_outputs()
        print()
        print("Verification with onnxruntime:")
        for inp in inputs:
            print(f"  Input:  name={inp.name}, shape={inp.shape}, type={inp.type}")
        for out in outputs:
            print(f"  Output: name={out.name}, shape={out.shape}, type={out.type}")
        print()
        print("Model is ready for use with VxlStudio.")
    except ImportError:
        print(
            "WARNING: onnxruntime not installed -- skipping verification.",
            file=sys.stderr,
        )
        print("  pip install onnxruntime")
    except Exception as e:
        print(f"WARNING: Verification failed: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
