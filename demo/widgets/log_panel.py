"""LogPanel -- color-coded log message viewer."""

from PySide6.QtCore import Qt
from PySide6.QtGui import QTextCharFormat, QColor, QFont
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPlainTextEdit, QPushButton,
    QGroupBox,
)

from demo.app import DemoApp

# Log level constants matching vxl::log::Level
_LEVEL_NAMES = {0: "TRACE", 1: "DEBUG", 2: "INFO", 3: "WARN", 4: "ERROR", 5: "FATAL"}
_LEVEL_COLORS = {
    0: "#888888",  # trace - dark gray
    1: "#aaaaaa",  # debug - gray
    2: "#e0e0e0",  # info  - white
    3: "#f1c40f",  # warn  - yellow
    4: "#e74c3c",  # error - red
    5: "#e74c3c",  # fatal - red
}

_MAX_LINES = 5000


class LogPanel(QWidget):
    """Read-only log viewer with colored messages."""

    def __init__(self, app: DemoApp = None, parent=None):
        super().__init__(parent)
        self._app = app
        self._build_ui()

        if app is not None:
            app.log_message.connect(self.append_message)

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        group = QGroupBox("Log")
        glayout = QVBoxLayout(group)

        self._text = QPlainTextEdit()
        self._text.setReadOnly(True)
        self._text.setMaximumBlockCount(_MAX_LINES)
        font = QFont("Courier New", 9)
        font.setStyleHint(QFont.Monospace)
        self._text.setFont(font)
        glayout.addWidget(self._text)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch()
        self._btn_clear = QPushButton("Clear")
        self._btn_clear.clicked.connect(self._text.clear)
        btn_layout.addWidget(self._btn_clear)
        glayout.addLayout(btn_layout)

        layout.addWidget(group)

    def append_message(self, level: int, msg: str):
        """Append a log message with color coding by level."""
        level_name = _LEVEL_NAMES.get(level, "???")
        color = _LEVEL_COLORS.get(level, "#e0e0e0")

        fmt = QTextCharFormat()
        fmt.setForeground(QColor(color))

        cursor = self._text.textCursor()
        cursor.movePosition(cursor.End)
        cursor.insertText(f"[{level_name:5s}] {msg}\n", fmt)

        # Auto-scroll
        self._text.setTextCursor(cursor)
        self._text.ensureCursorVisible()
