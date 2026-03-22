"""Tests for VxlStudio Python bindings.

Run with:  pytest python/tests/test_bindings.py -v
"""

import numpy as np
import pytest

import vxl


# =============================================================================
# Type existence
# =============================================================================

class TestTypesExist:
    """Verify that all major bound types are importable."""

    def test_error_code(self):
        assert hasattr(vxl, "ErrorCode")
        assert vxl.ErrorCode.OK is not None

    def test_pixel_format(self):
        assert vxl.PixelFormat.GRAY8 is not None
        assert vxl.PixelFormat.FLOAT32 is not None

    def test_point_format(self):
        assert vxl.PointFormat.XYZ_FLOAT is not None

    def test_image(self):
        assert hasattr(vxl, "Image")

    def test_height_map(self):
        assert hasattr(vxl, "HeightMap")

    def test_point_cloud(self):
        assert hasattr(vxl, "PointCloud")

    def test_roi(self):
        assert hasattr(vxl, "ROI")

    def test_pose6d(self):
        assert hasattr(vxl, "Pose6D")

    def test_defect_region(self):
        assert hasattr(vxl, "DefectRegion")

    def test_measure_result(self):
        assert hasattr(vxl, "MeasureResult")

    def test_inspection_result(self):
        assert hasattr(vxl, "InspectionResult")

    def test_calibration_params(self):
        assert hasattr(vxl, "CalibrationParams")

    def test_reconstruct_params(self):
        assert hasattr(vxl, "ReconstructParams")

    def test_inspector3d(self):
        assert hasattr(vxl, "Inspector3D")

    def test_inspector_config(self):
        assert hasattr(vxl, "InspectorConfig")

    def test_recipe(self):
        assert hasattr(vxl, "Recipe")

    def test_message_bus(self):
        assert hasattr(vxl, "MessageBus")

    def test_plane(self):
        assert hasattr(vxl, "Plane")

    def test_height_map_processor(self):
        assert hasattr(vxl, "HeightMapProcessor")

    def test_camera_submodule(self):
        assert hasattr(vxl, "Camera")
        assert callable(vxl.Camera.enumerate)

    def test_log_submodule(self):
        assert hasattr(vxl, "log")
        assert hasattr(vxl.log, "Level")

    def test_version(self):
        assert vxl.__version__ == "0.1.0"


# =============================================================================
# Image <-> numpy round-trip
# =============================================================================

class TestImageNumpy:
    """Image / numpy interop via buffer protocol."""

    def test_gray8_round_trip(self):
        arr = np.random.randint(0, 256, (480, 640), dtype=np.uint8)
        img = vxl.Image.from_numpy(arr)
        assert img.width == 640
        assert img.height == 480
        assert img.format == vxl.PixelFormat.GRAY8

        out = img.to_numpy()
        np.testing.assert_array_equal(out, arr)

    def test_float32_round_trip(self):
        arr = np.random.rand(100, 200).astype(np.float32)
        img = vxl.Image.from_numpy(arr)
        assert img.format == vxl.PixelFormat.FLOAT32
        out = img.to_numpy()
        np.testing.assert_allclose(out, arr)

    def test_rgb8_round_trip(self):
        arr = np.random.randint(0, 256, (60, 80, 3), dtype=np.uint8)
        img = vxl.Image.from_numpy(arr)
        assert img.format == vxl.PixelFormat.RGB8
        out = img.to_numpy()
        np.testing.assert_array_equal(out, arr)

    def test_gray16_round_trip(self):
        arr = np.random.randint(0, 65536, (50, 50), dtype=np.uint16)
        img = vxl.Image.from_numpy(arr)
        assert img.format == vxl.PixelFormat.GRAY16
        out = img.to_numpy()
        np.testing.assert_array_equal(out, arr)

    def test_from_numpy_bad_ndim(self):
        with pytest.raises(Exception):
            vxl.Image.from_numpy(np.zeros((5,), dtype=np.uint8))


# =============================================================================
# HeightMap <-> numpy round-trip
# =============================================================================

class TestHeightMapNumpy:
    """HeightMap / numpy interop."""

    def test_round_trip(self):
        arr = np.random.rand(128, 256).astype(np.float32)
        hm = vxl.HeightMap.from_numpy(arr)
        assert hm.width == 256
        assert hm.height == 128

        out = hm.to_numpy()
        np.testing.assert_allclose(out, arr)

    def test_create(self):
        hm = vxl.HeightMap.create(64, 32, 0.05)
        assert hm.width == 64
        assert hm.height == 32
        assert hm.resolution_mm == pytest.approx(0.05)

    def test_from_numpy_bad_ndim(self):
        with pytest.raises(Exception):
            vxl.HeightMap.from_numpy(np.zeros((5,), dtype=np.float32))


# =============================================================================
# ROI
# =============================================================================

class TestROI:

    def test_create(self):
        roi = vxl.ROI(10, 20, 100, 200)
        assert roi.x == 10
        assert roi.y == 20
        assert roi.w == 100
        assert roi.h == 200

    def test_area(self):
        roi = vxl.ROI(0, 0, 10, 20)
        assert roi.area() == 200

    def test_contains(self):
        roi = vxl.ROI(10, 10, 100, 100)
        assert roi.contains(50, 50)
        assert not roi.contains(0, 0)

    def test_repr(self):
        roi = vxl.ROI(1, 2, 3, 4)
        assert "ROI" in repr(roi)


# =============================================================================
# Error / exception hierarchy
# =============================================================================

class TestExceptions:

    def test_exception_hierarchy(self):
        """All specific exceptions should be catchable as VxlError."""
        assert issubclass(type(vxl.DeviceError("test", None)), BaseException)
        assert issubclass(type(vxl.CalibrationError("test", None)), BaseException)
        assert issubclass(type(vxl.ReconstructError("test", None)), BaseException)
        assert issubclass(type(vxl.InspectError("test", None)), BaseException)
        assert issubclass(type(vxl.ModelError("test", None)), BaseException)
        assert issubclass(type(vxl.IOError_("test", None)), BaseException)

    def test_error_code_to_string(self):
        s = vxl.error_code_to_string(vxl.ErrorCode.DEVICE_NOT_FOUND)
        assert isinstance(s, str)
        assert len(s) > 0


# =============================================================================
# Pose6D
# =============================================================================

class TestPose6D:

    def test_default(self):
        p = vxl.Pose6D()
        t = np.array(p.translation)
        np.testing.assert_array_equal(t, [0, 0, 0])


# =============================================================================
# CalibrationParams
# =============================================================================

class TestCalibrationParams:

    def test_default_sim(self):
        calib = vxl.CalibrationParams.default_sim()
        assert calib.image_width > 0

    def test_matrix_as_numpy(self):
        calib = vxl.CalibrationParams()
        mat = calib.camera_matrix
        assert mat.shape == (3, 3)


# =============================================================================
# ReconstructParams defaults
# =============================================================================

class TestReconstructParams:

    def test_defaults(self):
        p = vxl.ReconstructParams()
        assert p.method == "multi_frequency"
        assert p.phase_shift_steps == 4
        assert len(p.frequencies) == 3


# =============================================================================
# InspectorConfig / Inspector3D
# =============================================================================

class TestInspector:

    def test_config_fields(self):
        cfg = vxl.InspectorConfig()
        cfg.name = "test"
        cfg.type = "height_measure"
        cfg.severity = "warning"
        assert cfg.name == "test"

    def test_inspector3d_create(self):
        insp = vxl.Inspector3D()
        assert insp is not None

    def test_inspector3d_add_clear(self):
        insp = vxl.Inspector3D()
        cfg = vxl.InspectorConfig()
        cfg.name = "demo"
        cfg.type = "flatness"
        insp.add_inspector(cfg)
        insp.clear()


# =============================================================================
# Plane
# =============================================================================

class TestPlane:

    def test_distance(self):
        # z-plane at z=0: distance of (0,0,5) should be 5
        p = vxl.Plane(0, 0, 1, 0)
        assert p.distance(0, 0, 5) == pytest.approx(5.0)

    def test_repr(self):
        assert "Plane" in repr(vxl.Plane())
