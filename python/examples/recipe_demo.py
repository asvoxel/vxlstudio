#!/usr/bin/env python3
"""Recipe-based inspection demo.

Loads a recipe from a JSON file, runs the inspection on a height map
produced from a simulated camera, and prints the results.
"""

import sys

import vxl


def main(recipe_path: str) -> None:
    # -- Initialize logging ---------------------------------------------------
    vxl.log.init()
    vxl.log.add_console_sink()

    # -- Load recipe ----------------------------------------------------------
    recipe = vxl.Recipe.load(recipe_path)
    print(f"Loaded recipe: name={recipe.name()}, type={recipe.type()}")

    # Validate the recipe
    recipe.validate()
    print("Recipe validation passed.")

    # Print inspector configs
    configs = recipe.inspector_configs()
    print(f"Inspector configs ({len(configs)}):")
    for cfg in configs:
        print(f"  - {cfg}")

    # -- Acquire a height map (simulated) -------------------------------------
    cam = vxl.Camera.open_3d("SIM-001")
    frames = cam.capture_sequence()
    calib = vxl.CalibrationParams.default_sim()
    hmap = vxl.reconstruct(frames, calib)
    cam.close()
    print(f"\nReconstructed height map: {hmap}")

    # -- Run the recipe -------------------------------------------------------
    result = recipe.inspect(hmap)
    print(f"\nInspection result: {result}")
    print(f"  OK:       {result.ok}")
    print(f"  Defects:  {len(result.defects)}")
    print(f"  Measures: {len(result.measures)}")

    for i, d in enumerate(result.defects):
        print(f"  Defect [{i}]: type={d.type}, area={d.area_mm2:.3f} mm^2, "
              f"max_h={d.max_height:.4f}")

    for i, mr in enumerate(result.measures):
        print(f"  Measure [{i}]: min={mr.min_height:.4f}, "
              f"max={mr.max_height:.4f}, avg={mr.avg_height:.4f}")

    # -- Optionally save the recipe back --------------------------------------
    # recipe.save("output_recipe.json")

    print("\nDone.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <recipe.json>")
        sys.exit(1)
    main(sys.argv[1])
