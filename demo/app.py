"""DemoApp -- central application state for VxlStudio Demo."""

from enum import Enum

from PySide6.QtCore import QObject, Signal


class AppMode(Enum):
    ENGINEERING = "engineering"
    RUN = "run"


class DemoApp(QObject):
    """Manages application-wide state and coordinates subsystems."""

    # Signals ------------------------------------------------------------------
    mode_changed = Signal(object)            # AppMode
    camera_connected = Signal(bool)          # True = connected
    recipe_loaded = Signal(object)           # recipe name (str) or None
    stats_updated = Signal(int, int, int)    # total, pass_count, ng_count
    log_message = Signal(int, str)           # log level, message

    def __init__(self, parent=None):
        super().__init__(parent)

        # Camera (vxl.ICamera3D or None)
        self._camera = None
        self._camera_connected = False

        # Recipe (vxl.Recipe or None)
        self._recipe = None
        self._recipe_path = None

        # Mode
        self._mode = AppMode.ENGINEERING

        # Inspection stats
        self._total_count = 0
        self._pass_count = 0
        self._ng_count = 0

        # Try to install vxl log callback.
        self._setup_log_callback()

    # -- Log -------------------------------------------------------------------

    def _setup_log_callback(self):
        try:
            import vxl
            vxl.log.add_callback_sink(self._on_vxl_log)
        except (ImportError, AttributeError):
            pass

    def _on_vxl_log(self, level, msg):
        """Called from vxl native log sink (may be on any thread)."""
        self.log_message.emit(int(level), msg)

    # -- Camera ----------------------------------------------------------------

    @property
    def camera(self):
        return self._camera

    @camera.setter
    def camera(self, cam):
        self._camera = cam
        connected = cam is not None
        if connected != self._camera_connected:
            self._camera_connected = connected
            self.camera_connected.emit(connected)

    @property
    def is_camera_connected(self):
        return self._camera_connected

    # -- Recipe ----------------------------------------------------------------

    @property
    def recipe(self):
        return self._recipe

    def load_recipe(self, path: str):
        """Load a recipe from *path* via vxl.Recipe.load()."""
        try:
            import vxl
            result = vxl.Recipe.load(path)
            self._recipe = result
            self._recipe_path = path
            self.recipe_loaded.emit(self._recipe.name())
        except ImportError:
            # UI-only mode: store the path but no real recipe.
            self._recipe = None
            self._recipe_path = path
            self.recipe_loaded.emit(path)
        except Exception as e:
            self.log_message.emit(4, f"Failed to load recipe: {e}")

    def ensure_default_recipe(self):
        """Create a default inline recipe if none has been loaded.

        Uses vxl.Inspector3D directly so that the demo works out of the box
        without requiring a recipe JSON file on disk.
        """
        if self._recipe is not None:
            return
        # First try loading the bundled recipe file
        import os
        default_path = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            "recipes", "pcb_smt", "pcb_model_a.json")
        if os.path.exists(default_path):
            self.load_recipe(default_path)
            if self._recipe is not None:
                return
        # Fallback: build an Inspector3D programmatically and wrap it
        try:
            import vxl
            inspector = vxl.Inspector3D()
            cfg = vxl.InspectorConfig()
            cfg.name = "solder_height"
            cfg.type = "height_measure"
            cfg.rois = [vxl.ROI(100, 100, 200, 200)]
            cfg.params = {"ref_height": 0.0}
            cfg.severity = "critical"
            inspector.add_inspector(cfg)

            cfg2 = vxl.InspectorConfig()
            cfg2.name = "board_flatness"
            cfg2.type = "flatness"
            cfg2.rois = [vxl.ROI(50, 50, 300, 300)]
            cfg2.params = {"max_flatness_mm": 0.1}
            cfg2.severity = "warning"
            inspector.add_inspector(cfg2)

            self._recipe = inspector  # Inspector3D has .run(hmap) like Recipe.inspect(hmap)
            self._recipe_path = None
            self.recipe_loaded.emit("(default inline)")
            self.log_message.emit(2, "Using default inline inspection config")
        except ImportError:
            pass
        except Exception as e:
            self.log_message.emit(3, f"Could not create default inspector: {e}")

    def save_recipe(self, path: str = None):
        if path is not None:
            self._recipe_path = path
        if self._recipe is not None and self._recipe_path:
            if hasattr(self._recipe, 'save'):
                self._recipe.save(self._recipe_path)
            else:
                self.log_message.emit(3, "Current inspector config cannot be saved as recipe")

    @property
    def recipe_path(self):
        return self._recipe_path

    # -- Mode ------------------------------------------------------------------

    @property
    def mode(self):
        return self._mode

    @mode.setter
    def mode(self, new_mode: AppMode):
        if new_mode != self._mode:
            self._mode = new_mode
            self.mode_changed.emit(new_mode)

    # -- Stats -----------------------------------------------------------------

    @property
    def total_count(self):
        return self._total_count

    @property
    def pass_count(self):
        return self._pass_count

    @property
    def ng_count(self):
        return self._ng_count

    def record_result(self, ok: bool):
        self._total_count += 1
        if ok:
            self._pass_count += 1
        else:
            self._ng_count += 1
        self.stats_updated.emit(self._total_count, self._pass_count, self._ng_count)

    def reset_stats(self):
        self._total_count = 0
        self._pass_count = 0
        self._ng_count = 0
        self.stats_updated.emit(0, 0, 0)
