"""EngineeringView -- engineering / debugging mode layout."""

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QTabWidget, QDockWidget, QMainWindow,
)

from demo.app import DemoApp
from demo.widgets.camera_panel import CameraPanel
from demo.widgets.param_panel import ParamPanel
from demo.widgets.image_viewer import ImageViewer
from demo.widgets.height_map_viewer import HeightMapViewer
from demo.widgets.roi_editor import ROIEditor
from demo.widgets.result_table import ResultTable
from demo.widgets.log_panel import LogPanel
from demo.workers.capture_worker import CaptureWorker
from demo.workers.process_worker import ProcessWorker


class EngineeringView(QWidget):
    """Engineering mode: full debug layout with docks around a central viewer."""

    def __init__(self, app: DemoApp, main_window: QMainWindow, parent=None):
        super().__init__(parent)
        self._app = app
        self._main_window = main_window

        # Workers
        self._capture_worker = None
        self._process_worker = None

        self._build_ui()
        self._connect_signals()

    # -- UI construction ---------------------------------------------------

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        # Central tabbed viewer
        self._tabs = QTabWidget()
        self._image_viewer = ImageViewer()
        self._height_map_viewer = HeightMapViewer()
        self._tabs.addTab(self._image_viewer, "2D Image")
        self._tabs.addTab(self._height_map_viewer, "Height Map")
        layout.addWidget(self._tabs)

        # Left dock: Camera + Params
        left_dock = QDockWidget("Camera / Parameters", self._main_window)
        left_dock.setObjectName("dock_left")
        left_widget = QWidget()
        left_layout = QVBoxLayout(left_widget)
        self._camera_panel = CameraPanel(self._app)
        self._param_panel = ParamPanel(self._app)
        left_layout.addWidget(self._camera_panel)
        left_layout.addWidget(self._param_panel)
        left_dock.setWidget(left_widget)
        self._main_window.addDockWidget(Qt.LeftDockWidgetArea, left_dock)

        # Right dock: ROI editor + Result table
        right_dock = QDockWidget("ROI / Results", self._main_window)
        right_dock.setObjectName("dock_right")
        right_widget = QWidget()
        right_layout = QVBoxLayout(right_widget)
        self._roi_editor = ROIEditor()
        self._result_table = ResultTable()
        right_layout.addWidget(self._roi_editor)
        right_layout.addWidget(self._result_table)
        right_dock.setWidget(right_widget)
        self._main_window.addDockWidget(Qt.RightDockWidgetArea, right_dock)

        # Bottom dock: Log panel
        bottom_dock = QDockWidget("Log", self._main_window)
        bottom_dock.setObjectName("dock_bottom")
        self._log_panel = LogPanel(self._app)
        bottom_dock.setWidget(self._log_panel)
        self._main_window.addDockWidget(Qt.BottomDockWidgetArea, bottom_dock)

    # -- Signal wiring -----------------------------------------------------

    def _connect_signals(self):
        # ROI selection highlights in image viewer
        self._roi_editor.roi_selected.connect(self._on_roi_selected)

        # Camera panel capture button
        self._camera_panel.capture_requested.connect(self.single_capture)

    def _on_roi_selected(self, x, y, w, h):
        self._image_viewer.clear_overlays()
        if w > 0 and h > 0:
            self._image_viewer.add_overlay_rect(x, y, w, h, "cyan")
        self._image_viewer.update()

    # -- Capture / process -------------------------------------------------

    def single_capture(self):
        cam = self._app.camera
        if cam is None:
            self._log_panel.append_message(3, "No camera connected")
            return
        self._log_panel.append_message(2, "Starting capture...")
        try:
            import vxl
            import numpy as np
            frames = cam.capture_sequence()
            if frames:
                self._log_panel.append_message(2, f"Captured {len(frames)} frames")
                # Show first frame in 2D viewer
                img = frames[0]
                arr = np.array(img, copy=False)
                self._image_viewer.set_image(arr)

                # Ensure we have a recipe/inspector for processing
                self._app.ensure_default_recipe()

                # Reconstruct + inspect in background
                self._run_process(frames)
            else:
                self._log_panel.append_message(3, "Capture returned no frames")
        except ImportError:
            self._log_panel.append_message(4, "vxl module not available")
        except Exception as e:
            self._log_panel.append_message(4, f"Capture error: {e}")

    def _run_process(self, frames):
        self._log_panel.append_message(2, "Starting reconstruction...")
        self._process_worker = ProcessWorker(
            frames, self._app.recipe)
        self._process_worker.reconstruct_done.connect(self._on_reconstruct_done)
        self._process_worker.inspection_done.connect(self._on_inspection_done)
        self._process_worker.error_occurred.connect(self._on_process_error)
        self._process_worker.start()

    def _on_reconstruct_done(self, height_map):
        self._log_panel.append_message(2, "Reconstruction complete")
        self._height_map_viewer.set_height_map(height_map)
        self._tabs.setCurrentIndex(1)  # Switch to height map tab

    def _on_inspection_done(self, result):
        ok_str = "PASS" if result.ok else "FAIL"
        n_defects = len(result.defects) if hasattr(result, 'defects') else 0
        n_measures = len(result.measures) if hasattr(result, 'measures') else 0
        self._log_panel.append_message(
            2, f"Inspection complete: {ok_str} "
               f"(defects={n_defects}, measures={n_measures})")
        self._result_table.set_result(result)
        self._app.record_result(result.ok)

    def _on_process_error(self, msg):
        self._log_panel.append_message(4, f"Process error: {msg}")

    def start_continuous(self):
        cam = self._app.camera
        if cam is None:
            return
        self._app.ensure_default_recipe()
        self._capture_worker = CaptureWorker(cam)
        self._capture_worker.frame_captured.connect(self._on_frames_captured)
        self._capture_worker.start_continuous()
        self._log_panel.append_message(2, "Continuous capture started")

    def stop_continuous(self):
        if self._capture_worker is not None:
            self._capture_worker.stop()
            self._capture_worker = None
            self._log_panel.append_message(2, "Continuous capture stopped")

    def _on_frames_captured(self, frames):
        if frames:
            try:
                import numpy as np
                img = frames[0]
                arr = np.array(img, copy=False)
                self._image_viewer.set_image(arr)
            except Exception:
                pass
            self._run_process(frames)
