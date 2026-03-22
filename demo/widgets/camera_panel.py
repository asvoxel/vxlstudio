"""CameraPanel -- camera connection and capture controls."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout,
    QComboBox, QPushButton, QSlider, QSpinBox, QCheckBox,
    QLabel, QGroupBox, QMessageBox,
)

from demo.app import DemoApp


class CameraPanel(QWidget):
    """Camera device selection, connection, exposure, and capture controls."""

    capture_requested = Signal()

    def __init__(self, app: DemoApp, parent=None):
        super().__init__(parent)
        self._app = app
        self._build_ui()
        self._connect_signals()
        self._refresh_devices()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        group = QGroupBox("Camera")
        glayout = QVBoxLayout(group)

        # Device selection
        dev_layout = QHBoxLayout()
        self._combo_device = QComboBox()
        self._btn_refresh = QPushButton("Refresh")
        dev_layout.addWidget(self._combo_device, stretch=1)
        dev_layout.addWidget(self._btn_refresh)
        glayout.addLayout(dev_layout)

        # Connect / Disconnect
        conn_layout = QHBoxLayout()
        self._btn_connect = QPushButton("Connect")
        self._btn_disconnect = QPushButton("Disconnect")
        self._btn_disconnect.setEnabled(False)
        conn_layout.addWidget(self._btn_connect)
        conn_layout.addWidget(self._btn_disconnect)
        glayout.addLayout(conn_layout)

        # Status
        self._lbl_status = QLabel("Disconnected")
        self._lbl_status.setAlignment(Qt.AlignCenter)
        self._lbl_status.setStyleSheet(
            "color: #e74c3c; font-weight: bold; padding: 4px;")
        glayout.addWidget(self._lbl_status)

        # Exposure
        form = QFormLayout()
        exp_layout = QHBoxLayout()
        self._slider_exposure = QSlider(Qt.Horizontal)
        self._slider_exposure.setRange(100, 100000)
        self._slider_exposure.setValue(10000)
        self._spin_exposure = QSpinBox()
        self._spin_exposure.setRange(100, 100000)
        self._spin_exposure.setValue(10000)
        self._spin_exposure.setSuffix(" us")
        exp_layout.addWidget(self._slider_exposure, stretch=1)
        exp_layout.addWidget(self._spin_exposure)
        form.addRow("Exposure:", exp_layout)

        # Fringe count
        self._spin_fringe = QSpinBox()
        self._spin_fringe.setRange(1, 64)
        self._spin_fringe.setValue(12)
        form.addRow("Fringe count:", self._spin_fringe)
        glayout.addLayout(form)

        # Capture + Live
        cap_layout = QHBoxLayout()
        self._btn_capture = QPushButton("Capture")
        self._btn_capture.setEnabled(False)
        self._chk_live = QCheckBox("Live preview")
        cap_layout.addWidget(self._btn_capture)
        cap_layout.addWidget(self._chk_live)
        glayout.addLayout(cap_layout)

        layout.addWidget(group)

    def _connect_signals(self):
        self._btn_refresh.clicked.connect(self._refresh_devices)
        self._btn_connect.clicked.connect(self._connect)
        self._btn_disconnect.clicked.connect(self._disconnect)
        self._btn_capture.clicked.connect(self.capture_requested.emit)

        # Sync slider <-> spinbox
        self._slider_exposure.valueChanged.connect(self._spin_exposure.setValue)
        self._spin_exposure.valueChanged.connect(self._slider_exposure.setValue)
        self._spin_exposure.valueChanged.connect(self._set_exposure)

        self._app.camera_connected.connect(self._on_camera_state)

    # -- Device enumeration ------------------------------------------------

    def _refresh_devices(self):
        self._combo_device.clear()
        try:
            import vxl
            devices = vxl.Camera.enumerate()
            if devices:
                self._combo_device.addItems(devices)
            else:
                # No devices enumerated; offer the simulator id
                self._combo_device.addItem("SIM-001")
        except ImportError:
            self._combo_device.addItem("SIM-001")

    # -- Connection --------------------------------------------------------

    def _connect(self):
        device_id = self._combo_device.currentText()
        if not device_id:
            return
        try:
            import vxl
            cam = vxl.Camera.open_3d(device_id)
            self._app.camera = cam
        except ImportError:
            # UI-only mode
            self._app.camera = object()
        except Exception as e:
            QMessageBox.critical(self, "Camera Error", str(e))

    def _disconnect(self):
        cam = self._app.camera
        if cam is not None:
            try:
                cam.close()
            except Exception:
                pass
            self._app.camera = None

    def _on_camera_state(self, connected: bool):
        self._btn_connect.setEnabled(not connected)
        self._btn_disconnect.setEnabled(connected)
        self._btn_capture.setEnabled(connected)
        self._combo_device.setEnabled(not connected)
        if connected:
            self._lbl_status.setText("Connected")
            self._lbl_status.setStyleSheet(
                "color: #27ae60; font-weight: bold; padding: 4px;")
        else:
            self._lbl_status.setText("Disconnected")
            self._lbl_status.setStyleSheet(
                "color: #e74c3c; font-weight: bold; padding: 4px;")

    # -- Exposure ----------------------------------------------------------

    def _set_exposure(self, value):
        cam = self._app.camera
        if cam is not None:
            try:
                cam.set_exposure(value)
            except Exception:
                pass
