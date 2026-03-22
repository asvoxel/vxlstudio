# Recipe Schema

Recipe 是检测方案的完整描述，包含参数、ROI、算子配置和模型引用。

## Phase 1 最小示例

Phase 1 仅使用 3D 基础算子（height_measure, flatness, height_threshold, ref_plane_fit, defect_cluster）：

```json
{
  "version": "1.0",
  "type": "pcb_smt",
  "name": "PCB Model A - 基础检测",
  "description": "焊点高度 + 平面度检测",
  "created": "2026-03-20T10:00:00Z",
  "modified": "2026-03-20T10:00:00Z",

  "camera": {
    "device_id": "SL-001",
    "calibration_file": "calib_sl001_20260320.json",
    "exposure_us": 5000,
    "fringe_count": 12,
    "hdr_exposures": [2000, 5000, 15000]
  },

  "reconstruct": {
    "method": "multi_frequency",
    "phase_shift_steps": 4,
    "frequencies": [1, 8, 64],
    "height_map_resolution_mm": 0.05,
    "min_modulation": 10.0,
    "filter": {
      "type": "median",
      "kernel_size": 3
    }
  },

  "reference": {
    "height_map_file": "ref_model_a.hmap",
    "ref_plane_roi": {"x": 100, "y": 100, "w": 800, "h": 600}
  },

  "inspectors_3d": [
    {
      "name": "焊点高度",
      "type": "height_measure",
      "rois": [
        {"id": "U1_pin1", "x": 200, "y": 150, "w": 30, "h": 30},
        {"id": "U1_pin2", "x": 240, "y": 150, "w": 30, "h": 30}
      ],
      "params": {
        "min_height_mm": 0.05,
        "max_height_mm": 0.35,
        "reference": "board_surface"
      }
    },
    {
      "name": "板面平面度",
      "type": "flatness",
      "rois": [
        {"id": "board_center", "x": 50, "y": 50, "w": 900, "h": 700}
      ],
      "params": {
        "max_flatness_mm": 0.20
      }
    },
    {
      "name": "异常凸起检测",
      "type": "height_threshold",
      "params": {
        "max_height_mm": 0.50,
        "min_defect_area_pixels": 20
      }
    }
  ],

  "inspectors_2d": [],

  "judgement": {
    "mode": "all_pass",
    "rules": [
      {"inspector": "焊点高度", "severity": "critical"},
      {"inspector": "板面平面度", "severity": "warning"},
      {"inspector": "异常凸起检测", "severity": "critical"}
    ]
  },

  "output": {
    "save_all_images": false,
    "save_ng_images": true,
    "save_height_map": false,
    "log_level": "INFO"
  }
}
```

## 完整示例（Phase 3+）

Phase 3 增加 coplanarity、template_compare 算子和 2D 检测通道后的完整 Recipe：

```json
{
  "version": "1.0",
  "type": "pcb_smt",
  "name": "PCB Model A - 正面全检",
  "description": "6层板正面焊点检测（3D + 2D 全检）",
  "created": "2026-03-20T10:00:00Z",
  "modified": "2026-03-20T15:30:00Z",

  "camera": {
    "device_id": "SL-001",
    "calibration_file": "calib_sl001_20260320.json",
    "exposure_us": 5000,
    "fringe_count": 12,
    "hdr_exposures": [2000, 5000, 15000]
  },

  "reconstruct": {
    "method": "multi_frequency",
    "phase_shift_steps": 4,
    "frequencies": [1, 8, 64],
    "height_map_resolution_mm": 0.05,
    "min_modulation": 10.0,
    "filter": {
      "type": "median",
      "kernel_size": 3
    }
  },

  "reference": {
    "height_map_file": "ref_model_a.hmap",
    "ref_plane_roi": {"x": 100, "y": 100, "w": 800, "h": 600}
  },

  "inspectors_3d": [
    {
      "name": "焊点高度",
      "type": "height_measure",
      "rois": [
        {"id": "U1_pin1", "x": 200, "y": 150, "w": 30, "h": 30},
        {"id": "U1_pin2", "x": 240, "y": 150, "w": 30, "h": 30}
      ],
      "params": {
        "min_height_mm": 0.05,
        "max_height_mm": 0.35,
        "reference": "board_surface"
      }
    },
    {
      "name": "元件共面性",
      "type": "coplanarity",
      "rois": [
        {"id": "U1_pins", "x": 180, "y": 130, "w": 120, "h": 80}
      ],
      "params": {
        "max_deviation_mm": 0.10
      }
    },
    {
      "name": "板面平面度",
      "type": "flatness",
      "rois": [
        {"id": "board_center", "x": 50, "y": 50, "w": 900, "h": 700}
      ],
      "params": {
        "max_flatness_mm": 0.20
      }
    },
    {
      "name": "表面缺陷",
      "type": "template_compare",
      "params": {
        "diff_threshold_mm": 0.08,
        "min_defect_area_mm2": 0.5
      }
    }
  ],

  "inspectors_2d": [
    {
      "name": "丝印识别",
      "type": "ocr",
      "rois": [
        {"id": "serial", "x": 50, "y": 20, "w": 200, "h": 40}
      ],
      "params": {
        "expected_pattern": "^SN[0-9]{10}$"
      }
    },
    {
      "name": "外观异常",
      "type": "anomaly_detect",
      "params": {
        "model_path": "models/anomaly/pcb_model_a.onnx",
        "threshold": 0.7
      }
    }
  ],

  "judgement": {
    "mode": "all_pass",
    "rules": [
      {"inspector": "焊点高度", "severity": "critical"},
      {"inspector": "元件共面性", "severity": "critical"},
      {"inspector": "板面平面度", "severity": "warning"},
      {"inspector": "表面缺陷", "severity": "critical"},
      {"inspector": "丝印识别", "severity": "warning"},
      {"inspector": "外观异常", "severity": "minor"}
    ]
  },

  "output": {
    "save_all_images": false,
    "save_ng_images": true,
    "save_height_map": false,
    "log_level": "INFO",
    "io_ok_pin": "Y0",
    "io_ng_pin": "Y1"
  }
}
```

## 字段说明

| 顶级字段 | 说明 |
|----------|------|
| `version` | Schema 版本号，用于兼容性检查 |
| `type` | 模板类型：`pcb_smt` / `flatness` / `surface_defect` / `custom` |
| `camera` | 相机配置和标定文件引用 |
| `reconstruct` | 重建参数（方法、相移步数、频率、分辨率） |
| `reference` | 参考数据（参考高度图、基准面 ROI） |
| `inspectors_3d` | 3D 检测算子列表，每个含 type、ROI、参数 |
| `inspectors_2d` | 2D 检测算子列表 |
| `judgement` | 综合判定规则（all_pass / weighted / custom） |
| `output` | 输出配置（保存策略、IO 引脚映射） |

### 3D 算子 type 值与可用阶段

| type | 说明 | 可用阶段 |
|------|------|----------|
| `ref_plane_fit` | 基准面拟合 | Phase 1 |
| `height_measure` | 高度测量 | Phase 1 |
| `flatness` | 平面度 | Phase 1 |
| `height_threshold` | 高度阈值分割 | Phase 1 |
| `defect_cluster` | 缺陷聚类 | Phase 1 |
| `coplanarity` | 共面性检查 | Phase 3 |
| `template_compare` | 模板比对 | Phase 3 |

### 2D 算子 type 值与可用阶段

| type | 说明 | 可用阶段 |
|------|------|----------|
| `template_match` | 模板匹配定位 | Phase 2 |
| `blob_analysis` | Blob 分析 | Phase 2 |
| `edge_detect` | 边缘检测 | Phase 2 |
| `ocr` | 丝印识别 | Phase 2 |
| `anomaly_detect` | 异常检测 | Phase 2 |

## 版本管理

- `version` 字段保证前向兼容：新版 Core 能加载旧版 Recipe
- Recipe 文件名建议带日期或版本号：`pcb_model_a_v3.json`
- 修改 Recipe 时自动更新 `modified` 时间戳
- Recipe 中引用了当前 Phase 不支持的算子时，Core 应返回明确错误（`ErrorCode::INVALID_PARAMETER`），而不是静默忽略

## Python 使用

```python
recipe = vxl.Recipe.load("pcb_model_a.json")
recipe.camera.exposure_us = 6000            # 修改参数
recipe.save("pcb_model_a_v2.json")          # 另存为
result = recipe.inspect(hmap)               # 执行检测
```
