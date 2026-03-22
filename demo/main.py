#!/usr/bin/env python3
"""VxlStudio Demo -- entry point."""

import sys
import os

# Ensure the project root (parent of demo/) is on sys.path so that
# ``from demo.xxx import ...`` works regardless of how this script is invoked.
_project_root = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
if _project_root not in sys.path:
    sys.path.insert(0, _project_root)

from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QFile, QTextStream

def main():
    # Initialize vxl logging before anything else.
    try:
        import vxl
        vxl.log.init()
        vxl.log.set_level(vxl.log.Level.DEBUG)
        vxl.log.add_console_sink()
    except ImportError:
        print("[warn] vxl module not available -- running in UI-only mode")

    app = QApplication(sys.argv)
    app.setApplicationName("VxlStudio Demo")
    app.setOrganizationName("asVoxel")

    # Load stylesheet.
    style_path = os.path.join(os.path.dirname(__file__), "resources", "styles.qss")
    if os.path.exists(style_path):
        qss_file = QFile(style_path)
        if qss_file.open(QFile.ReadOnly | QFile.Text):
            stream = QTextStream(qss_file)
            app.setStyleSheet(stream.readAll())
            qss_file.close()

    from demo.app import DemoApp
    from demo.views.main_window import MainWindow

    demo_app = DemoApp()

    # Pre-load the default recipe so the pipeline is ready out of the box.
    demo_app.ensure_default_recipe()

    window = MainWindow(demo_app)
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
