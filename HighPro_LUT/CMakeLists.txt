cmake_minimum_required(VERSION 3.20)

project(HighPro_LUT
    VERSION 1.0.0
    DESCRIPTION "High-Performance Sequence-Frame Recolor Tool"
    LANGUAGES CXX C
)

# ========= 全局选项 =========
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 单目标输出: 项目根 (双击即可运行, Qt dll 也部署在此)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ========= MSVC =========
if(MSVC)
    add_compile_options(/utf-8 /W3 /MP /EHsc /Zc:__cplusplus /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX UNICODE _UNICODE)
    # 静态运行时 (M10 切换); 一期可保持动态
    if(HIGHPRO_STATIC_RUNTIME)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

# ========= Qt 6 =========
if(NOT DEFINED CMAKE_PREFIX_PATH OR CMAKE_PREFIX_PATH STREQUAL "")
    set(CMAKE_PREFIX_PATH "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt installation path" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Gui)
qt_standard_project_setup()

# ========= 第三方头文件 =========
add_library(extern_stb INTERFACE)
target_include_directories(extern_stb INTERFACE ${CMAKE_SOURCE_DIR}/extern/stb)

add_library(extern_json INTERFACE)
target_include_directories(extern_json INTERFACE ${CMAKE_SOURCE_DIR}/extern)

add_library(extern_gif INTERFACE)
target_include_directories(extern_gif INTERFACE ${CMAKE_SOURCE_DIR}/extern/gif)

# ========= 资源 =========
qt_add_resources(APP_RESOURCES "assets/assets.qrc")

# ========= 源文件 =========
set(APP_SOURCES
    src/main.cpp

    # core
    src/core/PathUtil.h
    src/core/PathUtil.cpp
    src/core/LayerData.h
    src/core/LayerData.cpp
    src/core/ResourceScanner.h
    src/core/ResourceScanner.cpp
    src/core/HaldClut.h
    src/core/HaldClut.cpp
    src/core/ColorEffect.h
    src/core/ColorEffect.cpp
    src/core/CurveSolver.h
    src/core/CurveSolver.cpp
    src/core/KPoint.h
    src/core/Project.h
    src/core/SchemePalette.h
    src/core/SchemePalette.cpp

    # render
    src/render/D3D11Context.h
    src/render/D3D11Context.cpp
    src/render/D3D11Texture.h
    src/render/D3D11Texture.cpp
    src/render/D3D11Shader.h
    src/render/D3D11Shader.cpp
    src/render/FrameLoader.h
    src/render/FrameLoader.cpp
    src/render/FrameRenderer.h
    src/render/FrameRenderer.cpp
    src/render/LutBaker.h
    src/render/LutBaker.cpp
    src/render/PngExporter.h
    src/render/PngExporter.cpp
    src/render/GifExporter.h
    src/render/GifExporter.cpp
    src/render/DebugDumper.h
    src/render/DebugDumper.cpp

    # ui
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/D3DWidget.h
    src/ui/D3DWidget.cpp
    src/ui/PreviewPanel.h
    src/ui/PreviewPanel.cpp
    src/ui/LayerTreePanel.h
    src/ui/LayerTreePanel.cpp
    src/ui/EffectPanel.h
    src/ui/EffectPanel.cpp
    src/ui/SchemePanel.h
    src/ui/SchemePanel.cpp
    src/ui/SchemeRefineDialog.h
    src/ui/SchemeRefineDialog.cpp
    src/ui/widgets/CurveEditor.h
    src/ui/widgets/CurveEditor.cpp

    # app
    src/app/AppSettings.h
    src/app/AppSettings.cpp
    src/app/ProjectController.h
    src/app/ProjectController.cpp
    src/app/ProjectIO.h
    src/app/ProjectIO.cpp
    src/app/ThumbnailWorker.h
    src/app/ThumbnailWorker.cpp
)

# ========= 可执行 =========
qt_add_executable(HighPro_LUT WIN32
    ${APP_SOURCES}
    ${APP_RESOURCES}
)

target_include_directories(HighPro_LUT PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(HighPro_LUT PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    extern_stb
    extern_json
    extern_gif
    # D3D11
    d3d11
    dxgi
    d3dcompiler
    dxguid
)

set_target_properties(HighPro_LUT PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "HighPro_LUT"
)

# ========= 安装 =========
install(TARGETS HighPro_LUT RUNTIME DESTINATION .)
