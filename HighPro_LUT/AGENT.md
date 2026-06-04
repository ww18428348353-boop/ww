# AGENT.md — HighPro_LUT 协作约定

> 本文件给协作 Agent 看. 任何修改源码后, 必须按以下流程走完, 才能算"完成".

---

## 一. 开发约定

> **每完成一个里程碑批次的所有任务后, 必须立即跑一次 `build.bat release` 验证编译通过 + 烟测核心路径, 再标里程碑 `[x]`.**
>
> 详见 `HighPro_LUT/02_开发里程碑_HighPro_LUT.md` → "开发工作流" 节.

## 二. 开发流程

> **每次开发完成后, 必须执行 `build.bat` 编译, 编译通过后自动启动 `HighPro_LUT.exe` 进行验证.**

执行位置: `HighPro_LUT/` 目录.

```bat
cd /d x:\Lut变色\HighPro_LUT
build.bat            :: Release 默认; 加 debug 走 Debug
HighPro_LUT.exe      :: 烟测启动
```

`build.bat` 会自动:
1. 加载 MSVC vcvars64
2. CMake configure + Ninja 构建
3. windeployqt 部署 Qt dll/插件到项目根
4. 产物: `x:\Lut变色\HighPro_LUT\HighPro_LUT.exe` (动态版, 同目录有 Qt dll)

## 三. 单 EXE 打包 (静态版)

最终交付的"单文件 EXE" 名称固定为 **`LUT_Pro.exe`**.

- **产物名**: `LUT_Pro.exe`
- **构建脚本**: `HighPro_LUT/_build_static.bat`
- **输出目录**: `HighPro_LUT/build-static/LUT_Pro.exe`
- **目标特性**: 静态 Qt (`C:/Qt/6.11.1/msvc2022_64_static`) + MSVC `/MT` 静态运行时, 单文件, 无外部 dll 依赖, 可拷到任意 Win10/11 x64 直接运行.

CMake 决策位置 (`HighPro_LUT/CMakeLists.txt`):

```cmake
# 静态构建产物叫 LUT_Pro.exe; 动态构建仍叫 HighPro_LUT.exe
if(HIGHPRO_STATIC_RUNTIME)
    set_target_properties(HighPro_LUT PROPERTIES OUTPUT_NAME "LUT_Pro")
else()
    set_target_properties(HighPro_LUT PROPERTIES OUTPUT_NAME "HighPro_LUT")
endif()
```

执行步骤:

```bat
cd /d x:\Lut变色\HighPro_LUT
_build_static.bat       :: 静态构建, 产出 build-static\LUT_Pro.exe
```

详细记录见: `HighPro_LUT/静态EXE打包完整记录.md`.

## 四. 命名速查

| EXE 名 | 何时生成 | 来源脚本 | 是否需 Qt dll | 用途 |
|--------|---------|---------|---------------|------|
| `HighPro_LUT.exe` | 日常开发 | `build.bat` | 是 (同目录) | 编译验证 / 调试 |
| `LUT_Pro.exe` | 发布 | `_build_static.bat` | 否 (单文件) | 交付 / 拷贝绿色版 |
| `HighPro_LUT_setup.exe` | 7z SFX 安装包 | `pack.bat` | — | 自解压安装包 (基于动态版) |

## 五. 强制清单 (Definition of Done)

任何代码改动合入前必须确认:

- [ ] `build.bat release` 在 `HighPro_LUT/` 下执行成功 (无 error)
- [ ] `HighPro_LUT.exe` 启动并完成核心路径烟测
- [ ] 涉及发布的改动: `_build_static.bat` 也要跑通, 确认 `LUT_Pro.exe` 正常启动
- [ ] 里程碑批次任务全部完成才能标 `[x]` (见 `02_开发里程碑_HighPro_LUT.md`)
