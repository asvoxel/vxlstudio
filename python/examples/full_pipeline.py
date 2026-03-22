#!/usr/bin/env python3
"""Full VxlStudio 3D-inspection pipeline example.

This script demonstrates the complete workflow:
  1. Open a simulated 3D camera
  2. Capture a fringe-pattern sequence
  3. Reconstruct a height map
  4. Configure and run 3D inspection
  5. Print the results
"""

import vxl


def main() -> None:
    # -- Initialize logging ---------------------------------------------------
    vxl.log.init()
    vxl.log.add_console_sink()
    vxl.log.set_level(vxl.log.Level.INFO)

    # -- List available cameras -----------------------------------------------
    devices = vxl.Camera.enumerate()
    print(f"Available cameras: {devices}")

    # -- Open a simulated 3D camera -------------------------------------------
    cam = vxl.Camera.open_3d("SIM-001")
    print(f"Opened camera: {cam.device_id()}")

    # Optionally adjust exposure
    cam.set_exposure(5000)
    print(f"Exposure: {cam.exposure()} us")

    # -- Capture fringe-pattern sequence --------------------------------------
    frames = cam.capture_sequence()
    print(f"Captured {len(frames)} fringe images")

    # -- Load calibration (default simulator parameters) ----------------------
    calib = vxl.CalibrationParams.default_sim()
    print(f"Calibration: {calib}")

    # -- Reconstruct height map -----------------------------------------------
    hmap = vxl.reconstruct(frames, calib)
    print(f"Height map: {hmap}")

    # Convert to numpy for quick inspection
    arr = hmap.to_numpy()
    print(f"Height map shape: {arr.shape}, dtype: {arr.dtype}")

    # -- Configure 3D inspector -----------------------------------------------
    inspector = vxl.Inspector3D()

    # Add a height-measurement inspector
    cfg_measure = vxl.InspectorConfig()
    cfg_measure.name = "solder_height"
    cfg_measure.type = "height_measure"
    cfg_measure.rois = [vxl.ROI(100, 100, 200, 200)]
    cfg_measure.params = {"ref_height": 0.0}
    cfg_measure.severity = "critical"
    inspector.add_inspector(cfg_measure)

    # Add a flatness inspector
    cfg_flat = vxl.InspectorConfig()
    cfg_flat.name = "board_flatness"
    cfg_flat.type = "flatness"
    cfg_flat.rois = [vxl.ROI(50, 50, 300, 300)]
    cfg_flat.params = {"max_flatness_mm": 0.1}
    cfg_flat.severity = "warning"
    inspector.add_inspector(cfg_flat)

    # -- Run inspection -------------------------------------------------------
    result = inspector.run(hmap)
    print(f"\nInspection result: {result}")
    print(f"  OK: {result.ok}")
    print(f"  Defects: {len(result.defects)}")
    for i, d in enumerate(result.defects):
        print(f"    [{i}] {d}")
    print(f"  Measures: {len(result.measures)}")
    for i, mr in enumerate(result.measures):
        print(f"    [{i}] {mr}")

    # Serialize to JSON
    json_str = result.to_json()
    print(f"\nJSON output ({len(json_str)} chars):")
    print(json_str[:500])

    # -- Clean up -------------------------------------------------------------
    cam.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
