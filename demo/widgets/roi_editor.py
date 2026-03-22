"""ROIEditor -- list-based ROI editor with add/delete."""

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QPushButton,
    QTableWidget, QTableWidgetItem, QHeaderView, QGroupBox,
    QInputDialog,
)


class ROIEditor(QWidget):
    """Editable list of ROIs displayed in a table."""

    roi_selected = Signal(int, int, int, int)  # x, y, w, h
    roi_changed = Signal()                     # any change to the ROI list

    def __init__(self, parent=None):
        super().__init__(parent)
        self._build_ui()
        self._connect_signals()

    def _build_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        group = QGroupBox("ROI Editor")
        glayout = QVBoxLayout(group)

        self._table = QTableWidget(0, 5)
        self._table.setHorizontalHeaderLabels(["Name", "X", "Y", "W", "H"])
        self._table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._table.setSelectionBehavior(QTableWidget.SelectRows)
        self._table.setSelectionMode(QTableWidget.SingleSelection)
        glayout.addWidget(self._table)

        btn_layout = QHBoxLayout()
        self._btn_add = QPushButton("Add")
        self._btn_delete = QPushButton("Delete")
        btn_layout.addWidget(self._btn_add)
        btn_layout.addWidget(self._btn_delete)
        glayout.addLayout(btn_layout)

        layout.addWidget(group)

    def _connect_signals(self):
        self._btn_add.clicked.connect(self._add_roi)
        self._btn_delete.clicked.connect(self._delete_roi)
        self._table.currentCellChanged.connect(self._on_selection_changed)
        self._table.cellChanged.connect(self._on_cell_changed)

    # -- Public API --------------------------------------------------------

    def set_rois(self, rois):
        """Populate table from a list of vxl.ROI or dicts."""
        self._table.blockSignals(True)
        self._table.setRowCount(0)
        for i, roi in enumerate(rois):
            if hasattr(roi, "x"):
                name = getattr(roi, "name", f"ROI_{i}")
                x, y, w, h = roi.x, roi.y, roi.w, roi.h
            else:
                name = roi.get("name", f"ROI_{i}")
                x, y, w, h = roi["x"], roi["y"], roi["w"], roi["h"]
            self._append_row(name, x, y, w, h)
        self._table.blockSignals(False)

    def get_rois(self):
        """Return list of dicts with name, x, y, w, h."""
        rois = []
        for row in range(self._table.rowCount()):
            rois.append({
                "name": self._table.item(row, 0).text(),
                "x": int(self._table.item(row, 1).text()),
                "y": int(self._table.item(row, 2).text()),
                "w": int(self._table.item(row, 3).text()),
                "h": int(self._table.item(row, 4).text()),
            })
        return rois

    # -- Internal ----------------------------------------------------------

    def _append_row(self, name, x, y, w, h):
        row = self._table.rowCount()
        self._table.insertRow(row)
        self._table.setItem(row, 0, QTableWidgetItem(str(name)))
        self._table.setItem(row, 1, QTableWidgetItem(str(x)))
        self._table.setItem(row, 2, QTableWidgetItem(str(y)))
        self._table.setItem(row, 3, QTableWidgetItem(str(w)))
        self._table.setItem(row, 4, QTableWidgetItem(str(h)))

    def _add_roi(self):
        name, ok = QInputDialog.getText(self, "Add ROI", "ROI name:")
        if ok and name:
            self._append_row(name, 0, 0, 100, 100)
            self.roi_changed.emit()

    def _delete_roi(self):
        row = self._table.currentRow()
        if row >= 0:
            self._table.removeRow(row)
            self.roi_changed.emit()

    def _on_selection_changed(self, row, _col, _prev_row, _prev_col):
        if row < 0:
            return
        try:
            x = int(self._table.item(row, 1).text())
            y = int(self._table.item(row, 2).text())
            w = int(self._table.item(row, 3).text())
            h = int(self._table.item(row, 4).text())
            self.roi_selected.emit(x, y, w, h)
        except (ValueError, AttributeError):
            pass

    def _on_cell_changed(self, row, col):
        self.roi_changed.emit()
        # Re-emit selection to update overlay
        self._on_selection_changed(row, col, -1, -1)
