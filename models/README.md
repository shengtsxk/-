\# 灵境 AI 模型目录



本目录用于存放 AI 修复模型。灵境会自动扫描此目录及用户目录

（`%APPDATA%/Lingjing/models/`）下的模型文件。



\## 支持格式



| 格式 | 扩展名 | 说明 |

|------|--------|------|

| ONNX | .onnx | 通用格式，首次加载会编译为引擎 |

| TensorRT | .trt, .engine, .plan | NVIDIA 专用，已编译引擎 |

| OpenVINO IR | .xml + .bin | Intel 专用 |



\## 推荐模型



\### Lingjing Repair V1 (FP16)



\- \*\*文件\*\*：`lingjing\_repair\_v1\_fp16.onnx`

\- \*\*大小\*\*：\~45 MB

\- \*\*精度\*\*：FP16

\- \*\*输入\*\*：1×3×H×W

\- \*\*输出\*\*：1×3×H×W

\- \*\*用途\*\*：通用画质修复

\- \*\*性能\*\*：约 0.5 ms @ 1080p (RTX 4070)



\### Lingjing Repair V1 (INT8)



\- \*\*文件\*\*：`lingjing\_repair\_v1\_int8.onnx`

\- \*\*大小\*\*：\~12 MB

\- \*\*精度\*\*：INT8

\- \*\*用途\*\*：低功耗 GPU

\- \*\*性能\*\*：约 0.3 ms @ 1080p



\### Lingjing Repair Pro (FP16)



\- \*\*文件\*\*：`lingjing\_repair\_pro\_fp16.onnx`

\- \*\*大小\*\*：\~120 MB

\- \*\*精度\*\*：FP16

\- \*\*用途\*\*：高质量场景

\- \*\*性能\*\*：约 1.2 ms @ 1080p



\## 目录结构



