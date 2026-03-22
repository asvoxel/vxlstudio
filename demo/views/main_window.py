"""MainWindow -- top-level window for VxlStudio Demo."""

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QIcon, QKeySequence
from PySide6.QtWidgets import (
    QMainWindow, QStackedWidget, QToolBar, QLabel,
    QFileDialog, QMessageBox, QStatusBar,
)

from demo.app import DemoApp, AppMode
from demo.views.engineering_view import EngineeringView
from demo.views.run_view import RunView


class MainWindow(QMainWindow):
    def __init__(self, app: DemoApp, parent=None):
        super().__init__(parent)
        self._app = app
        self.setWindowTitle("VxlStudio Demo")
        self.resize(1400, 900)

        # Central stacked widget -------------------------------------------
        self._stack = QStackedWidget()
        self._engineering_view = EngineeringView(app, self)
        self._run_view = RunView(app, self)
        self._stack.addWidget(self._engineering_view)
        self._stack.addWidget(self._run_view)
        self.setCentralWidget(self._stack)

        # Menu bar ---------------------------------------------------------
        self._build_menus()

        # Toolbar ----------------------------------------------------------
        self._build_toolbar()

        # Status bar -------------------------------------------------------
        self._build_status_bar()

        # Connections ------------------------------------------------------
        app.mode_changed.connect(self._on_mode_changed)
        app.camera_connected.connect(self._on_camera_connected)
        app.recipe_loaded.connect(self._on_recipe_loaded)
        app.stats_updated.connect(self._on_stats_updated)

    # -- Menu bar ----------------------------------------------------------

    def _build_menus(self):
        mb = self.menuBar()

        # File
        file_menu = mb.addMenu("&File")
        self._act_open_recipe = file_menu.addAction("&Open Recipe...")
        self._act_open_recipe.setShortcut(QKeySequence.Open)
        self._act_open_recipe.triggered.connect(self._open_recipe)

        self._act_save_recipe = file_menu.addAction("&Save Recipe...")
        self._act_save_recipe.setShortcut(QKeySequence.Save)
        self._act_save_recipe.triggered.connect(self._save_recipe)

        file_menu.addSeparator()
        act_exit = file_menu.addAction("E&xit")
        act_exit.setShortcut(QKeySequence.Quit)
        act_exit.triggered.connect(self.close)

        # Camera
        camera_menu = mb.addMenu("&Camera")
        self._act_connect = camera_menu.addAction("&Connect")
        self._act_connect.triggered.connect(self._connect_camera)
        self._act_disconnect = camera_menu.addAction("&Disconnect")
        self._act_disconnect.triggered.connect(self._disconnect_camera)
        self._act_disconnect.setEnabled(False)
        camera_menu.addSeparator()
        self._act_calibration = camera_menu.addAction("标定向导...")
        self._act_calibration.triggered.connect(self._open_calibration_wizard)

        # Mode
        mode_menu = mb.addMenu("&Mode")
        self._act_engineering = mode_menu.addAction("&Engineering")
        self._act_engineering.setCheckable(True)
        self._act_engineering.setChecked(True)
        self._act_engineering.triggered.connect(lambda: self._set_mode(AppMode.ENGINEERING))
        self._act_run = mode_menu.addAction("&Run")
        self._act_run.setCheckable(True)
        self._act_run.triggered.connect(lambda: self._set_mode(AppMode.RUN))

        # Help
        help_menu = mb.addMenu("&Help")
        act_about = help_menu.addAction("&About")
        act_about.triggered.connect(self._show_about)

    # -- Toolbar -----------------------------------------------------------

    def _build_toolbar(self):
        tb = QToolBar("Main")
        tb.setMovable(False)
        self.addToolBar(tb)

        self._tb_connect = tb.addAction("Connect")
        self._tb_connect.triggered.connect(self._connect_camera)

        self._tb_capture = tb.addAction("Capture")
        self._tb_capture.triggered.connect(self._single_capture)
        self._tb_capture.setEnabled(False)

        self._tb_run = tb.addAction("Run")
        self._tb_run.triggered.connect(self._start_continuous)
        self._tb_run.setEnabled(False)

        self._tb_stop = tb.addAction("Stop")
        self._tb_stop.triggered.connect(self._stop_continuous)
        self._tb_stop.setEnabled(False)

        tb.addSeparator()

        self._tb_mode = tb.addAction("Mode: Engineering")
        self._tb_mode.triggered.connect(self._toggle_mode)

    # -- Status bar --------------------------------------------------------

    def _build_status_bar(self):
        sb = QStatusBar()
        self.setStatusBar(sb)

        self._lbl_camera = QLabel("Camera: disconnected")
        self._lbl_recipe = QLabel("Recipe: (none)")
        self._lbl_stats = QLabel("Total: 0  Pass: 0  NG: 0")

        sb.addWidget(self._lbl_camera, 1)
        sb.addWidget(self._lbl_recipe, 1)
        sb.addWidget(self._lbl_stats, 1)

    # -- Slots -------------------------------------------------------------

    def _on_mode_changed(self, mode: AppMode):
        is_eng = mode == AppMode.ENGINEERING
        self._stack.setCurrentIndex(0 if is_eng else 1)
        self._act_engineering.setChecked(is_eng)
        self._act_run.setChecked(not is_eng)
        self._tb_mode.setText(f"Mode: {mode.value.capitalize()}")

    def _on_camera_connected(self, connected: bool):
        self._lbl_camera.setText(f"Camera: {'connected' if connected else 'disconnected'}")
        self._act_connect.setEnabled(not connected)
        self._act_disconnect.setEnabled(connected)
        self._tb_connect.setEnabled(not connected)
        self._tb_capture.setEnabled(connected)
        self._tb_run.setEnabled(connected)

    def _on_recipe_loaded(self, name):
        self._lbl_recipe.setText(f"Recipe: {name}")

    def _on_stats_updated(self, total, pass_c, ng_c):
        self._lbl_stats.setText(f"Total: {total}  Pass: {pass_c}  NG: {ng_c}")

    # -- Actions -----------------------------------------------------------

    def _open_recipe(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open Recipe", "", "Recipe Files (*.json);;All Files (*)")
        if path:
            self._app.load_recipe(path)

    def _save_recipe(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Save Recipe", "", "Recipe Files (*.json);;All Files (*)")
        if path:
            self._app.save_recipe(path)

    def _connect_camera(self):
        """Connect to SimCamera (first enumerated device or SIM-001)."""
        try:
            import vxl
            devices = vxl.Camera.enumerate()
            if not devices:
                # Fall back to SIM-001 (the simulator device id)
                devices = ["SIM-001"]
            cam = vxl.Camera.open_3d(devices[0])
            self._app.camera = cam
            self._app.log_message.emit(2, f"Camera connected: {devices[0]}")
        except ImportError:
            QMessageBox.information(self, "Camera",
                                   "vxl module not available -- simulating connection.")
            self._app.camera = object()  # placeholder
        except Exception as e:
            QMessageBox.critical(self, "Camera Error", str(e))

    def _disconnect_camera(self):
        if self._app.camera is not None:
            try:
                self._app.camera.close()
            except Exception:
                pass
            self._app.camera = None

    def _set_mode(self, mode: AppMode):
        self._app.mode = mode

    def _toggle_mode(self):
        if self._app.mode == AppMode.ENGINEERING:
            self._app.mode = AppMode.RUN
        else:
            self._app.mode = AppMode.ENGINEERING

    def _single_capture(self):
        self._engineering_view.single_capture()

    def _start_continuous(self):
        self._tb_run.setEnabled(False)
        self._tb_stop.setEnabled(True)
        if self._app.mode == AppMode.RUN:
            self._run_view.start()
        else:
            self._engineering_view.start_continuous()

    def _stop_continuous(self):
        self._tb_run.setEnabled(True)
        self._tb_stop.setEnabled(False)
        if self._app.mode == AppMode.RUN:
            self._run_view.stop()
        else:
            self._engineering_view.stop_continuous()

    def _open_calibration_wizard(self):
        from demo.views.calibration_wizard import CalibrationWizard
        wizard = CalibrationWizard(self)
        wizard.exec()

    def _show_about(self):
        QMessageBox.about(
            self,
            "About VxlStudio Demo",
            "VxlStudio Demo\n\n"
            "A PySide6 demonstration application for the vxl "
            "structured-light 3D inspection SDK.\n\n"
            "(c) asVoxel",
        )
