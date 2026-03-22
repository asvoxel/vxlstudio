"""RunView -- production / operator mode layout."""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QSplitter,
)

from demo.app import DemoApp
from demo.widgets.image_viewer import ImageViewer
from demo.widgets.stats_panel import StatsPanel
from demo.widgets.log_panel import LogPanel
from demo.workers.capture_worker import CaptureWorker
from demo.workers.process_worker import ProcessWorker


class RunView(QWidget):
    """Run mode: large OK/NG indicator, small live preview, stats, log."""

    def __init__(self, app: DemoApp, parent=None):
        super().__init__(parent)
        self._app = app
        self._capture_worker = None
        self._process_worker = None
        self._build_ui()
        self._connect_signals()

    # -- UI ----------------------------------------------------------------

    def _build_ui(self):
        root = QHBoxLayout(self)

        # Left: OK/NG indicator + start/stop
        left = QVBoxLayout()
        self._indicator = QLabel("READY")
        self._indicator.setAlignment(Qt.AlignCenter)
        self._indicator.setStyleSheet(
            "font-size: 72px; font-weight: bold; color: #e0e0e0; "
            "background-color: #3a3a3a; border-radius: 16px; min-height: 300px;")
        left.addWidget(self._indicator, stretch=3)

        btn_layout = QHBoxLayout()
        self._btn_start = QPushButton("Start")
        self._btn_stop = QPushButton("Stop")
        self._btn_stop.setEnabled(False)
        btn_layout.addWidget(self._btn_start)
        btn_layout.addWidget(self._btn_stop)
        left.addLayout(btn_layout)

        root.addLayout(left, stretch=3)

        # Right: small live preview + stats + log
        right_splitter = QSplitter(Qt.Vertical)

        self._preview = ImageViewer()
        self._preview.setMaximumHeight(300)
        right_splitter.addWidget(self._preview)

        self._stats_panel = StatsPanel(self._app)
        right_splitter.addWidget(self._stats_panel)

        self._log_panel = LogPanel(self._app)
        self._log_panel.setMaximumHeight(200)
        right_splitter.addWidget(self._log_panel)

        root.addWidget(right_splitter, stretch=1)

    # -- Signals -----------------------------------------------------------

    def _connect_signals(self):
        self._btn_start.clicked.connect(self.start)
        self._btn_stop.clicked.connect(self.stop)

    # -- Run control -------------------------------------------------------

    def start(self):
        cam = self._app.camera
        if cam is None:
            return
        self._app.ensure_default_recipe()
        self._btn_start.setEnabled(False)
        self._btn_stop.setEnabled(True)
        self._indicator.setText("RUNNING")
        self._indicator.setStyleSheet(
            "font-size: 72px; font-weight: bold; color: #e0e0e0; "
            "background-color: #3a3a3a; border-radius: 16px; min-height: 300px;")

        self._capture_worker = CaptureWorker(cam)
        self._capture_worker.frame_captured.connect(self._on_frames)
        self._capture_worker.start_continuous()

    def stop(self):
        if self._capture_worker is not None:
            self._capture_worker.stop()
            self._capture_worker = None
        self._btn_start.setEnabled(True)
        self._btn_stop.setEnabled(False)
        self._indicator.setText("STOPPED")
        self._indicator.setStyleSheet(
            "font-size: 72px; font-weight: bold; color: #e0e0e0; "
            "background-color: #3a3a3a; border-radius: 16px; min-height: 300px;")

    def _on_frames(self, frames):
        # Show first frame in small preview
        if frames:
            try:
                import numpy as np
                img = frames[0]
                arr = np.array(img, copy=False)
                self._preview.set_image(arr)
            except Exception:
                pass

        # Process
        self._process_worker = ProcessWorker(frames, self._app.recipe)
        self._process_worker.inspection_done.connect(self._on_result)
        self._process_worker.error_occurred.connect(self._on_error)
        self._process_worker.start()

    def _on_error(self, msg):
        self._app.log_message.emit(4, f"Run mode error: {msg}")

    def _on_result(self, result):
        self._app.record_result(result.ok)
        if result.ok:
            self._indicator.setText("OK")
            self._indicator.setStyleSheet(
                "font-size: 120px; font-weight: bold; color: white; "
                "background-color: #27ae60; border-radius: 16px; min-height: 300px;")
        else:
            self._indicator.setText("NG")
            self._indicator.setStyleSheet(
                "font-size: 120px; font-weight: bold; color: white; "
                "background-color: #e74c3c; border-radius: 16px; min-height: 300px;")
