"""HeightMapViewer -- displays a HeightMap as a color-mapped image."""

from PySide6.QtCore import Qt, QPointF
from PySide6.QtGui import QPainter, QColor, QPen
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel

import numpy as np

from demo.widgets.image_viewer import ImageViewer


def _apply_turbo_colormap(data: np.ndarray) -> np.ndarray:
    """Map a float32 array to RGB using a turbo-like colormap.

    Tries matplotlib first; falls back to a simple blue-green-red ramp.
    """
    # Normalize to [0, 1]
    valid = np.isfinite(data)
    if not np.any(valid):
        return np.zeros((*data.shape, 3), dtype=np.uint8), 0.0, 0.0

    vmin = float(np.nanmin(data))
    vmax = float(np.nanmax(data))
    if vmax - vmin < 1e-9:
        vmax = vmin + 1.0

    norm = np.clip((data - vmin) / (vmax - vmin), 0.0, 1.0)

    try:
        from matplotlib import cm
        rgba = cm.turbo(norm)  # (H, W, 4) float64 in [0,1]
        rgb = (rgba[:, :, :3] * 255).astype(np.uint8)
    except ImportError:
        # Fallback: simple blue -> green -> red ramp
        rgb = np.zeros((*data.shape, 3), dtype=np.uint8)
        rgb[:, :, 0] = np.clip(norm * 2 - 1, 0, 1) * 255         # red
        rgb[:, :, 1] = np.clip(1 - np.abs(norm * 2 - 1), 0, 1) * 255  # green
        rgb[:, :, 2] = np.clip(1 - norm * 2, 0, 1) * 255          # blue

    # Mark NaN pixels as black
    rgb[~valid] = 0
    return rgb, vmin, vmax


class _ColorBar(QWidget):
    """Vertical color bar showing the height range."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._vmin = 0.0
        self._vmax = 1.0
        self.setFixedWidth(60)

    def set_range(self, vmin, vmax):
        self._vmin = vmin
        self._vmax = vmax
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        h = self.height()
        w = self.width()
        bar_w = 20

        # Draw gradient bar
        for y in range(h):
            t = 1.0 - y / max(h - 1, 1)
            r = int(max(0, min(1, t * 2 - 1)) * 255)
            g = int(max(0, min(1, 1 - abs(t * 2 - 1))) * 255)
            b = int(max(0, min(1, 1 - t * 2)) * 255)
            painter.setPen(QColor(r, g, b))
            painter.drawLine(0, y, bar_w, y)

        # Labels
        painter.setPen(QColor("#e0e0e0"))
        painter.drawText(bar_w + 4, 14, f"{self._vmax:.3f}")
        mid = (self._vmin + self._vmax) / 2
        painter.drawText(bar_w + 4, h // 2 + 5, f"{mid:.3f}")
        painter.drawText(bar_w + 4, h - 4, f"{self._vmin:.3f}")
        painter.end()


class _ProfilePlot(QWidget):
    """Small widget showing a 1D height profile between two clicked points."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._profile = None
        self.setFixedHeight(120)
        self.setMinimumWidth(200)

    def set_profile(self, values: np.ndarray):
        self._profile = values
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.fillRect(self.rect(), QColor("#1e1e1e"))
        if self._profile is None or len(self._profile) < 2:
            painter.setPen(QColor("#888"))
            painter.drawText(self.rect(), Qt.AlignCenter, "Click two points for profile")
            painter.end()
            return

        valid = np.isfinite(self._profile)
        if not np.any(valid):
            painter.end()
            return

        vmin = float(np.nanmin(self._profile))
        vmax = float(np.nanmax(self._profile))
        if vmax - vmin < 1e-9:
            vmax = vmin + 1.0

        w = self.width() - 8
        h = self.height() - 16
        n = len(self._profile)

        painter.setPen(QPen(QColor("#2ecc71"), 1))
        prev = None
        for i in range(n):
            if not np.isfinite(self._profile[i]):
                prev = None
                continue
            x = 4 + int(i / (n - 1) * w)
            y = 8 + h - int((self._profile[i] - vmin) / (vmax - vmin) * h)
            pt = QPointF(x, y)
            if prev is not None:
                painter.drawLine(prev, pt)
            prev = pt

        # Axis labels
        painter.setPen(QColor("#aaa"))
        painter.drawText(4, h + 14, f"{vmin:.3f}")
        painter.drawText(w - 40, h + 14, f"{vmax:.3f}")
        painter.end()


class HeightMapViewer(QWidget):
    """Displays a HeightMap as a color-mapped image with colorbar and profile tool."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._height_data = None  # numpy float32 (H, W)
        self._click_points = []   # up to 2 points for cross-section

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        top = QHBoxLayout()
        self._image_viewer = ImageViewer()
        top.addWidget(self._image_viewer, stretch=1)
        self._colorbar = _ColorBar()
        top.addWidget(self._colorbar)
        layout.addLayout(top, stretch=1)

        self._profile_plot = _ProfilePlot()
        layout.addWidget(self._profile_plot)

        # Override image viewer mouse press for cross-section tool
        self._image_viewer.mousePressEvent = self._on_image_click

    # -- Public API --------------------------------------------------------

    def set_height_map(self, hmap):
        """Accept a vxl.HeightMap or a numpy float32 array."""
        if hmap is None:
            self._height_data = None
            self._image_viewer.set_image(None)
            return

        # Extract numpy data from vxl.HeightMap
        try:
            # Use buffer protocol (HeightMap supports it) or to_numpy()
            if hasattr(hmap, 'to_numpy'):
                data = hmap.to_numpy()
            else:
                data = np.asarray(hmap, dtype=np.float32)
        except (AttributeError, TypeError):
            # Assume it's already a numpy array
            data = np.asarray(hmap, dtype=np.float32)

        self._height_data = data.copy()
        result = _apply_turbo_colormap(self._height_data)
        rgb, vmin, vmax = result
        self._image_viewer.set_image(rgb)
        self._colorbar.set_range(vmin, vmax)
        self._click_points.clear()

    # -- Cross-section tool ------------------------------------------------

    def _on_image_click(self, event):
        if event.button() == Qt.LeftButton and self._height_data is not None:
            # Map widget coords to image coords (approximate)
            viewer = self._image_viewer
            pos = event.position()
            ix = int((pos.x() - viewer._offset.x()) / viewer._zoom)
            iy = int((pos.y() - viewer._offset.y()) / viewer._zoom)

            h, w = self._height_data.shape
            ix = max(0, min(ix, w - 1))
            iy = max(0, min(iy, h - 1))

            self._click_points.append((ix, iy))
            if len(self._click_points) >= 2:
                p0 = self._click_points[-2]
                p1 = self._click_points[-1]
                self._extract_profile(p0, p1)
                self._click_points.clear()
        elif event.button() == Qt.MiddleButton:
            # Delegate to image viewer panning
            ImageViewer.mousePressEvent(self._image_viewer, event)

    def _extract_profile(self, p0, p1):
        """Extract height values along a line from p0 to p1."""
        x0, y0 = p0
        x1, y1 = p1
        length = int(np.hypot(x1 - x0, y1 - y0))
        if length < 2:
            return
        xs = np.linspace(x0, x1, length).astype(int)
        ys = np.linspace(y0, y1, length).astype(int)
        h, w = self._height_data.shape
        xs = np.clip(xs, 0, w - 1)
        ys = np.clip(ys, 0, h - 1)
        profile = self._height_data[ys, xs]
        self._profile_plot.set_profile(profile)

        # Draw line overlay
        self._image_viewer.clear_overlays()
        self._image_viewer.add_annotation(x0, y0, "A", "cyan")
        self._image_viewer.add_annotation(x1, y1, "B", "cyan")
        self._image_viewer.update()
