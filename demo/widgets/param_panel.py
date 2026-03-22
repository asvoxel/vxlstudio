"""ParamPanel -- auto-generated parameter editing form from Recipe."""

from PySide6.QtCore import Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QFormLayout, QGroupBox, QScrollArea,
    QDoubleSpinBox, QSpinBox, QComboBox, QLabel,
)

from demo.app import DemoApp


class ParamPanel(QWidget):
    """Displays editable parameters grouped by inspector from the current recipe."""

    param_changed = Signal(str, str, object)  # inspector_name, param_name, value

    def __init__(self, app: DemoApp, parent=None):
        super().__init__(parent)
        self._app = app
        self._widgets = {}  # (inspector_name, param_name) -> widget

        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        self._scroll = QScrollArea()
        self._scroll.setWidgetResizable(True)
        self._container = QWidget()
        self._container_layout = QVBoxLayout(self._container)
        self._container_layout.addStretch()
        self._scroll.setWidget(self._container)
        layout.addWidget(self._scroll)

        app.recipe_loaded.connect(self._on_recipe_loaded)

    def _on_recipe_loaded(self, _name):
        self._rebuild()

    def _rebuild(self):
        """Rebuild all parameter groups from the current recipe."""
        # Clear existing
        while self._container_layout.count() > 1:
            item = self._container_layout.takeAt(0)
            if item.widget():
                item.widget().deleteLater()
        self._widgets.clear()

        recipe = self._app.recipe
        if recipe is None:
            return

        try:
            configs = recipe.inspector_configs()
        except (AttributeError, TypeError):
            return

        for cfg in configs:
            name = cfg.name if hasattr(cfg, "name") else str(cfg)
            group = QGroupBox(name)
            form = QFormLayout(group)

            params = cfg.params if hasattr(cfg, "params") else {}
            if not params:
                form.addRow(QLabel("(no parameters)"))

            for pname, value in params.items():
                widget = self._create_widget(name, pname, value)
                form.addRow(pname, widget)
                self._widgets[(name, pname)] = widget

            # Insert before the stretch
            idx = self._container_layout.count() - 1
            self._container_layout.insertWidget(idx, group)

    def _create_widget(self, inspector_name, param_name, value):
        if isinstance(value, float):
            w = QDoubleSpinBox()
            w.setRange(-1e9, 1e9)
            w.setDecimals(4)
            w.setValue(value)
            w.valueChanged.connect(
                lambda v, iname=inspector_name, pname=param_name:
                    self.param_changed.emit(iname, pname, v))
            return w
        elif isinstance(value, int):
            w = QSpinBox()
            w.setRange(-999999, 999999)
            w.setValue(value)
            w.valueChanged.connect(
                lambda v, iname=inspector_name, pname=param_name:
                    self.param_changed.emit(iname, pname, v))
            return w
        elif isinstance(value, str):
            # Treat as enum-like if value looks like choices; otherwise plain label
            w = QComboBox()
            w.addItem(value)
            w.setEditable(True)
            w.currentTextChanged.connect(
                lambda v, iname=inspector_name, pname=param_name:
                    self.param_changed.emit(iname, pname, v))
            return w
        else:
            lbl = QLabel(str(value))
            return lbl
