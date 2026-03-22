"""CaptureWorker -- background camera capture thread."""

from PySide6.QtCore import QThread, Signal


class CaptureWorker(QThread):
    """Runs camera capture in a background thread.

    Emits *frame_captured* with a list of vxl.Image objects for each
    capture_sequence() call.
    """

    frame_captured = Signal(list)  # list[vxl.Image]

    def __init__(self, camera, parent=None):
        super().__init__(parent)
        self._camera = camera
        self._running = False
        self._single_shot = False

    def start_continuous(self):
        """Start continuous capture loop."""
        self._single_shot = False
        self._running = True
        self.start()

    def single_capture(self):
        """Capture a single sequence and stop."""
        self._single_shot = True
        self._running = True
        self.start()

    def stop(self):
        """Stop the capture loop."""
        self._running = False
        self.wait(3000)

    def run(self):
        try:
            import vxl
            _VxlError = vxl.VxlError
        except (ImportError, AttributeError):
            _VxlError = None

        while self._running:
            try:
                frames = self._camera.capture_sequence()
                self.frame_captured.emit(frames)
            except Exception as e:
                # Camera error -- stop loop
                if _VxlError is not None:
                    try:
                        import vxl
                        vxl.log.error(f"CaptureWorker error: {e}")
                    except Exception:
                        pass
                self._running = False
                break

            if self._single_shot:
                break

        self._running = False
