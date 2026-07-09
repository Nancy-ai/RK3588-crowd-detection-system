```markdown
# 边缘智能监控系统

基于 RK3588 边缘计算平台的实时视频监控与智能分析系统，面向教室、楼道、图书馆入口等场景，实现人员检测、多目标跟踪、ROI 区域分析、长时间停留识别、实时视频推流和后台可视化展示。

## 项目简介
本项目在 RK3588 端侧部署 YOLO 检测模型，通过 OV13855 MIPI-CSI 摄像头采集视频流，使用 RGA 完成图像预处理，调用 RKNN NPU 进行目标检测，并结合 ByteTrack 实现跨帧目标跟踪。系统支持 ROI 区域人数统计、长时间停留事件识别、系统状态监控、JSON 数据上报和 RTSP/WebRTC 视频展示。

整体流程为：

```text
MIPI 摄像头
  -> V4L2 采集
  -> RGA 图像预处理
  -> RKNN YOLO 推理
  -> YOLO 后处理
  -> ByteTrack 跟踪
  -> ROI / 事件分析
  -> RTSP 视频推流 + HTTP 数据上报
  -> Web 后台展示
```

## 已实现功能

- 实时视频采集：基于 V4L2 从 OV13855 摄像头获取 NV12 数据。
- 硬件图像加速：使用 RGA 完成 NV12 到 RGB、缩放和模型输入预处理。
- NPU 推理：基于 RKNN Toolkit 部署 YOLO 模型到 RK3588 NPU。
- 目标检测：支持人员目标检测和检测框绘制。
- 多目标跟踪：集成 ByteTrack，为目标分配稳定的 Track ID。
- ROI 区域分析：支持自定义监控区域并统计区域内人数。
- 长时间停留识别：根据 Track ID 的停留时间触发异常事件。
- 系统状态监控：统计 FPS、CPU 温度、CPU/GPU/NPU 占用等信息。
- 数据上报：通过 HTTP JSON 将检测结果和系统状态上传到后台。
- 视频展示：通过 RTSP/MediaMTX/WebRTC 在浏览器端查看实时画面。

## 项目结构

```text
.
├── include/                 # 头文件
│   ├── camera.h             # 摄像头采集接口
│   ├── RGA_processor.h      # RGA 预处理接口
│   ├── detector.h           # RKNN 推理接口
│   ├── post_processor.h     # YOLO 后处理结构与接口
│   ├── tracker_adapter.h    # ByteTrack 适配层
│   ├── event_analysis.h     # ROI 和事件分析
│   ├── reporter.h           # JSON 数据上报
│   └── rtsp_streamer.h      # RTSP 推流
├── src/
│   ├── main.cpp             # 主流程
│   ├── camera.c             # V4L2 摄像头采集
│   ├── RGA_processor.c      # RGA 图像预处理
│   ├── detector.c           # RKNN 初始化与推理
│   ├── post_processor.c     # YOLO 输出后处理
│   ├── tracker_adapter.cpp  # ByteTrack 调用封装
│   ├── event_analysis.cpp   # ROI / 长时间停留分析
│   ├── reporter.c           # HTTP 数据上报
│   └── rtsp_streamer.cpp    # RTSP 视频流输出
├── model/                   # RKNN 模型文件
├── third_party/             # 第三方库和头文件
├── CMakeLists.txt
└── README.md
```

## 运行环境

### 硬件环境

- RK3588 / ELF2 开发板
- OV13855 MIPI-CSI 摄像头
- MIPI 显示屏或 HDMI 显示设备
- PC 端浏览器用于后台展示

### 软件环境

板端：

- Ubuntu 22.04 / Linux aarch64
- CMake
- OpenCV
- GStreamer
- RKNN Runtime
- RGA Runtime
- Eigen3

PC 端：

- Python 3
- FastAPI
- Uvicorn
- MediaMTX
- 浏览器

## 编译方法

在 RK3588 板端执行：

```bash
cd yolov8_project
mkdir -p build
cd build
cmake ..
make -j4
```

运行程序：

```bash
sudo ./yolov8_project
```

如果修改了 `CMakeLists.txt` 或新增源文件，建议重新执行：

```bash
rm -rf build
mkdir build
cd build
cmake ..
make -j4
```

## 后台与视频显示

PC 端启动 FastAPI 后台：

```bash
cd backend
uvicorn main:app --host 0.0.0.0 --port 8000
```

启动 MediaMTX：

```bash
mediamtx.exe
```

浏览器访问：

```text
http://127.0.0.1:8000
```

视频流地址示例：

```text
rtsp://<board_ip>:8554/live
http://127.0.0.1:8889/live/
```

其中 `<board_ip>` 需要替换成 RK3588 板子的实际 IP。

## 主要性能

| 指标 | 当前表现 |
| --- | --- |
| 摄像头输入 | 1920×1080 NV12 |
| 模型输入 | 640×640 RGB |
| 实时帧率 | 约 29-30 FPS |
| RGA 预处理耗时 | 约 2-3 ms |
| NPU + 后处理耗时 | 约 17-24 ms |
| 数据上报周期 | 约 1 秒 |
| 视频传输方式 | RTSP / WebRTC |

## 技术特点

- 端侧完成完整 AI 推理，不依赖云端计算。
- 使用 RGA + NPU 异构加速，降低 CPU 负载。
- 检测、跟踪、ROI 分析和事件上报形成完整闭环。
- 通过 MediaMTX 将 RTSP 流转换为浏览器可访问的视频流。
- 后台同时展示实时视频、人数统计、设备状态和异常事件。

## 后续优化方向

- 使用多 RKNN Context 调度三核 NPU，提高多路视频处理能力。
- 针对校园实际场景采集数据并微调 YOLO 模型，提升远距离小目标检测效果。
- 增加异常聚集、越界、逆行、摔倒等规则分析。
- 增加历史数据存储、事件检索、视频回放和告警分级功能。
- 优化 Web 页面展示效果和多终端适配能力。

## 注意事项

- `build/` 目录不建议上传到 GitHub。
- `.rknn` 模型文件如果较大，建议使用 Git LFS 或 GitHub Release 管理。
- 第三方库和模型文件需要确认授权许可后再公开上传。
- 板端 IP、PC 端 IP、摄像头设备号 `/dev/videoXX` 需要根据实际环境修改。

## License

本项目仅用于学习、实验和比赛展示。若用于实际部署，请确认模型、第三方库和硬件 SDK 的授权协议。
```

另外建议你再建一个 `.gitignore`，至少把这些排除掉：

```gitignore
build/
.vscode/
*.o
*.so
*.log
recordings/
*.mp4
```

如果 `model/*.rknn` 文件很大，最好不要直接放 GitHub 主仓库，可以放到 Release 或者用 Git LFS。
