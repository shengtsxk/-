# ============================================================================
# 灵境 Lingjing — 完整 CMake 构建配置
#
# 支持：
#   - NVIDIA CUDA + TensorRT 后端
#   - Intel SYCL + OpenVINO 后端
#   - Qt6 UI（可选）
#   - Windows / Linux 跨平台
#   - 多配置构建（Debug/Release/RelWithDebInfo）
# ============================================================================

cmake_minimum_required(VERSION 3.24)

# ============================================================================
# 项目声明
# ============================================================================

project(Lingjing
    VERSION 1.3.9
    DESCRIPTION "灵境 Lingjing - 通用游戏帧生成软件"
    HOMEPAGE_URL "https://github.com/lingjing-project/lingjing"
    LANGUAGES CXX C)

# ============================================================================
# C++ 标准
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# 输出目录
# ============================================================================

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# 多配置输出
foreach(CFG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${CFG} CFG_UPPER)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER}
        "${CMAKE_BINARY_DIR}/lib/${CFG}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER}
        "${CMAKE_BINARY_DIR}/lib/${CFG}")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER}
        "${CMAKE_BINARY_DIR}/bin/${CFG}")
endforeach()

# ============================================================================
# 构建类型默认值
# ============================================================================

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release"
        CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ============================================================================
# 构建选项
# ============================================================================

# 后端
option(LJ_ENABLE_NVIDIA   "启用 NVIDIA CUDA + TensorRT 后端" ON)
option(LJ_ENABLE_INTEL    "启用 Intel SYCL + OpenVINO 后端"  ON)

# UI
option(LJ_ENABLE_UI       "启用 Qt6 UI 界面"                 ON)

# 功能模块
option(LJ_ENABLE_AI       "启用 AI 修复模块"                 ON)
option(LJ_ENABLE_LEARNING "启用学习系统"                     ON)
option(LJ_ENABLE_SHUN     "启用瞬低延迟引擎"                 ON)
option(LJ_ENABLE_HUAIZHU  "启用淮竹光流算法"                 ON)

# 构建目标
option(LJ_BUILD_TESTS     "构建单元测试"                     OFF)
option(LJ_BUILD_TOOLS     "构建辅助工具"                     OFF)
option(LJ_BUILD_INSTALLER "生成安装包"                       OFF)

# 优化
option(LJ_ENABLE_LTO      "启用链接时优化 (LTO)"             ON)
option(LJ_ENABLE_AVX2     "启用 AVX2 指令集"                 ON)
option(LJ_ENABLE_AVX512   "启用 AVX512 指令集"               OFF)

# 调试
option(LJ_ENABLE_PROFILING "启用性能分析支持"                OFF)
option(LJ_ENABLE_SANITIZERS "启用 Sanitizer"                 OFF)

# 静态/动态
option(LJ_BUILD_SHARED    "构建为动态库"                     OFF)

# ============================================================================
# C++ 编译器警告和优化
# ============================================================================

if(MSVC)
    # MSVC 特殊处理
    add_compile_options(
        /W4                    # 高警告级别
        /permissive-           # 严格符合标准
        /Zc:__cplusplus        # 正确的 __cplusplus
        /Zc:preprocessor       # 新预处理器
        /Zc:inline             # 移除未引用函数
        /utf-8                 # UTF-8 源文件
        /MP                    # 多进程编译
        /EHsc                  # 异常处理
        /bigobj                # 大对象文件
    )

    # 禁用一些过度严格的警告
    add_compile_options(
        /wd4100   # 未引用的形参
        /wd4244   # 类型转换可能丢失数据
        /wd4267   # size_t 到 int 转换
        /wd4996   # 弃用函数
    )

    # Release 优化
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR
       CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_compile_options(/O2 /Oi /Ot /GL /Gy /Gw)
        add_link_options(/LTCG /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    endif()

    # Debug
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od /RTC1 /Zi)
        add_compile_definitions(_DEBUG)
    endif()

    # 静态运行时（Release 下）
    if(LJ_BUILD_SHARED)
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    # GCC/Clang
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()

    if(LJ_ENABLE_AVX2)
        add_compile_options(-mavx2 -mfma)
    endif()

    if(LJ_ENABLE_AVX512)
        add_compile_options(-mavx512f -mavx512cd)
    endif()
endif()

# ============================================================================
# LTO
# ============================================================================

if(LJ_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LJ_LTO_SUPPORTED OUTPUT LJ_LTO_ERROR)

    if(LJ_LTO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO: enabled")
    else()
        message(WARNING "LTO: not supported - ${LJ_LTO_ERROR}")
    endif()
endif()

# ============================================================================
# Qt6 自动工具
# ============================================================================

if(LJ_ENABLE_UI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

# ============================================================================
# 平台检测
# ============================================================================

if(WIN32)
    set(LJ_PLATFORM "Windows")
    set(LJ_PLATFORM_WINDOWS TRUE)
elseif(UNIX AND NOT APPLE)
    set(LJ_PLATFORM "Linux")
    set(LJ_PLATFORM_LINUX TRUE)
elseif(APPLE)
    set(LJ_PLATFORM "macOS")
    set(LJ_PLATFORM_MACOS TRUE)
else()
    set(LJ_PLATFORM "Unknown")
    message(FATAL_ERROR "不支持的平台")
endif()

message(STATUS "===========================================")
message(STATUS "灵境 Lingjing v${PROJECT_VERSION}")
message(STATUS "平台: ${LJ_PLATFORM}")
message(STATUS "编译器: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "===========================================")

# ============================================================================
# 依赖查找
# ============================================================================

# ---------------------------------------------------------------------------
# 线程
# ---------------------------------------------------------------------------

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# OpenMP（用于部分并行算法）
# ---------------------------------------------------------------------------

find_package(OpenMP QUIET)

# ---------------------------------------------------------------------------
# Qt6
# ---------------------------------------------------------------------------

if(LJ_ENABLE_UI)
    find_package(Qt6 QUIET COMPONENTS
        Core
        Gui
        Widgets
        OpenGL
        OpenGLWidgets
        Concurrent
        Network)

    if(Qt6_FOUND)
        message(STATUS "Qt6: ${Qt6_VERSION}")

        if(TARGET Qt6::OpenGL)
            set(LJ_QT_HAS_OPENGL TRUE)
        endif()
    else()
        message(WARNING "Qt6 未找到，UI 将被禁用")
        set(LJ_ENABLE_UI OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# CUDA
# ---------------------------------------------------------------------------

if(LJ_ENABLE_NVIDIA)
    # 先检查是否可用
    find_package(CUDAToolkit QUIET)

    if(CUDAToolkit_FOUND)
        message(STATUS "CUDA: ${CUDAToolkit_VERSION}")
        set(LJ_HAS_CUDA TRUE)

        # 启用 CUDA 语言
        enable_language(CUDA)

        # CUDA 标准
        set(CMAKE_CUDA_STANDARD 17)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)

        # CUDA 架构
        if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
            set(CMAKE_CUDA_ARCHITECTURES
                "75;80;86;89;90"
                CACHE STRING "CUDA architectures")
        endif()

        message(STATUS "CUDA 架构: ${CMAKE_CUDA_ARCHITECTURES}")

        # CUDA 编译选项
        if(MSVC)
            add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:
                --use_fast_math
                --extra-device-vectorization
                -Xcompiler=/utf-8
                -Xcompiler=/MP
                -Xcompiler=/wd4100
                -Xcompiler=/wd4244>)
        else()
            add_compile_options($<$<COMPILE_LANGUAGE:CUDA>:
                --use_fast_math
                --extra-device-vectorization
                -Xcompiler=-Wall>)
        endif()

        # Release 下优化
        add_compile_options($<$<AND:$<COMPILE_LANGUAGE:CUDA>:
                                       $<CONFIG:Release>>:
            -O3 -lineinfo>)

        # Debug 下保留调试信息
        add_compile_options($<$<AND:$<COMPILE_LANGUAGE:CUDA>:
                                       $<CONFIG:Debug>>:
            -G -g>)

    else()
        message(WARNING "CUDA 未找到，NVIDIA 后端将被禁用")
        set(LJ_ENABLE_NVIDIA OFF)
    endif()
endif()

# ---------------------------------------------------------------------------
# TensorRT
# ---------------------------------------------------------------------------

if(LJ_ENABLE_NVIDIA AND LJ_ENABLE_AI)
    # TensorRT 通常在特定路径
    set(LJ_TENSORRT_ROOT "" CACHE PATH "TensorRT 根目录")

    if(LJ_TENSORRT_ROOT)
        set(LJ_TENSORRT_INCLUDE_HINTS
            "${LJ_TENSORRT_ROOT}/include")
        if(WIN32)
            set(LJ_TENSORRT_LIB_HINTS
                "${LJ_TENSORRT_ROOT}/lib"
                "${LJ_TENSORRT_ROOT}/lib/x64")
        else()
            set(LJ_TENSORRT_LIB_HINTS
                "${LJ_TENSORRT_ROOT}/lib"
                "${LJ_TENSORRT_ROOT}/lib/x86_64-linux-gnu")
        endif()
    endif()

    find_path(LJ_TENSORRT_INCLUDE_DIR
        NAMES NvInfer.h
        HINTS ${LJ_TENSORRT_INCLUDE_HINTS}
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/TensorRT/include"
        DOC "TensorRT include dir")

    find_library(LJ_TENSORRT_LIBRARY
        NAMES nvinfer nvinfer_10
        HINTS ${LJ_TENSORRT_LIB_HINTS}
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/TensorRT/lib"
        DOC "TensorRT library")

    find_library(LJ_TENSORRT_PLUGIN_LIBRARY
        NAMES nvinfer_plugin nvinfer_plugin_10
        HINTS ${LJ_TENSORRT_LIB_HINTS}
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/TensorRT/lib"
        DOC "TensorRT plugin library")

    find_library(LJ_NVONNXPARSER_LIBRARY
        NAMES nvonnxparser nvonnxparser_10
        HINTS ${LJ_TENSORRT_LIB_HINTS}
        PATHS
            "${CMAKE_SOURCE_DIR}/third_party/TensorRT/lib"
        DOC "TensorRT ONNX parser library")

    if(LJ_TENSORRT_INCLUDE_DIR AND LJ_TENSORRT_LIBRARY)
        message(STATUS "TensorRT: found")
        set(LJ_HAS_TENSORRT TRUE)
    else()
        message(WARNING "TensorRT 未找到，AI 修复的 TensorRT 后端将不可用")
        set(LJ_HAS_TENSORRT FALSE)
    endif()
endif()

# ---------------------------------------------------------------------------
# NVOFA (NVIDIA Optical Flow SDK)
# ---------------------------------------------------------------------------

if(LJ_ENABLE_NVIDIA)
    set(LJ_NVOFA_ROOT
        "${CMAKE_SOURCE_DIR}/third_party/nvidia_optical_flow_sdk"
        CACHE PATH "NVOFA 根目录")

    find_path(LJ_NVOFA_INCLUDE_DIR
        NAMES nvofapi/nvofapi.h
        HINTS
            "${LJ_NVOFA_ROOT}/Interface"
            "${LJ_NVOFA_ROOT}/include"
        DOC "NVOFA include dir")

    if(WIN32)
        set(LJ_NVOFA_LIB_NAME nvofapi64)
        set(LJ_NVOFA_LIB_HINTS
            "${LJ_NVOFA_ROOT}/Lib/win/x64")
    else()
        set(LJ_NVOFA_LIB_NAME nvofapi)
        set(LJ_NVOFA_LIB_HINTS
            "${LJ_NVOFA_ROOT}/Lib/linux/x64")
    endif()

    find_library(LJ_NVOFA_LIBRARY
        NAMES ${LJ_NVOFA_LIB_NAME}
        HINTS ${LJ_NVOFA_LIB_HINTS}
        DOC "NVOFA library")

    if(LJ_NVOFA_INCLUDE_DIR AND LJ_NVOFA_LIBRARY)
        message(STATUS "NVOFA: found")
        set(LJ_HAS_NVOFA TRUE)
    else()
        message(STATUS "NVOFA: not found (optional)")
        set(LJ_HAS_NVOFA FALSE)
    endif()
endif()

# ---------------------------------------------------------------------------
# NVML (NVIDIA Management Library)
# ---------------------------------------------------------------------------

if(LJ_ENABLE_NVIDIA)
    find_path(LJ_NVML_INCLUDE_DIR
        NAMES nvml.h
        PATHS
            "${CUDAToolkit_INCLUDE_DIRS}"
        DOC "NVML include dir")

    find_library(LJ_NVML_LIBRARY
        NAMES nvml
        PATHS
            "${CUDAToolkit_LIBRARY_DIR}"
        DOC "NVML library")

    if(LJ_NVML_INCLUDE_DIR AND LJ_NVML_LIBRARY)
        message(STATUS "NVML: found")
        set(LJ_HAS_NVML TRUE)
    else()
        message(STATUS "NVML: not found")
        set(LJ_HAS_NVML FALSE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Intel oneAPI / SYCL
# ---------------------------------------------------------------------------

if(LJ_ENABLE_INTEL)
    # 优先查找 oneAPI
    find_package(IntelSYCL QUIET)

    if(IntelSYCL_FOUND)
        message(STATUS "Intel oneAPI SYCL: found")
        set(LJ_HAS_SYCL TRUE)
    else()
        # 尝试直接查找 SYCL 编译器
        find_program(LJ_ICPX_EXECUTABLE
            NAMES icpx dpcpp
            PATHS
                "C:/Program Files (x86)/Intel/oneAPI/compiler/latest/windows/bin"
                "C:/Program Files/Intel/oneAPI/compiler/latest/windows/bin"
                "/opt/intel/oneapi/compiler/latest/linux/bin"
            DOC "Intel oneAPI 编译器")

        if(LJ_ICPX_EXECUTABLE)
            message(STATUS "SYCL 编译器: ${LJ_ICPX_EXECUTABLE}")
            set(LJ_HAS_SYCL TRUE)
        else()
            message(WARNING "Intel oneAPI 未找到，Intel 后端将被禁用")
            set(LJ_ENABLE_INTEL OFF)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# OpenVINO
# ---------------------------------------------------------------------------

if(LJ_ENABLE_INTEL AND LJ_ENABLE_AI)
    set(OpenVINO_DIR
        "${CMAKE_SOURCE_DIR}/third_party/OpenVINO/cmake"
        CACHE PATH "OpenVINO cmake 目录")

    find_package(OpenVINO QUIET)

    if(OpenVINO_FOUND)
        message(STATUS "OpenVINO: ${OpenVINO_VERSION}")
        set(LJ_HAS_OPENVINO TRUE)
    else()
        message(WARNING "OpenVINO 未找到，AI 修复的 OpenVINO 后端将不可用")
        set(LJ_HAS_OPENVINO FALSE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Windows 系统库
# ---------------------------------------------------------------------------

if(WIN32)
    set(LJ_WIN_SYSTEM_LIBS
        d3d11          # Direct3D 11
        dxgi           # DirectX Graphics Infrastructure
        d3dcompiler    # D3D 着色器编译
        dwmapi         # Desktop Window Manager
        shcore         # DPI 感知
        shlwapi        # Shell 工具
        user32         # 用户输入
        gdi32          # GDI
        ole32          # COM
        oleaut32       # OLE 自动化
        psapi          # 进程状态
        version        # 版本信息
        winmm          # 多媒体计时器
        xinput         # Xbox 手柄
        bcrypt         # 加密（SHA256）
        runtimeobject   # WinRT RoGetActivationFactory
        crypt32        # 加密
        wininet        # 网络
        ws2_32         # Winsock
        iphlpapi       # IP 助手
        setupapi       # 设备安装
        advapi32       # 高级 API
        kernel32       # 内核
    )
endif()

# ---------------------------------------------------------------------------
# Linux 系统库
# ---------------------------------------------------------------------------

if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(X11 QUIET x11)
        pkg_check_modules(WAYLAND QUIET wayland-client)
        pkg_check_modules(VULKAN QUIET vulkan)
        pkg_check_modules(EGL QUIET egl)
        pkg_check_modules(GL QUIET gl)
    endif()
endif()

# ============================================================================
# 源文件列表
# ============================================================================

# ---------------------------------------------------------------------------
# 核心模块
# ---------------------------------------------------------------------------

set(LJ_CORE_SOURCES
    src/core/Logger.cpp
    src/core/Timer.cpp
    src/core/Config.cpp
    src/core/UpdateChecker.cpp
    src/core/Error.cpp
    src/core/DeviceCaps.cpp
    src/core/PathUtils.cpp
    src/core/StringUtils.cpp
    src/core/Json.cpp
    src/core/MemoryPool.cpp
)

# ---------------------------------------------------------------------------
# 捕获模块
# ---------------------------------------------------------------------------

set(LJ_CAPTURE_SOURCES
    src/capture/WGCCapture.cpp
    src/capture/DXGICapture.cpp
    src/capture/WindowEnumerator.cpp
    src/capture/CaptureSelector.cpp
)

if(LJ_PLATFORM_WINDOWS)
    list(APPEND LJ_CAPTURE_SOURCES
        src/capture/HookedCapture.cpp
        src/capture/CursorRenderer.cpp
    )
endif()

# ---------------------------------------------------------------------------
# GPU 模块
# ---------------------------------------------------------------------------

set(LJ_GPU_SOURCES
    src/gup/GpuSelector.cpp
)

if(LJ_HAS_CUDA)
    list(APPEND LJ_GPU_SOURCES
        src/gup/CudaContext.cpp
        src/gup/CudaInterop.cpp
    )
endif()

if(LJ_HAS_SYCL)
    list(APPEND LJ_GPU_SOURCES
        src/gup/SyclContext.cpp
        src/gup/SyclInterop.cpp
    )
endif()

# ---------------------------------------------------------------------------
# 光流模块（淮竹）
# ---------------------------------------------------------------------------

set(LJ_FLOW_SOURCES
    src/flow/FlowEngineFactory.cpp
    src/flow/HuaiZhuEngine.cpp
    src/flow/TimeGeometryMemory.cpp
    src/flow/MultiCueProjection.cpp
    src/flow/Coherent3DFlow.cpp
    src/flow/AdaptiveConfidenceField.cpp
)

# ---------------------------------------------------------------------------
# AI 模块
# ---------------------------------------------------------------------------

set(LJ_AI_SOURCES
    src/ai/ModelLoader.cpp
    src/ai/ModelValidator.cpp
    src/ai/AIRepairFactory.cpp
)

if(LJ_HAS_TENSORRT)
    list(APPEND LJ_AI_SOURCES
        src/ai/TensorRTEngine.cpp
        src/ai/TensorRTAIRepair.cpp
    )
endif()

if(LJ_HAS_OPENVINO)
    list(APPEND LJ_AI_SOURCES
        src/ai/OpenVINOEngine.cpp
        src/ai/OpenVINOAIREpair.cpp
    )
endif()

# ---------------------------------------------------------------------------
# 渲染模块
# ---------------------------------------------------------------------------

set(LJ_RENDER_SOURCES
    src/pipeline/FrameBlender.cpp
    src/pipeline/OcclusionHandler.cpp
    src/pipeline/Presenter.cpp
    src/pipeline/MultiFrameGenerator.cpp
    src/pipeline/D3D11FrameInterpolator.cpp
    src/pipeline/D3D11SuperResolution.cpp
)

# ---------------------------------------------------------------------------
# 管线模块
# ---------------------------------------------------------------------------

set(LJ_PIPELINE_SOURCES
    src/pipeline/FrameGenPipeline.cpp
    src/pipeline/LatencyController.cpp
    src/pipeline/SceneDetector.cpp
    src/pipeline/PerformanceMonitor.cpp
)

# ---------------------------------------------------------------------------
# 学习模块
# ---------------------------------------------------------------------------

if(LJ_ENABLE_LEARNING)
    set(LJ_LEARNING_SOURCES
        src/learning/GameFingerprint.cpp
        src/learning/LearningDatabase.cpp
        src/learning/OnlineLearner.cpp
        src/learning/TransferLearning.cpp
        src/learning/ActiveLearning.cpp
        src/learning/LearningManager.cpp
        src/learning/LearningViewModel.cpp
    )
else()
    set(LJ_LEARNING_SOURCES "")
endif()

# ---------------------------------------------------------------------------
# 瞬 低延迟模块
# ---------------------------------------------------------------------------

if(LJ_ENABLE_SHUN)
    set(LJ_SHUN_SOURCES
        src/shun/ShunEngine.cpp
        src/shun/ShunController.cpp
        src/shun/InputPresampler.cpp
        src/shun/TimerBooster.cpp
        src/shun/PresentAnalyzer.cpp
        src/shun/DwmOptimizer.cpp
        src/shun/GpuQueueMonitor.cpp
        src/shun/ProcessOptimizer.cpp
        src/shun/DisplaySync.cpp
        src/shun/LatencyMeasurement.cpp
    )
else()
    set(LJ_SHUN_SOURCES "")
endif()

# ---------------------------------------------------------------------------
# UI 模块
# ---------------------------------------------------------------------------

if(LJ_ENABLE_UI)
    set(LJ_UI_SOURCES
        src/ui/MainWindow.cpp
        src/ui/ControlPanel.cpp
        src/ui/GameListWidget.cpp
        src/ui/PerformanceGraphWidget.cpp
        src/ui/SettingsDialog.cpp
        src/ui/RippleWidget.cpp
        src/ui/WallpaperEngine.cpp
        src/ui/TrayIcon.cpp
        src/ui/Theme.cpp
    )
else()
    set(LJ_UI_SOURCES "")
endif()

# ---------------------------------------------------------------------------
# CUDA 内核
# ---------------------------------------------------------------------------

if(LJ_HAS_CUDA)
    set(LJ_CUDA_KERNELS
        kernels/cuda/flow_solver.cu
        kernels/cuda/flow_consistency.cu
        kernels/cuda/flow_infill.cu
        kernels/cuda/frame_warp.cu
        kernels/cuda/frame_blend.cu
        kernels/cuda/dejelly.cu
        kernels/cuda/ai_repair.cu
        kernels/cuda/geometry_inversion.cu
    )

    # 只包含存在的文件
    set(LJ_CUDA_KERNELS_FILTERED "")
    foreach(src ${LJ_CUDA_KERNELS})
        if(EXISTS "${CMAKE_SOURCE_DIR}/${src}")
            list(APPEND LJ_CUDA_KERNELS_FILTERED ${src})
        endif()
    endforeach()
    set(LJ_CUDA_KERNELS ${LJ_CUDA_KERNELS_FILTERED})
else()
    set(LJ_CUDA_KERNELS "")
endif()

# ---------------------------------------------------------------------------
# SYCL 内核
# ---------------------------------------------------------------------------

if(LJ_HAS_SYCL)
    set(LJ_SYCL_KERNELS
        kernels/sycl/flow_solver.cpp
        kernels/sycl/flow_consistency.cpp
        kernels/sycl/frame_warp.cpp
        kernels/sycl/frame_blend.cpp
        kernels/sycl/dejelly.cpp
    )

    set(LJ_SYCL_KERNELS_FILTERED "")
    foreach(src ${LJ_SYCL_KERNELS})
        if(EXISTS "${CMAKE_SOURCE_DIR}/${src}")
            list(APPEND LJ_SYCL_KERNELS_FILTERED ${src})
        endif()
    endforeach()
    set(LJ_SYCL_KERNELS ${LJ_SYCL_KERNELS_FILTERED})
else()
    set(LJ_SYCL_KERNELS "")
endif()

# ---------------------------------------------------------------------------
# 主目标源文件
# ---------------------------------------------------------------------------

set(LJ_SOURCES
    src/main.cpp
    ${LJ_CORE_SOURCES}
    ${LJ_CAPTURE_SOURCES}
    ${LJ_GPU_SOURCES}
    ${LJ_FLOW_SOURCES}
    ${LJ_AI_SOURCES}
    ${LJ_RENDER_SOURCES}
    ${LJ_PIPELINE_SOURCES}
    ${LJ_LEARNING_SOURCES}
    ${LJ_SHUN_SOURCES}
    ${LJ_UI_SOURCES}
    ${LJ_CUDA_KERNELS}
)

# 过滤不存在的文件（用于脚本生成后的渐进式开发）
set(LJ_SOURCES_FILTERED "")
foreach(src ${LJ_SOURCES})
    if(EXISTS "${CMAKE_SOURCE_DIR}/${src}")
        list(APPEND LJ_SOURCES_FILTERED ${src})
    else()
        message(STATUS "跳过不存在的源文件: ${src}")
    endif()
endforeach()

set(LJ_SOURCES ${LJ_SOURCES_FILTERED})

# ============================================================================
# 头文件列表（用于 IDE）
# ============================================================================

file(GLOB_RECURSE LJ_HEADERS
    "${CMAKE_SOURCE_DIR}/include/*.h"
    "${CMAKE_SOURCE_DIR}/include/*.hpp")

# ============================================================================
# 主目标
# ============================================================================

if(LJ_PLATFORM_WINDOWS)
    # Windows：无控制台窗口
    add_executable(Lingjing WIN32
        ${LJ_SOURCES}
        ${LJ_HEADERS})
else()
    add_executable(Lingjing
        ${LJ_SOURCES}
        ${LJ_HEADERS})
endif()

# ============================================================================
# 目标属性
# ============================================================================

set_target_properties(Lingjing PROPERTIES
    OUTPUT_NAME "Lingjing"
    DEBUG_POSTFIX "_d"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_ENVIRONMENT
        "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
)

# 版本信息
set_target_properties(Lingjing PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ============================================================================
# 包含目录
# ============================================================================

target_include_directories(Lingjing
    PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/third_party"

        # Qt6
        $<$<BOOL:${Qt6_FOUND}>:${Qt6_INCLUDE_DIRS}>

        # CUDA
        $<$<BOOL:${LJ_HAS_CUDA}>:${CUDAToolkit_INCLUDE_DIRS}>

        # TensorRT
        $<$<BOOL:${LJ_HAS_TENSORRT}>:${LJ_TENSORRT_INCLUDE_DIR}>

        # NVML
        $<$<BOOL:${LJ_HAS_NVML}>:${LJ_NVML_INCLUDE_DIR}>

        # NVOFA
        $<$<BOOL:${LJ_HAS_NVOFA}>:${LJ_NVOFA_INCLUDE_DIR}>

        # OpenVINO
        $<$<BOOL:${LJ_HAS_OPENVINO}>:${OpenVINO_INCLUDE_DIRS}>

        # Intel SYCL
        $<$<BOOL:${LJ_HAS_SYCL}>:${SYCL_INCLUDE_DIRS}>
)

# ============================================================================
# 链接库
# ============================================================================

target_link_libraries(Lingjing
    PRIVATE
        Threads::Threads

        # Windows 系统库
        $<$<PLATFORM_ID:Windows>:${LJ_WIN_SYSTEM_LIBS}>

        # OpenMP
        $<$<BOOL:${OpenMP_CXX_FOUND}>:OpenMP::OpenMP_CXX>

        # Qt6
        $<$<BOOL:${Qt6_FOUND}>:
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Concurrent
            Qt6::Network
        >
        $<$<AND:$<BOOL:${Qt6_FOUND}>,$<BOOL:${LJ_QT_HAS_OPENGL}>>:
            Qt6::OpenGL
            Qt6::OpenGLWidgets
        >

        # CUDA
        $<$<BOOL:${LJ_HAS_CUDA}>:
            ${CUDAToolkit_LIBRARIES}
            CUDA::cudart
            CUDA::cuda_driver
        >

        # TensorRT
        $<$<BOOL:${LJ_HAS_TENSORRT}>:
            ${LJ_TENSORRT_LIBRARY}
            ${LJ_TENSORRT_PLUGIN_LIBRARY}
            ${LJ_NVONNXPARSER_LIBRARY}
        >

        # NVML
        $<$<BOOL:${LJ_HAS_NVML}>:${LJ_NVML_LIBRARY}>

        # NVOFA
        $<$<BOOL:${LJ_HAS_NVOFA}>:${LJ_NVOFA_LIBRARY}>

        # OpenVINO
        $<$<BOOL:${LJ_HAS_OPENVINO}>:${OpenVINO_LIBRARIES}>

        # Intel SYCL
        $<$<BOOL:${LJ_HAS_SYCL}>:${SYCL_LIBRARIES}>

        # X11 / Vulkan (Linux)
        $<$<PLATFORM_ID:Linux>:X11::X11>
        $<$<PLATFORM_ID:Linux>:Vulkan::Vulkan>
)

# ============================================================================
# 编译定义
# ============================================================================

target_compile_definitions(Lingjing
    PRIVATE
        # 版本
        LINGJING_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        LINGJING_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        LINGJING_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        LINGJING_VERSION_STRING="${PROJECT_VERSION}"

        # 平台
        $<$<PLATFORM_ID:Windows>:LJ_WINDOWS>
        $<$<PLATFORM_ID:Linux>:LJ_LINUX>
        $<$<PLATFORM_ID:Darwin>:LJ_MACOS>

        # 后端
        $<$<BOOL:${LJ_ENABLE_NVIDIA}>:LJ_NVIDIA>
        $<$<BOOL:${LJ_ENABLE_INTEL}>:LJ_INTEL>
        $<$<BOOL:${LJ_HAS_CUDA}>:LJ_HAS_CUDA>
        $<$<BOOL:${LJ_HAS_TENSORRT}>:LJ_HAS_TENSORRT>
        $<$<BOOL:${LJ_HAS_NVML}>:LJ_HAS_NVML>
        $<$<BOOL:${LJ_HAS_NVOFA}>:LJ_HAS_NVOFA>
        $<$<BOOL:${LJ_HAS_SYCL}>:LJ_HAS_SYCL>
        $<$<BOOL:${LJ_HAS_OPENVINO}>:LJ_HAS_OPENVINO>

        # 功能
        $<$<BOOL:${LJ_ENABLE_UI}>:LJ_ENABLE_UI>
        $<$<BOOL:${LJ_ENABLE_AI}>:LJ_ENABLE_AI>
        $<$<BOOL:${LJ_ENABLE_LEARNING}>:LJ_ENABLE_LEARNING>
        $<$<BOOL:${LJ_ENABLE_SHUN}>:LJ_ENABLE_SHUN>
        $<$<BOOL:${LJ_ENABLE_HUAIZHU}>:LJ_ENABLE_HUAIZHU>

        # 通用
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        $<$<BOOL:${LJ_ENABLE_PROFILING}>:LJ_PROFILING>
        $<$<BOOL:${LJ_ENABLE_SANITIZERS}>:LJ_SANITIZERS>
)

# ============================================================================
# PCH（预编译头）
# ============================================================================

if(EXISTS "${CMAKE_SOURCE_DIR}/include/Lingjing.h")
    target_precompile_headers(Lingjing PRIVATE
        "${CMAKE_SOURCE_DIR}/include/Lingjing.h")
endif()

# ============================================================================
# 资源文件
# ============================================================================

# Qt 资源
if(LJ_ENABLE_UI AND EXISTS "${CMAKE_SOURCE_DIR}/resources/resources.qrc")
    qt_add_resources(Lingjing "resources"
        PREFIX "/"
        FILES
            "${CMAKE_SOURCE_DIR}/resources/resources.qrc"
    )
endif()

# Windows 资源（图标）
if(LJ_PLATFORM_WINDOWS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/assets/icons/app.rc")
        target_sources(Lingjing PRIVATE
            "${CMAKE_SOURCE_DIR}/assets/icons/app.rc")
    endif()
endif()

# ============================================================================
# 拷贝资源
# ============================================================================

# 拷贝 assets
if(EXISTS "${CMAKE_SOURCE_DIR}/assets")
    add_custom_command(TARGET Lingjing POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:Lingjing>/assets"
        COMMENT "拷贝 assets 目录")
endif()

# 拷贝 models
if(EXISTS "${CMAKE_SOURCE_DIR}/models")
    add_custom_command(TARGET Lingjing POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/models"
            "$<TARGET_FILE_DIR:Lingjing>/models"
        COMMENT "拷贝 models 目录")
endif()

# 拷贝 docs
if(EXISTS "${CMAKE_SOURCE_DIR}/docs")
    add_custom_command(TARGET Lingjing POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/docs"
            "$<TARGET_FILE_DIR:Lingjing>/docs"
        COMMENT "拷贝 docs 目录")
endif()

# 拷贝第三方运行时 DLL（Windows）
if(LJ_PLATFORM_WINDOWS)
    # CUDA 运行时
    if(LJ_HAS_CUDA AND EXISTS "${CUDAToolkit_BIN_DIR}")
        add_custom_command(TARGET Lingjing POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CUDAToolkit_BIN_DIR}/cudart64_12.dll"
                "$<TARGET_FILE_DIR:Lingjing>"
            COMMENT "拷贝 CUDA 运行时"
            COMMAND_EXPAND_LISTS
        )
    endif()

    # TensorRT DLL
    if(LJ_HAS_TENSORRT)
        add_custom_command(TARGET Lingjing POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${LJ_TENSORRT_LIBRARY}>"
                "$<TARGET_FILE_DIR:Lingjing>"
            COMMENT "拷贝 TensorRT 运行时")
    endif()
endif()

# ============================================================================
# 安装规则
# ============================================================================

include(GNUInstallDirs)

install(TARGETS Lingjing
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT Runtime)

# 安装资源
install(DIRECTORY assets/
    DESTINATION "${CMAKE_INSTALL_BINDIR}/assets"
    COMPONENT Runtime)

install(DIRECTORY models/
    DESTINATION "${CMAKE_INSTALL_BINDIR}/models"
    COMPONENT Models
    OPTIONAL)

install(DIRECTORY docs/
    DESTINATION "${CMAKE_INSTALL_BINDIR}/docs"
    COMPONENT Documentation
    OPTIONAL)

install(FILES
    "${CMAKE_SOURCE_DIR}/README.md"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Documentation
    OPTIONAL)

install(FILES
    "${CMAKE_SOURCE_DIR}/LICENSE.txt"
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
    COMPONENT Documentation
    OPTIONAL)

# ============================================================================
# CPack 打包
# ============================================================================

set(CPACK_PACKAGE_NAME "Lingjing")
set(CPACK_PACKAGE_VENDOR "Lingjing Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "通用游戏帧生成软件")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

set(CPACK_PACKAGE_INSTALL_DIRECTORY "Lingjing")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")

    # NSIS 配置
    set(CPACK_NSIS_DISPLAY_NAME "灵境 Lingjing")
    set(CPACK_NSIS_PACKAGE_NAME "Lingjing")
    set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/icons/app.ico")
    set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/assets/icons/app.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "Lingjing.exe")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/lingjing-project")
    set(CPACK_NSIS_HELP_LINK "https://github.com/lingjing-project/issues")
    set(CPACK_NSIS_MENU_LINKS
        "https://github.com/lingjing-project" "灵境 官网")
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\灵境.lnk' '$INSTDIR\\\\Lingjing.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\灵境.lnk'")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;TGZ")

    # DEB 配置
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Lingjing Project <dev@lingjing.dev>")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libgcc-s1, libstdc++6")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
endif()

include(CPack)

# ============================================================================
# 测试
# ============================================================================

if(LJ_BUILD_TESTS)
    enable_testing()

    # Google Test
    find_package(GTest QUIET)

    if(GTest_FOUND)
        add_executable(LingjingTests
            tests/IntegrationTest.cpp
            tests/CoreTests.cpp
            tests/FlowTests.cpp)

        target_include_directories(LingjingTests PRIVATE
            "${CMAKE_SOURCE_DIR}/include")

        target_link_libraries(LingjingTests PRIVATE
            GTest::gtest
            GTest::gtest_main
            Threads::Threads)

        add_test(NAME LingjingTests COMMAND LingjingTests)
    else()
        message(WARNING "Google Test 未找到，跳过测试构建")
    endif()
endif()

# ============================================================================
# 辅助工具
# ============================================================================

if(LJ_BUILD_TOOLS)
    # 模型检查工具
    add_executable(ModelChecker
        tools/ModelChecker.cpp
        src/core/Error.cpp
        src/core/PathUtils.cpp
        src/core/StringUtils.cpp)

    target_include_directories(ModelChecker PRIVATE
        "${CMAKE_SOURCE_DIR}/include")

    # GPU 信息工具
    add_executable(GpuInfo
        tools/GpuInfo.cpp
        src/core/DeviceCaps.cpp
        src/core/Logger.cpp)

    target_include_directories(GpuInfo PRIVATE
        "${CMAKE_SOURCE_DIR}/include")

    target_link_libraries(GpuInfo PRIVATE
        $<$<BOOL:${LJ_HAS_NVML}>:${LJ_NVML_LIBRARY}>
        dxgi)
endif()

# ============================================================================
# 自定义目标
# ============================================================================

# 清理缓存
add_custom_target(clean-cache
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        "$<TARGET_FILE_DIR:Lingjing>/cache"
    COMMENT "清理缓存目录")

# 打包
if(LJ_BUILD_INSTALLER)
    add_custom_target(package-all
        COMMAND ${CMAKE_CPACK_COMMAND} -G NSIS
        COMMAND ${CMAKE_CPACK_COMMAND} -G ZIP
        DEPENDS Lingjing
        COMMENT "生成所有安装包")
endif()

# 版本信息
add_custom_target(version
    COMMAND ${CMAKE_COMMAND} -E echo
        "Lingjing v${PROJECT_VERSION} (${LJ_PLATFORM})"
    COMMAND ${CMAKE_COMMAND} -E echo
        "CUDA: ${LJ_HAS_CUDA}"
    COMMAND ${CMAKE_COMMAND} -E echo
        "TensorRT: ${LJ_HAS_TENSORRT}"
    COMMAND ${CMAKE_COMMAND} -E echo
        "SYCL: ${LJ_HAS_SYCL}"
    COMMAND ${CMAKE_COMMAND} -E echo
        "OpenVINO: ${LJ_HAS_OPENVINO}"
    COMMENT "显示构建信息")

# ============================================================================
# 构建总结
# ============================================================================

message(STATUS "")
message(STATUS "===========================================")
message(STATUS "构建配置总结")
message(STATUS "===========================================")
message(STATUS "项目:        ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "平台:        ${LJ_PLATFORM}")
message(STATUS "构建类型:    ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ 标准:    C++${CMAKE_CXX_STANDARD}")
message(STATUS "LTO:         ${LJ_ENABLE_LTO}")
message(STATUS "")
message(STATUS "后端:")
message(STATUS "  NVIDIA:    ${LJ_ENABLE_NVIDIA}")
message(STATUS "  CUDA:      ${LJ_HAS_CUDA}")
message(STATUS "  TensorRT:  ${LJ_HAS_TENSORRT}")
message(STATUS "  NVOFA:     ${LJ_HAS_NVOFA}")
message(STATUS "  NVML:      ${LJ_HAS_NVML}")
message(STATUS "  Intel:     ${LJ_ENABLE_INTEL}")
message(STATUS "  SYCL:      ${LJ_HAS_SYCL}")
message(STATUS "  OpenVINO:  ${LJ_HAS_OPENVINO}")
message(STATUS "")
message(STATUS "功能:")
message(STATUS "  UI:        ${LJ_ENABLE_UI}")
message(STATUS "  Qt6:       ${Qt6_FOUND}")
message(STATUS "  AI:        ${LJ_ENABLE_AI}")
message(STATUS "  学习:      ${LJ_ENABLE_LEARNING}")
message(STATUS "  瞬:        ${LJ_ENABLE_SHUN}")
message(STATUS "  淮竹:      ${LJ_ENABLE_HUAIZHU}")
message(STATUS "")
message(STATUS "源文件:      ${LJ_SOURCES}")
message(STATUS "输出目录:    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "===========================================")
message(STATUS "")
