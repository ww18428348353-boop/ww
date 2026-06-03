# HighPro_LUT

> 高性能序列帧角色换色工具 · C++17 / Qt 6 / Direct3D 11

## 构建环境

| 工具 | 版本 | 安装位置 |
|------|------|---------|
| Visual Studio 2026 (含 C++ Tools) | 17.51+ | `C:\Program Files\Microsoft Visual Studio\18\Community` |
| Qt | 6.11.1 (msvc2022_64) | `C:\Qt\6.11.1\msvc2022_64` |
| CMake | 3.20+ | `C:\Qt\Tools\CMake_64\bin` |
| Ninja | 1.x | `C:\Qt\Tools\Ninja` |
| Windows SDK | 10.0.22621+ | (随 VS 安装) |

## 构建步骤

### 一键脚本 (推荐)

```cmd
cd HighPro_LUT
build.bat            :: Release 构建
build.bat debug      :: Debug 构建
```

> **开发约定**: 每完成一个里程碑批次的所有任务后, 必须立即跑一次 `build.bat release` 验证编译通过 + 烟测核心路径, 再标里程碑 `[x]`. 详见 `02_开发里程碑_HighPro_LUT.md` → "开发工作流" 节.
>
> **开发流程**: 每次开发完成后, 必须执行 `build.bat` 编译, 编译通过后自动启动 `HighPro_LUT.exe` 进行验证.

### 手动 (CMake Preset)

打开 `x64 Native Tools Command Prompt for VS 2026`:

```cmd
cd HighPro_LUT
cmake --preset msvc-release
cmake --build build/release
build\release\bin\HighPro_LUT.exe
```

### 部署 (Debug 一期, Qt dll 同目录)

```cmd
windeployqt --qmldir . build\release\bin\HighPro_LUT.exe
```

## 目录

```
HighPro_LUT/
├── CMakeLists.txt
├── CMakePresets.json
├── build.bat             一键编译
├── extern/               第三方头文件 (stb / nlohmann_json)
├── assets/               资源 (颜色图.png, shader, 字体, 图标)
├── src/
│   ├── core/             数据/算法 (HALD-CLUT/扫描/效果/方案)
│   ├── render/           D3D11 + 渲染
│   ├── ui/               Qt Widgets UI
│   └── app/              应用层 (设置, 控制器)
└── tests/                单元测试
```

## 进度

参见 [`../开发文档/02_开发里程碑_HighPro_LUT.md`](../开发文档/02_开发里程碑_HighPro_LUT.md)

- [x] M1 项目骨架 + D3D11 渲染 hello
- [x] M2 资源扫描 + 单层序列帧播放
- [x] M3 HALD-CLUT 加载 + 运行时变色
- [x] M4 7 效果 HLSL + 烘焙器 + EffectPanel  *(批 2: LutBaker + add_lut PNG 导出)*
- [ ] M5 多层合成 + 方案管理 + 缩略图
- [ ] M6 复杂控件 (曲线/通道混合/色彩平衡)
- [ ] M7 随机化 + 肤色层 + 重置 + addon 单选
- [ ] M8 PNG 导出 + 项目持久化
- [ ] M9 性能调优 + 异步加载 + LRU
- [ ] M10 静态打包 + 三种结构兼容测试
