"""ImageViewer -- 2D image display with zoom, pan, and overlay drawing."""

from PySide6.QtCore import Qt, QRectF, QPointF
from PySide6.QtGui import QImage, QPixmap, QPainter, QPen, QColor, QWheelEvent
from PySide6.QtWidgets import QWidget

import numpy as np


class _OverlayRect:
    __slots__ = ("x", "y", "w", "h", "color")

    def __init__(self, x, y, w, h, color):
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        self.color = color


class ImageViewer(QWidget):
    """Displays a numpy array (grayscale or RGB) with zoom, pan, and overlays."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._pixmap = None
        self._overlays = []       # list[_OverlayRect]
        self._annotations = []    # list[(x, y, text, color)]

        # View transform
        self._zoom = 1.0
        self._offset = QPointF(0, 0)

        # Pan state
        self._panning = False
        self._pan_start = QPointF()

        self.setMinimumSize(200, 200)
        self.setFocusPolicy(Qt.StrongFocus)

    # -- Public API --------------------------------------------------------

    def set_image(self, arr: np.ndarray):
        """Set the displayed image from a numpy array (H, W) or (H, W, 3)."""
        if arr is None:
            self._pixmap = None
            self.update()
            return

        # Ensure contiguous uint8 data for QImage
        if arr.dtype != np.uint8:
            # For float data, normalize to 0-255
            if np.issubdtype(arr.dtype, np.floating):
                valid = np.isfinite(arr)
                if np.any(valid):
                    vmin = float(np.nanmin(arr))
                    vmax = float(np.nanmax(arr))
                    if vmax - vmin < 1e-9:
                        vmax = vmin + 1.0
                    arr = np.clip((arr - vmin) / (vmax - vmin) * 255, 0, 255).astype(np.uint8)
                else:
                    arr = np.zeros(arr.shape[:2], dtype=np.uint8)
            elif arr.dtype == np.uint16:
                arr = (arr >> 8).astype(np.uint8)
            else:
                arr = arr.astype(np.uint8)

        arr = np.ascontiguousarray(arr)

        if arr.ndim == 2:
            h, w = arr.shape
            bytes_per_line = w
            qimg = QImage(arr.data, w, h, bytes_per_line, QImage.Format_Grayscale8)
        elif arr.ndim == 3 and arr.shape[2] == 3:
            h, w, _ = arr.shape
            bytes_per_line = 3 * w
            qimg = QImage(arr.data, w, h, bytes_per_line, QImage.Format_RGB888)
        else:
            return

        self._pixmap = QPixmap.fromImage(qimg.copy())
        self.update()

    def add_overlay_rect(self, x, y, w, h, color="red"):
        self._overlays.append(_OverlayRect(x, y, w, h, color))

    def add_annotation(self, x, y, text, color="yellow"):
        self._annotations.append((x, y, text, color))

    def clear_overlays(self):
        self._overlays.clear()
        self._annotations.clear()

    def reset_view(self):
        self._zoom = 1.0
        self._offset = QPointF(0, 0)
        self.update()

    # -- Paint -------------------------------------------------------------

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), QColor("#1e1e1e"))

        if self._pixmap is None:
            painter.setPen(QColor("#888888"))
            painter.drawText(self.rect(), Qt.AlignCenter, "No image")
            painter.end()
            return

        painter.translate(self._offset)
        painter.scale(self._zoom, self._zoom)

        # Draw image
        painter.drawPixmap(0, 0, self._pixmap)

        # Draw overlay rectangles
        for ov in self._overlays:
            pen = QPen(QColor(ov.color))
            pen.setWidth(max(1, int(2 / self._zoom)))
            painter.setPen(pen)
            painter.setBrush(Qt.NoBrush)
            painter.drawRect(QRectF(ov.x, ov.y, ov.w, ov.h))

        # Draw annotations
        for ax, ay, text, color in self._annotations:
            painter.setPen(QColor(color))
            painter.drawText(QPointF(ax, ay), text)

        painter.end()

    # -- Zoom (scroll wheel) ----------------------------------------------

    def wheelEvent(self, event: QWheelEvent):
        delta = event.angleDelta().y()
        factor = 1.15 if delta > 0 else 1.0 / 1.15
        self._zoom *= factor
        self._zoom = max(0.05, min(self._zoom, 50.0))
        self.update()

    # -- Pan (middle mouse drag) -------------------------------------------

    def mousePressEvent(self, event):
        if event.button() == Qt.MiddleButton:
            self._panning = True
            self._pan_start = event.position()
            self.setCursor(Qt.ClosedHandCursor)

    def mouseMoveEvent(self, event):
        if self._panning:
            delta = event.position() - self._pan_start
            self._offset += delta
            self._pan_start = event.position()
            self.update()

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MiddleButton:
            self._panning = False
            self.setCursor(Qt.ArrowCursor)
