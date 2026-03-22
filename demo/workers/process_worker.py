"""ProcessWorker -- background reconstruction and inspection thread."""

from PySide6.QtCore import QThread, Signal


class ProcessWorker(QThread):
    """Receives captured frames, runs reconstruction and inspection.

    Emits *reconstruct_done* with the HeightMap and *inspection_done*
    with the InspectionResult.
    """

    reconstruct_done = Signal(object)   # vxl.HeightMap
    inspection_done = Signal(object)    # vxl.InspectionResult
    error_occurred = Signal(str)        # error message

    def __init__(self, frames, recipe=None, calib_params=None, parent=None):
        super().__init__(parent)
        self._frames = frames
        self._recipe = recipe
        self._calib_params = calib_params

    def run(self):
        try:
            import vxl
        except ImportError:
            return

        try:
            # Calibration params
            calib = self._calib_params
            if calib is None:
                calib = vxl.CalibrationParams.default_sim()

            # Reconstruct
            height_map = vxl.reconstruct(self._frames, calib)
            self.reconstruct_done.emit(height_map)

            # Inspect -- support both Recipe (.inspect) and Inspector3D (.run)
            recipe = self._recipe
            if recipe is not None:
                if hasattr(recipe, 'inspect'):
                    result = recipe.inspect(height_map)
                elif hasattr(recipe, 'run'):
                    result = recipe.run(height_map)
                else:
                    return
                self.inspection_done.emit(result)

        except vxl.VxlError as e:
            self.error_occurred.emit(f"vxl error: {e}")
            try:
                vxl.log.error(f"ProcessWorker vxl error: {e}")
            except Exception:
                pass
        except Exception as e:
            self.error_occurred.emit(str(e))
            try:
                vxl.log.error(f"ProcessWorker error: {e}")
            except Exception:
                pass
