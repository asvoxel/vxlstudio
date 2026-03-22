"""VxlStudio -- Python bindings for structured-light 3D inspection."""

from _vxl_core import *  # noqa: F401,F403
from _vxl_core import (
    # Error handling
    ErrorCode,
    error_code_to_string,
    VxlError,
    DeviceError,
    CalibrationError,
    ReconstructError,
    InspectError,
    ModelError,
    IOError_,
    set_error_callback,
    # Types
    SharedBuffer,
    PixelFormat,
    PointFormat,
    Point2f,
    Image,
    HeightMap,
    PointCloud,
    Mesh,
    ROI,
    Pose6D,
    DefectRegion,
    MeasureResult,
    InspectionResult,
    # Message bus
    Message,
    FrameCaptured,
    ReconstructDone,
    InspectionDone,
    ParamChanged,
    AlarmTriggered,
    DispatchMode,
    MessageBus,
    message_bus,
    # Camera
    ICamera,
    ICamera2D,
    ICamera3D,
    CameraManager,
    # Calibration
    CalibrationParams,
    # Reconstruct
    ReconstructParams,
    ReconstructOutput,
    Reconstruct,
    reconstruct,
    # Compute backend
    ComputeBackend,
    set_compute_backend,
    get_compute_backend,
    available_backends,
    # Height map processing
    Plane,
    HeightMapProcessor,
    # Inspector
    InspectorConfig,
    InspectorResult,
    Inspector3D,
    CompareResult,
    build_inspection_result,
    # Point cloud
    PointCloudOps,
    # Recipe
    Recipe,
    # Inspector 2D
    Inspector2D,
    MatchResult,
    BlobResult,
    OcrResult,
    # Inference
    Inference,
    InferenceParams,
    # Pipeline
    StepType,
    PipelineStep,
    PipelineContext,
    Pipeline,
    # IO
    IIO,
    ITrigger,
    IOManager,
    # Audit & permissions
    Role,
    User,
    AuditEntry,
    UserManager,
    AuditLog,
    user_manager,
    audit_log,
    # Plugin system
    PluginInfo,
    PluginManager,
    plugin_manager,
    # VLM
    VLMConfig,
    VLMResponse,
    VLMAssistant,
    vlm_assistant,
    # Transport
    TransportMessage,
    TransportServer,
    TransportClient,
    # Guidance
    GraspPose,
    GuidanceParams,
    GuidanceEngine,
    # Visual AI
    AnomalyResult,
    ClassifyResult,
    Detection,
    DetectionResult,
    SegmentResult,
    IAnomalyDetector,
    IClassifier,
    IDetector,
    ISegmenter,
    AnomalibDetector,
    YoloClassifier,
    YoloDetector,
    YoloSegmenter,
    ModelRegistry,
    model_registry,
)

# Submodules re-exported at package level for convenience.
from _vxl_core import Camera  # noqa: F401  (factory namespace)
from _vxl_core import log     # noqa: F401  (logging submodule)

from vxl.vlm_utils import setup_vlm, vlm_query  # noqa: F401

__version__ = "0.1.0"
__all__ = [
    # Version
    "__version__",
    # Error
    "ErrorCode", "error_code_to_string",
    "VxlError", "DeviceError", "CalibrationError", "ReconstructError",
    "InspectError", "ModelError", "IOError_", "set_error_callback",
    # Types
    "SharedBuffer", "PixelFormat", "PointFormat", "Point2f",
    "Image", "HeightMap", "PointCloud", "Mesh",
    "ROI", "Pose6D", "DefectRegion", "MeasureResult", "InspectionResult",
    # Message bus
    "Message", "FrameCaptured", "ReconstructDone", "InspectionDone",
    "ParamChanged", "AlarmTriggered", "DispatchMode", "MessageBus", "message_bus",
    # Camera
    "ICamera", "ICamera2D", "ICamera3D", "CameraManager", "Camera",
    # Calibration
    "CalibrationParams",
    # Reconstruct
    "ReconstructParams", "ReconstructOutput", "Reconstruct", "reconstruct",
    # Compute backend
    "ComputeBackend", "set_compute_backend", "get_compute_backend",
    "available_backends",
    # Height map
    "Plane", "HeightMapProcessor",
    # Inspector
    "InspectorConfig", "InspectorResult", "Inspector3D", "CompareResult",
    "build_inspection_result",
    # Point cloud
    "PointCloudOps",
    # Recipe
    "Recipe",
    # Inspector 2D
    "Inspector2D", "MatchResult", "BlobResult", "OcrResult",
    # Inference
    "Inference", "InferenceParams",
    # Pipeline
    "StepType", "PipelineStep", "PipelineContext", "Pipeline",
    # IO
    "IIO", "ITrigger", "IOManager",
    # Audit & permissions
    "Role", "User", "AuditEntry", "UserManager", "AuditLog",
    "user_manager", "audit_log",
    # Plugin system
    "PluginInfo", "PluginManager", "plugin_manager",
    # VLM
    "VLMConfig", "VLMResponse", "VLMAssistant", "vlm_assistant",
    "setup_vlm", "vlm_query",
    # Transport
    "TransportMessage", "TransportServer", "TransportClient",
    # Guidance
    "GraspPose", "GuidanceParams", "GuidanceEngine",
    # Visual AI
    "AnomalyResult", "ClassifyResult", "Detection", "DetectionResult",
    "SegmentResult",
    "IAnomalyDetector", "IClassifier", "IDetector", "ISegmenter",
    "AnomalibDetector", "YoloClassifier", "YoloDetector", "YoloSegmenter",
    "ModelRegistry", "model_registry",
    # Submodules
    "log",
]
