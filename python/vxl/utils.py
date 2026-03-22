"""VxlStudio utility functions for visualization."""

from __future__ import annotations

from typing import TYPE_CHECKING

import numpy as np

if TYPE_CHECKING:
    from _vxl_core import DefectRegion, HeightMap, Image


def display_height_map(
    hmap: "HeightMap",
    colormap: str = "turbo",
    *,
    vmin: float | None = None,
    vmax: float | None = None,
) -> np.ndarray:
    """Convert a HeightMap to an RGB uint8 image using a matplotlib colormap.

    Parameters
    ----------
    hmap : HeightMap
        The height map to visualize.
    colormap : str
        Name of a matplotlib colormap (default ``'turbo'``).
    vmin, vmax : float or None
        Explicit min/max for the colour scaling.  ``None`` means auto.

    Returns
    -------
    np.ndarray
        An (H, W, 3) uint8 RGB image.
    """
    try:
        from matplotlib import colormaps  # matplotlib >= 3.7
    except ImportError:  # pragma: no cover
        from matplotlib import cm as _cm  # type: ignore[attr-defined]

        class _FallbackColormaps:  # noqa: D106
            def __getitem__(self, name: str):  # noqa: D105
                return _cm.get_cmap(name)

        colormaps = _FallbackColormaps()

    data = np.array(hmap, dtype=np.float32, copy=False)

    # Mask NaN pixels
    valid = np.isfinite(data)
    if vmin is None:
        vmin = float(np.nanmin(data)) if valid.any() else 0.0
    if vmax is None:
        vmax = float(np.nanmax(data)) if valid.any() else 1.0
    if vmax == vmin:
        vmax = vmin + 1.0

    normalized = np.clip((data - vmin) / (vmax - vmin), 0.0, 1.0)
    normalized[~valid] = 0.0

    cmap = colormaps[colormap]
    rgba = cmap(normalized)  # (H, W, 4) float in [0, 1]
    rgb = (rgba[:, :, :3] * 255).astype(np.uint8)

    # Set NaN pixels to black
    rgb[~valid] = 0
    return rgb


def display_defects(
    image: "Image | np.ndarray",
    defects: "list[DefectRegion]",
    *,
    color: tuple[int, int, int] = (255, 0, 0),
    thickness: int = 2,
) -> np.ndarray:
    """Draw defect bounding boxes on an image.

    Parameters
    ----------
    image : Image or np.ndarray
        The base image.  If it is a vxl.Image it will be converted via
        ``to_numpy()``.  Grayscale images are promoted to RGB.
    defects : list[DefectRegion]
        Defect regions whose ``bounding_box`` will be drawn.
    color : tuple[int, int, int]
        RGB colour for the bounding-box rectangles.
    thickness : int
        Line thickness in pixels.

    Returns
    -------
    np.ndarray
        A copy of the image (H, W, 3) uint8 with rectangles drawn.
    """
    if not isinstance(image, np.ndarray):
        image = image.to_numpy()

    out = np.asarray(image, dtype=np.uint8).copy()

    # Promote grayscale to 3-channel
    if out.ndim == 2:
        out = np.stack([out, out, out], axis=-1)

    for defect in defects:
        bb = defect.bounding_box
        x0, y0 = bb.x, bb.y
        x1, y1 = bb.x + bb.w, bb.y + bb.h
        # Clamp to image bounds
        h, w = out.shape[:2]
        x0 = max(0, x0)
        y0 = max(0, y0)
        x1 = min(w, x1)
        y1 = min(h, y1)

        # Draw rectangle edges
        t = thickness
        out[y0 : y0 + t, x0:x1] = color  # top
        out[y1 - t : y1, x0:x1] = color  # bottom
        out[y0:y1, x0 : x0 + t] = color  # left
        out[y0:y1, x1 - t : x1] = color  # right

    return out
