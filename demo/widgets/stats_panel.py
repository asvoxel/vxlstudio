"""StatsPanel -- inspection statistics with yield rate and defect chart."""

from PySide6.QtCore import Qt
from PySide6.QtGui import QPainter, QColor, QFont
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QGroupBox,
)

from demo.app import DemoApp


class _DefectChart(QWidget):
    """Simple bar chart of defect types drawn with QPainter."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._data = {}  # type_name -> count
        self.setMinimumHeight(100)

    def set_data(self, data: dict):
        self._data = dict(data)
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), QColor("#2b2b2b"))

        if not self._data:
            painter.setPen(QColor("#888"))
            painter.drawText(self.rect(), Qt.AlignCenter, "No defects")
            painter.end()
            return

        max_val = max(self._data.values()) if self._data else 1
        w = self.width()
        h = self.height()
        n = len(self._data)
        bar_w = max(20, (w - 20) // max(n, 1))
        colors = ["#e74c3c", "#e67e22", "#f1c40f", "#9b59b6", "#3498db"]

        x = 10
        for i, (name, count) in enumerate(self._data.items()):
            bar_h = int((count / max(max_val, 1)) * (h - 30))
            color = QColor(colors[i % len(colors)])
            painter.setBrush(color)
            painter.setPen(Qt.NoPen)
            painter.drawRect(x, h - 20 - bar_h, bar_w - 4, bar_h)

            painter.setPen(QColor("#e0e0e0"))
            painter.setFont(QFont("Sans", 8))
            painter.drawText(x, h - 6, name[:8])
            painter.drawText(x, h - 22 - bar_h, str(count))
            x += bar_w

        painter.end()


class StatsPanel(QWidget):
    """Displays total count, pass/NG, yield rate, and defect distribution."""

    def __init__(self, app: DemoApp, parent=None):
        super().__init__(parent)
        self._app = app
        self._defect_counts = {}  # defect_type -> count
        self._build_ui()
        app.stats_updated.connect(self._on_stats_updated)

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        group = QGroupBox("Statistics")
        glayout = QVBoxLayout(group)

        # Numeric stats
        stats_layout = QHBoxLayout()
        self._lbl_total = QLabel("Total: 0")
        self._lbl_pass = QLabel("Pass: 0")
        self._lbl_ng = QLabel("NG: 0")
        self._lbl_yield = QLabel("Yield: --")
        for lbl in (self._lbl_total, self._lbl_pass, self._lbl_ng, self._lbl_yield):
            lbl.setAlignment(Qt.AlignCenter)
            lbl.setStyleSheet("font-size: 14px; font-weight: bold; padding: 4px;")
            stats_layout.addWidget(lbl)
        glayout.addLayout(stats_layout)

        # Defect chart
        self._chart = _DefectChart()
        glayout.addWidget(self._chart)

        # Reset
        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        self._btn_reset = QPushButton("Reset")
        self._btn_reset.clicked.connect(self._reset)
        btn_layout.addWidget(self._btn_reset)
        glayout.addLayout(btn_layout)

        layout.addWidget(group)

    def _on_stats_updated(self, total, pass_c, ng_c):
        self._lbl_total.setText(f"Total: {total}")
        self._lbl_pass.setText(f"Pass: {pass_c}")
        self._lbl_pass.setStyleSheet(
            "font-size: 14px; font-weight: bold; padding: 4px; color: #27ae60;")
        self._lbl_ng.setText(f"NG: {ng_c}")
        self._lbl_ng.setStyleSheet(
            "font-size: 14px; font-weight: bold; padding: 4px; color: #e74c3c;")
        if total > 0:
            rate = pass_c / total * 100
            self._lbl_yield.setText(f"Yield: {rate:.1f}%")
        else:
            self._lbl_yield.setText("Yield: --")

    def record_defect(self, defect_type: str):
        self._defect_counts[defect_type] = self._defect_counts.get(defect_type, 0) + 1
        self._chart.set_data(self._defect_counts)

    def _reset(self):
        self._app.reset_stats()
        self._defect_counts.clear()
        self._chart.set_data({})
