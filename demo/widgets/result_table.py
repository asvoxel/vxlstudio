"""ResultTable -- per-inspector inspection results display."""

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QTableView, QHeaderView, QGroupBox,
)
from PySide6.QtCore import QAbstractTableModel, QModelIndex


_COLUMNS = ["Inspector", "Type", "Pass/Fail", "Value", "Severity"]


class _ResultModel(QAbstractTableModel):
    """Table model backing the result display."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._rows = []  # list of dicts

    def set_rows(self, rows):
        self.beginResetModel()
        self._rows = list(rows)
        self.endResetModel()

    def rowCount(self, _parent=QModelIndex()):
        return len(self._rows)

    def columnCount(self, _parent=QModelIndex()):
        return len(_COLUMNS)

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if role == Qt.DisplayRole and orientation == Qt.Horizontal:
            return _COLUMNS[section]
        return None

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        row = self._rows[index.row()]
        col = index.column()

        if role == Qt.DisplayRole:
            if col == 0:
                return row.get("name", "")
            elif col == 1:
                return row.get("type", "")
            elif col == 2:
                return "PASS" if row.get("pass", True) else "FAIL"
            elif col == 3:
                return row.get("value", "")
            elif col == 4:
                return row.get("severity", "")

        elif role == Qt.BackgroundRole:
            passed = row.get("pass", True)
            severity = row.get("severity", "")
            if not passed:
                return QColor("#e74c3c")  # red
            elif severity == "warning":
                return QColor("#f39c12")  # yellow
            else:
                return QColor("#27ae60")  # green

        elif role == Qt.ForegroundRole:
            return QColor("white")

        return None


class ResultTable(QWidget):
    """Displays inspection results in a colored table."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        group = QGroupBox("Inspection Results")
        glayout = QVBoxLayout(group)

        self._model = _ResultModel()
        self._view = QTableView()
        self._view.setModel(self._model)
        self._view.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._view.setSelectionBehavior(QTableView.SelectRows)
        self._view.setEditTriggers(QTableView.NoEditTriggers)
        glayout.addWidget(self._view)

        layout.addWidget(group)

    def set_result(self, result):
        """Update from a vxl.InspectionResult.

        The result object has:
          - ok (bool): overall pass/fail
          - defects (list[DefectRegion]): detected defects
          - measures (list[MeasureResult]): measurement results
        """
        rows = []
        overall_ok = getattr(result, "ok", True)

        # Summary row
        rows.append({
            "name": "Overall",
            "type": "summary",
            "pass": overall_ok,
            "value": "PASS" if overall_ok else "FAIL",
            "severity": "critical" if not overall_ok else "",
        })

        # Defect rows
        for i, defect in enumerate(getattr(result, "defects", [])):
            d_type = getattr(defect, "type", "defect")
            area = getattr(defect, "area_mm2", 0.0)
            max_h = getattr(defect, "max_height", 0.0)
            rows.append({
                "name": f"Defect_{i}",
                "type": d_type,
                "pass": False,
                "value": f"area={area:.2f}mm2, h={max_h:.3f}mm",
                "severity": "critical",
            })

        # Measure rows
        for i, meas in enumerate(getattr(result, "measures", [])):
            avg_h = getattr(meas, "avg_height", 0.0)
            min_h = getattr(meas, "min_height", 0.0)
            max_h = getattr(meas, "max_height", 0.0)
            rows.append({
                "name": f"Measure_{i}",
                "type": "height_measure",
                "pass": True,
                "value": f"avg={avg_h:.3f}, min={min_h:.3f}, max={max_h:.3f}",
                "severity": "",
            })

        self._model.set_rows(rows)
