"""CalibrationWizard -- 4-step stereo/single calibration dialog."""

import os
from pathlib import Path

from PySide6.QtCore import Qt, QThread, Signal, QTimer
from PySide6.QtGui import QImage, QPixmap, QColor, QPainter
from PySide6.QtWidgets import (
    QDialog, QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QPushButton, QSpinBox, QDoubleSpinBox,
    QComboBox, QProgressBar, QFileDialog, QMessageBox,
    QStackedWidget, QWidget, QGroupBox, QSizePolicy,
)


def _try_import_vxl():
    """Import vxl, returning None if unavailable."""
    try:
        import vxl
        return vxl
    except ImportError:
        return None


# ---------------------------------------------------------------------------
# Helper: convert vxl.Image (BGR8) to QPixmap for display
# ---------------------------------------------------------------------------
def _image_to_pixmap(vxl_image, max_w=480, max_h=360):
    """Convert a vxl.Image to a QPixmap, scaling to fit *max_w* x *max_h*."""
    vxl = _try_import_vxl()
    if vxl is None or vxl_image is None:
        pm = QPixmap(max_w, max_h)
        pm.fill(QColor(40, 40, 40))
        return pm

    import numpy as np

    w, h = vxl_image.width, vxl_image.height
    fmt = vxl_image.format

    # Use the buffer protocol / to_numpy() to get pixel data
    arr = np.ascontiguousarray(np.array(vxl_image, copy=True))
    stride = arr.strides[0]  # bytes per row

    if fmt == vxl.PixelFormat.BGR8:
        qimg = QImage(arr.data, w, h, stride, QImage.Format_BGR888)
    elif fmt == vxl.PixelFormat.RGB8:
        qimg = QImage(arr.data, w, h, stride, QImage.Format_RGB888)
    elif fmt == vxl.PixelFormat.GRAY8:
        qimg = QImage(arr.data, w, h, stride, QImage.Format_Grayscale8)
    else:
        pm = QPixmap(max_w, max_h)
        pm.fill(QColor(40, 40, 40))
        return pm

    pm = QPixmap.fromImage(qimg.copy())
    return pm.scaled(max_w, max_h, Qt.KeepAspectRatio, Qt.SmoothTransformation)


# ---------------------------------------------------------------------------
# Worker thread for calibration (heavy computation)
# ---------------------------------------------------------------------------
class _CalibrationWorker(QThread):
    finished = Signal(object)  # CalibrationResult or str (error message)

    def __init__(self, calibrator, stereo=True, parent=None):
        super().__init__(parent)
        self._calibrator = calibrator
        self._stereo = stereo

    def run(self):
        try:
            if self._stereo:
                result = self._calibrator.calibrate_stereo()
            else:
                result = self._calibrator.calibrate_single()
            self.finished.emit(result)
        except Exception as e:
            self.finished.emit(str(e))


# ---------------------------------------------------------------------------
# Step widgets
# ---------------------------------------------------------------------------

class _SetupPage(QWidget):
    """Step 1 -- board params, calibration type, camera selection."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)

        # Board params
        board_group = QGroupBox("棋盘格参数")
        bg_layout = QGridLayout(board_group)

        bg_layout.addWidget(QLabel("列数 (cols):"), 0, 0)
        self.spin_cols = QSpinBox()
        self.spin_cols.setRange(3, 30)
        self.spin_cols.setValue(9)
        bg_layout.addWidget(self.spin_cols, 0, 1)

        bg_layout.addWidget(QLabel("行数 (rows):"), 1, 0)
        self.spin_rows = QSpinBox()
        self.spin_rows.setRange(3, 30)
        self.spin_rows.setValue(6)
        bg_layout.addWidget(self.spin_rows, 1, 1)

        bg_layout.addWidget(QLabel("方格尺寸 (mm):"), 2, 0)
        self.spin_size = QDoubleSpinBox()
        self.spin_size.setRange(1.0, 200.0)
        self.spin_size.setValue(25.0)
        self.spin_size.setDecimals(2)
        bg_layout.addWidget(self.spin_size, 2, 1)

        layout.addWidget(board_group)

        # Calibration type
        type_group = QGroupBox("标定类型")
        tg_layout = QHBoxLayout(type_group)
        tg_layout.addWidget(QLabel("类型:"))
        self.combo_type = QComboBox()
        self.combo_type.addItems(["双目标定", "单目标定"])
        tg_layout.addWidget(self.combo_type)
        layout.addWidget(type_group)

        # Camera selection
        cam_group = QGroupBox("相机选择")
        cg_layout = QHBoxLayout(cam_group)
        cg_layout.addWidget(QLabel("相机:"))
        self.combo_camera = QComboBox()
        self._populate_cameras()
        cg_layout.addWidget(self.combo_camera, 1)
        self.btn_connect = QPushButton("连接相机")
        cg_layout.addWidget(self.btn_connect)
        layout.addWidget(cam_group)

        # Offline note
        note = QLabel("提示: 如果没有相机，可以在下一步通过文件加载图片进行离线标定。")
        note.setWordWrap(True)
        note.setStyleSheet("color: #888;")
        layout.addWidget(note)

        layout.addStretch()

    def _populate_cameras(self):
        self.combo_camera.clear()
        self.combo_camera.addItem("(无相机 -- 离线模式)")
        vxl = _try_import_vxl()
        if vxl is not None:
            try:
                devices = vxl.Camera.enumerate()
                for d in devices:
                    self.combo_camera.addItem(d)
            except Exception:
                pass

    def is_stereo(self):
        return self.combo_type.currentIndex() == 0


class _CapturePage(QWidget):
    """Step 2 -- live preview + capture."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)

        # Preview area
        preview_layout = QHBoxLayout()
        self.lbl_left = QLabel()
        self.lbl_left.setFixedSize(480, 360)
        self.lbl_left.setAlignment(Qt.AlignCenter)
        self.lbl_left.setStyleSheet("background: #222; border: 1px solid #555;")
        self.lbl_left.setText("左相机")
        preview_layout.addWidget(self.lbl_left)

        self.lbl_right = QLabel()
        self.lbl_right.setFixedSize(480, 360)
        self.lbl_right.setAlignment(Qt.AlignCenter)
        self.lbl_right.setStyleSheet("background: #222; border: 1px solid #555;")
        self.lbl_right.setText("右相机")
        preview_layout.addWidget(self.lbl_right)
        layout.addLayout(preview_layout)

        # Status
        status_layout = QHBoxLayout()
        self.lbl_status = QLabel("已采集: 0/15 组")
        status_layout.addWidget(self.lbl_status)
        self.progress = QProgressBar()
        self.progress.setRange(0, 15)
        self.progress.setValue(0)
        status_layout.addWidget(self.progress, 1)
        layout.addLayout(status_layout)

        # Buttons
        btn_layout = QHBoxLayout()
        self.btn_capture = QPushButton("拍照")
        self.btn_capture.setMinimumHeight(40)
        btn_layout.addWidget(self.btn_capture)

        self.btn_load_files = QPushButton("从文件加载...")
        btn_layout.addWidget(self.btn_load_files)

        self.btn_delete_last = QPushButton("删除上一张")
        btn_layout.addWidget(self.btn_delete_last)
        layout.addLayout(btn_layout)

        # Tips
        tips = QLabel(
            "请从不同角度、距离拍摄棋盘格，确保棋盘占画面 30-80%。\n"
            "绿色边框: 角点检测成功  红色边框: 角点检测失败"
        )
        tips.setWordWrap(True)
        tips.setStyleSheet("color: #aaa; font-size: 11px; margin-top: 8px;")
        layout.addWidget(tips)

    def set_stereo_mode(self, stereo: bool):
        self.lbl_right.setVisible(stereo)
        if stereo:
            self.lbl_left.setText("左相机")
        else:
            self.lbl_left.setText("相机预览")

    def update_count(self, count: int, target: int):
        self.lbl_status.setText(f"已采集: {count}/{target} 组")
        self.progress.setRange(0, target)
        self.progress.setValue(count)

    def flash_border(self, label: QLabel, success: bool):
        color = "#0f0" if success else "#f00"
        label.setStyleSheet(f"background: #222; border: 3px solid {color};")
        QTimer.singleShot(500, lambda: label.setStyleSheet(
            "background: #222; border: 1px solid #555;"))


class _CalibratePage(QWidget):
    """Step 3 -- run calibration."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.addStretch()

        self.btn_calibrate = QPushButton("开始标定")
        self.btn_calibrate.setMinimumHeight(50)
        self.btn_calibrate.setStyleSheet("font-size: 16px;")
        layout.addWidget(self.btn_calibrate, alignment=Qt.AlignCenter)

        self.lbl_progress = QLabel("")
        self.lbl_progress.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.lbl_progress)

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 0)  # indeterminate
        self.progress_bar.setVisible(False)
        layout.addWidget(self.progress_bar)

        self.lbl_result = QLabel("")
        self.lbl_result.setAlignment(Qt.AlignCenter)
        self.lbl_result.setStyleSheet("font-size: 18px; margin-top: 20px;")
        layout.addWidget(self.lbl_result)

        self.lbl_detail = QLabel("")
        self.lbl_detail.setAlignment(Qt.AlignCenter)
        self.lbl_detail.setWordWrap(True)
        layout.addWidget(self.lbl_detail)

        layout.addStretch()

    def show_running(self):
        self.btn_calibrate.setEnabled(False)
        self.lbl_progress.setText("正在计算...")
        self.progress_bar.setVisible(True)
        self.lbl_result.setText("")
        self.lbl_detail.setText("")

    def show_result(self, rms: float, pairs: int):
        self.progress_bar.setVisible(False)
        self.btn_calibrate.setEnabled(True)
        self.lbl_progress.setText("")

        if rms < 0.5:
            quality = "良好"
            color = "#0c0"
        elif rms < 1.0:
            quality = "一般"
            color = "#cc0"
        else:
            quality = "较差"
            color = "#c00"

        self.lbl_result.setText(
            f'<span style="color:{color}; font-size:20px;">'
            f'重投影误差: {rms:.4f} 像素 ({quality})</span>'
        )
        self.lbl_detail.setText(f"使用了 {pairs} 组图像")

    def show_error(self, msg: str):
        self.progress_bar.setVisible(False)
        self.btn_calibrate.setEnabled(True)
        self.lbl_progress.setText("")
        self.lbl_result.setText(
            f'<span style="color:#c00;">标定失败</span>'
        )
        self.lbl_detail.setText(msg)


class _SavePage(QWidget):
    """Step 4 -- save results."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.addStretch()

        # File path
        path_layout = QHBoxLayout()
        path_layout.addWidget(QLabel("保存路径:"))
        self.lbl_path = QLabel("(未选择)")
        self.lbl_path.setStyleSheet("color: #aaa;")
        path_layout.addWidget(self.lbl_path, 1)
        self.btn_browse = QPushButton("浏览...")
        path_layout.addWidget(self.btn_browse)
        layout.addLayout(path_layout)

        # Buttons
        btn_layout = QHBoxLayout()
        self.btn_save = QPushButton("保存标定文件")
        self.btn_save.setMinimumHeight(40)
        btn_layout.addWidget(self.btn_save)

        self.btn_test = QPushButton("测试效果")
        self.btn_test.setMinimumHeight(40)
        btn_layout.addWidget(self.btn_test)

        self.btn_finish = QPushButton("完成")
        self.btn_finish.setMinimumHeight(40)
        btn_layout.addWidget(self.btn_finish)
        layout.addLayout(btn_layout)

        # Preview area for rectified images
        self.lbl_preview = QLabel()
        self.lbl_preview.setFixedHeight(300)
        self.lbl_preview.setAlignment(Qt.AlignCenter)
        self.lbl_preview.setStyleSheet("background: #222; border: 1px solid #555;")
        self.lbl_preview.setText("保存后可点击「测试效果」查看矫正图像")
        layout.addWidget(self.lbl_preview)

        layout.addStretch()


# ---------------------------------------------------------------------------
# CalibrationWizard (main dialog)
# ---------------------------------------------------------------------------
class CalibrationWizard(QDialog):
    """4-step calibration wizard dialog."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("标定向导")
        self.resize(1060, 640)

        self._vxl = _try_import_vxl()
        self._calibrator = None
        self._camera = None
        self._calib_result = None
        self._worker = None

        # Stored images for offline / file-based workflow
        self._left_images = []
        self._right_images = []
        self._single_images = []

        self._build_ui()
        self._connect_signals()
        self._update_nav()

    # -----------------------------------------------------------------
    # UI construction
    # -----------------------------------------------------------------

    def _build_ui(self):
        main_layout = QVBoxLayout(self)

        # Step labels
        self._step_labels = []
        step_bar = QHBoxLayout()
        for i, name in enumerate(["1. 设置", "2. 采集", "3. 标定", "4. 保存"]):
            lbl = QLabel(name)
            lbl.setAlignment(Qt.AlignCenter)
            lbl.setStyleSheet("padding: 6px; font-weight: bold;")
            step_bar.addWidget(lbl)
            self._step_labels.append(lbl)
        main_layout.addLayout(step_bar)

        # Stacked pages
        self._stack = QStackedWidget()
        self._page_setup     = _SetupPage()
        self._page_capture   = _CapturePage()
        self._page_calibrate = _CalibratePage()
        self._page_save      = _SavePage()

        self._stack.addWidget(self._page_setup)
        self._stack.addWidget(self._page_capture)
        self._stack.addWidget(self._page_calibrate)
        self._stack.addWidget(self._page_save)
        main_layout.addWidget(self._stack, 1)

        # Navigation buttons
        nav = QHBoxLayout()
        self._btn_prev = QPushButton("上一步")
        self._btn_next = QPushButton("下一步")
        nav.addStretch()
        nav.addWidget(self._btn_prev)
        nav.addWidget(self._btn_next)
        main_layout.addLayout(nav)

    def _connect_signals(self):
        self._btn_prev.clicked.connect(self._go_prev)
        self._btn_next.clicked.connect(self._go_next)

        # Setup page
        self._page_setup.btn_connect.clicked.connect(self._connect_camera)

        # Capture page
        self._page_capture.btn_capture.clicked.connect(self._capture)
        self._page_capture.btn_load_files.clicked.connect(self._load_files)
        self._page_capture.btn_delete_last.clicked.connect(self._delete_last)

        # Calibrate page
        self._page_calibrate.btn_calibrate.clicked.connect(self._run_calibration)

        # Save page
        self._page_save.btn_browse.clicked.connect(self._browse_save_path)
        self._page_save.btn_save.clicked.connect(self._save_calibration)
        self._page_save.btn_test.clicked.connect(self._test_effect)
        self._page_save.btn_finish.clicked.connect(self.accept)

    # -----------------------------------------------------------------
    # Navigation
    # -----------------------------------------------------------------

    def _go_prev(self):
        idx = self._stack.currentIndex()
        if idx > 0:
            self._stack.setCurrentIndex(idx - 1)
            self._update_nav()

    def _go_next(self):
        idx = self._stack.currentIndex()
        if idx == 0:
            # Moving from Setup -> Capture: init calibrator
            self._init_calibrator()
        if idx < self._stack.count() - 1:
            self._stack.setCurrentIndex(idx + 1)
            self._update_nav()

    def _update_nav(self):
        idx = self._stack.currentIndex()
        self._btn_prev.setEnabled(idx > 0)
        self._btn_next.setEnabled(idx < self._stack.count() - 1)
        if idx == self._stack.count() - 1:
            self._btn_next.setText("完成")
        else:
            self._btn_next.setText("下一步")

        # Highlight current step label
        for i, lbl in enumerate(self._step_labels):
            if i == idx:
                lbl.setStyleSheet(
                    "padding: 6px; font-weight: bold; "
                    "background: #335; color: white; border-radius: 4px;")
            else:
                lbl.setStyleSheet("padding: 6px; font-weight: bold;")

    # -----------------------------------------------------------------
    # Step 1 -- Setup
    # -----------------------------------------------------------------

    def _init_calibrator(self):
        if self._vxl is None:
            return
        self._calibrator = self._vxl.StereoCalibrator()
        board = self._vxl.BoardParams()
        board.cols = self._page_setup.spin_cols.value()
        board.rows = self._page_setup.spin_rows.value()
        board.square_size_mm = self._page_setup.spin_size.value()
        self._calibrator.set_board(board)

        stereo = self._page_setup.is_stereo()
        self._page_capture.set_stereo_mode(stereo)
        target = 10 if stereo else 15
        count = self._calibrator.pair_count() if stereo else self._calibrator.image_count()
        self._page_capture.update_count(count, target)

    def _connect_camera(self):
        if self._vxl is None:
            QMessageBox.warning(self, "错误", "vxl 模块不可用")
            return
        idx = self._page_setup.combo_camera.currentIndex()
        if idx == 0:
            QMessageBox.information(self, "提示", "离线模式下请使用文件加载图片。")
            return
        device_id = self._page_setup.combo_camera.currentText()
        try:
            self._camera = self._vxl.Camera.open_2d(device_id)
            QMessageBox.information(self, "成功", f"已连接: {device_id}")
        except Exception as e:
            QMessageBox.critical(self, "连接失败", str(e))

    # -----------------------------------------------------------------
    # Step 2 -- Capture
    # -----------------------------------------------------------------

    def _capture(self):
        """Capture from live camera."""
        if self._camera is None:
            QMessageBox.warning(self, "提示", "未连接相机，请使用文件加载。")
            return
        if self._calibrator is None:
            QMessageBox.warning(self, "提示", "请先完成设置步骤。")
            return

        try:
            left_img = self._camera.capture()
            stereo = self._page_setup.is_stereo()

            if stereo:
                # For a real stereo rig you'd capture from a second camera.
                # Here we assume the camera returns a side-by-side image or
                # the user loads files instead.
                QMessageBox.information(
                    self, "提示",
                    "实时双目采集需要两个相机。请使用「从文件加载」功能。")
                return
            else:
                found = self._calibrator.add_image(left_img)
                self._page_capture.flash_border(
                    self._page_capture.lbl_left, found)

                if found:
                    board = self._vxl.BoardParams()
                    board.cols = self._page_setup.spin_cols.value()
                    board.rows = self._page_setup.spin_rows.value()
                    board.square_size_mm = self._page_setup.spin_size.value()
                    annotated = self._vxl.StereoCalibrator.detect_and_draw_corners(
                        left_img, board)
                    pm = _image_to_pixmap(annotated, 480, 360)
                    self._page_capture.lbl_left.setPixmap(pm)

                count = self._calibrator.image_count()
                self._page_capture.update_count(count, 15)

        except Exception as e:
            QMessageBox.critical(self, "采集失败", str(e))

    def _load_files(self):
        """Load images from files for offline calibration."""
        if self._vxl is None:
            QMessageBox.warning(self, "错误", "vxl 模块不可用")
            return
        if self._calibrator is None:
            self._init_calibrator()
        if self._calibrator is None:
            QMessageBox.warning(self, "错误", "无法创建标定器 (vxl 不可用)")
            return

        stereo = self._page_setup.is_stereo()

        if stereo:
            QMessageBox.information(
                self, "双目标定",
                "请先选择左相机图片，再选择右相机图片。\n"
                "两组图片数量必须相同，且按顺序一一对应。")

            left_files, _ = QFileDialog.getOpenFileNames(
                self, "选择左相机图片", "",
                "Images (*.png *.jpg *.bmp *.tiff);;All Files (*)")
            if not left_files:
                return

            right_files, _ = QFileDialog.getOpenFileNames(
                self, "选择右相机图片", "",
                "Images (*.png *.jpg *.bmp *.tiff);;All Files (*)")
            if not right_files:
                return

            if len(left_files) != len(right_files):
                QMessageBox.warning(
                    self, "错误",
                    f"左右图片数量不匹配: {len(left_files)} vs {len(right_files)}")
                return

            import cv2
            added = 0
            for lf, rf in zip(sorted(left_files), sorted(right_files)):
                mat_l = cv2.imread(lf)
                mat_r = cv2.imread(rf)
                if mat_l is None or mat_r is None:
                    continue
                img_l = self._vxl.Image.from_cv_mat(mat_l)
                img_r = self._vxl.Image.from_cv_mat(mat_r)
                found = self._calibrator.add_image_pair(img_l, img_r)
                if found:
                    added += 1
                    self._left_images.append(img_l)
                    self._right_images.append(img_r)

            count = self._calibrator.pair_count()
            self._page_capture.update_count(count, 10)
            QMessageBox.information(
                self, "完成",
                f"成功添加 {added} 组图像对 (共 {len(left_files)} 组)")

            # Show last added pair with corners
            if self._left_images:
                board = self._vxl.BoardParams()
                board.cols = self._page_setup.spin_cols.value()
                board.rows = self._page_setup.spin_rows.value()
                board.square_size_mm = self._page_setup.spin_size.value()
                try:
                    ann_l = self._vxl.StereoCalibrator.detect_and_draw_corners(
                        self._left_images[-1], board)
                    self._page_capture.lbl_left.setPixmap(
                        _image_to_pixmap(ann_l, 480, 360))
                    ann_r = self._vxl.StereoCalibrator.detect_and_draw_corners(
                        self._right_images[-1], board)
                    self._page_capture.lbl_right.setPixmap(
                        _image_to_pixmap(ann_r, 480, 360))
                except Exception:
                    pass

        else:
            files, _ = QFileDialog.getOpenFileNames(
                self, "选择标定图片", "",
                "Images (*.png *.jpg *.bmp *.tiff);;All Files (*)")
            if not files:
                return

            import cv2
            added = 0
            for f in sorted(files):
                mat = cv2.imread(f)
                if mat is None:
                    continue
                img = self._vxl.Image.from_cv_mat(mat)
                found = self._calibrator.add_image(img)
                if found:
                    added += 1
                    self._single_images.append(img)

            count = self._calibrator.image_count()
            self._page_capture.update_count(count, 15)
            QMessageBox.information(
                self, "完成",
                f"成功添加 {added} 张图像 (共 {len(files)} 张)")

            # Show last with corners
            if self._single_images:
                board = self._vxl.BoardParams()
                board.cols = self._page_setup.spin_cols.value()
                board.rows = self._page_setup.spin_rows.value()
                board.square_size_mm = self._page_setup.spin_size.value()
                try:
                    ann = self._vxl.StereoCalibrator.detect_and_draw_corners(
                        self._single_images[-1], board)
                    self._page_capture.lbl_left.setPixmap(
                        _image_to_pixmap(ann, 480, 360))
                except Exception:
                    pass

    def _delete_last(self):
        """Remove the last captured/loaded set and reset calibrator."""
        if self._calibrator is None:
            return

        stereo = self._page_setup.is_stereo()

        if stereo:
            if self._left_images:
                self._left_images.pop()
                self._right_images.pop()
            # Rebuild calibrator with remaining images
            self._rebuild_calibrator()
            count = self._calibrator.pair_count()
            self._page_capture.update_count(count, 10)
        else:
            if self._single_images:
                self._single_images.pop()
            self._rebuild_calibrator()
            count = self._calibrator.image_count()
            self._page_capture.update_count(count, 15)

    def _rebuild_calibrator(self):
        """Clear and re-add all stored images."""
        if self._calibrator is None:
            return
        self._calibrator.clear()
        stereo = self._page_setup.is_stereo()
        if stereo:
            for l, r in zip(self._left_images, self._right_images):
                self._calibrator.add_image_pair(l, r)
        else:
            for img in self._single_images:
                self._calibrator.add_image(img)

    # -----------------------------------------------------------------
    # Step 3 -- Calibrate
    # -----------------------------------------------------------------

    def _run_calibration(self):
        if self._calibrator is None:
            QMessageBox.warning(self, "错误", "标定器未初始化")
            return

        stereo = self._page_setup.is_stereo()
        count = (self._calibrator.pair_count() if stereo
                 else self._calibrator.image_count())
        if count < 3:
            QMessageBox.warning(
                self, "数据不足",
                f"至少需要 3 组图像，当前只有 {count} 组。")
            return

        self._page_calibrate.show_running()

        self._worker = _CalibrationWorker(
            self._calibrator, stereo=stereo, parent=self)
        self._worker.finished.connect(self._on_calibration_done)
        self._worker.start()

    def _on_calibration_done(self, result):
        self._worker = None
        if isinstance(result, str):
            self._page_calibrate.show_error(result)
            return

        self._calib_result = result
        self._page_calibrate.show_result(
            result.reprojection_error, result.image_pairs_used)

    # -----------------------------------------------------------------
    # Step 4 -- Save
    # -----------------------------------------------------------------

    def _browse_save_path(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "保存标定文件", "calibration.json",
            "JSON Files (*.json);;All Files (*)")
        if path:
            self._page_save.lbl_path.setText(path)
            self._page_save.lbl_path.setStyleSheet("color: white;")

    def _save_calibration(self):
        if self._calib_result is None:
            QMessageBox.warning(self, "错误", "尚未完成标定")
            return

        path = self._page_save.lbl_path.text()
        if path == "(未选择)":
            QMessageBox.warning(self, "错误", "请先选择保存路径")
            return

        try:
            self._calib_result.params.save(path)
            QMessageBox.information(self, "成功", f"标定文件已保存到:\n{path}")
        except Exception as e:
            QMessageBox.critical(self, "保存失败", str(e))

    def _test_effect(self):
        """Show rectified images side-by-side (stereo only)."""
        if self._calib_result is None:
            QMessageBox.warning(self, "错误", "尚未完成标定")
            return

        if not self._page_setup.is_stereo():
            QMessageBox.information(
                self, "提示", "单目标定无法展示矫正效果对比。")
            return

        if not self._left_images or not self._right_images:
            QMessageBox.warning(self, "错误", "没有可用的图像数据")
            return

        try:
            import cv2
            import numpy as np

            p = self._calib_result.params

            cam_l = np.array(p.camera_matrix).reshape(3, 3)
            dist_l = np.array(p.camera_distortion)
            cam_r = np.array(p.projector_matrix).reshape(3, 3)
            dist_r = np.array(p.projector_distortion)
            R = np.array(p.rotation).reshape(3, 3)
            T = np.array(p.translation)

            img_size = (p.image_width, p.image_height)

            R1, R2, P1, P2, Q, _, _ = cv2.stereoRectify(
                cam_l, dist_l, cam_r, dist_r, img_size, R, T)

            map1x, map1y = cv2.initUndistortRectifyMap(
                cam_l, dist_l, R1, P1, img_size, cv2.CV_32FC1)
            map2x, map2y = cv2.initUndistortRectifyMap(
                cam_r, dist_r, R2, P2, img_size, cv2.CV_32FC1)

            # Use last image pair
            mat_l = self._left_images[-1].to_cv_mat() if hasattr(
                self._left_images[-1], 'to_cv_mat') else None
            mat_r = self._right_images[-1].to_cv_mat() if hasattr(
                self._right_images[-1], 'to_cv_mat') else None

            if mat_l is None or mat_r is None:
                # Fallback: try via buffer
                QMessageBox.information(self, "提示", "无法获取图像矩阵")
                return

            rect_l = cv2.remap(mat_l, map1x, map1y, cv2.INTER_LINEAR)
            rect_r = cv2.remap(mat_r, map2x, map2y, cv2.INTER_LINEAR)

            # Draw horizontal lines for epipolar check
            h = rect_l.shape[0]
            for y in range(0, h, h // 16):
                cv2.line(rect_l, (0, y), (rect_l.shape[1], y), (0, 255, 0), 1)
                cv2.line(rect_r, (0, y), (rect_r.shape[1], y), (0, 255, 0), 1)

            combined = np.hstack([rect_l, rect_r])
            # Convert to QPixmap
            if combined.ndim == 3:
                h, w, ch = combined.shape
                qimg = QImage(combined.data, w, h, w * ch, QImage.Format_BGR888)
            else:
                h, w = combined.shape
                qimg = QImage(combined.data, w, h, w, QImage.Format_Grayscale8)

            pm = QPixmap.fromImage(qimg).scaled(
                960, 300, Qt.KeepAspectRatio, Qt.SmoothTransformation)
            self._page_save.lbl_preview.setPixmap(pm)

        except Exception as e:
            QMessageBox.critical(self, "测试失败", str(e))
