# Halcon_Python

一个 HALCON 扩展包，在 HALCON 中嵌入 Python 解释器，使 HDevelop 程序能够执行 Python 代码，并在 HALCON 与 Python 科学计算生态（NumPy、SciPy、matplotlib、OpenCV 等）之间双向交换数据（标量、数组、图像）。

API 接口与现有的 **Halcon_Matlab** 扩展保持一致，仅底层引擎调用不同。

---

## 功能特性

- 通过 `Py_EvalString` 在 HDevelop 中执行任意 Python 代码
- 使用 `Py_Import` 导入 Python 模块（`numpy`、`scipy`、`cv2` 等）
- 双向传输标量（int / float / string）
- 以 NumPy 数组形式双向传输矩阵/数组
- 将 HALCON 图像以 NumPy 数组形式传输到 Python（支持 byte、uint2、int4、int8、real；单通道及彩色）
- 通过 `Py_GetOutput` 捕获 Python 的 `stdout` / `stderr` 输出
- 所有调用共享同一工作空间（全局 Python `dict`）

---

## 环境要求

| 依赖项 | 说明 |
|---|---|
| [MVTec HALCON](https://www.mvtec.com/halcon) | 需设置 `HALCONROOT` 和 `HALCONEXAMPLES` 环境变量 |
| CMake ≥ 4.1.1 | |
| Python 3.x + 开发库 | **系统安装**即可，不限版本（3.10 / 3.11 / 3.12 …） |
| | Windows: 安装 Python 时勾选 "Add Python to PATH" |
| | Linux: `apt install python3-dev` |
| 编译器 | Windows: MSVC（VS 2019+）；Linux: GCC / Clang |

> **Python 版本无关**：扩展使用 Python 稳定 ABI（`Py_LIMITED_API`），编译后可在任意 Python 3.x 上运行，换版本无需重编译。

---

## 目录结构

```
Halcon_Python/
├── source/
│   ├── Halcon_Python.cpp   # 核心 C++ 实现
│   └── Halcon_Python.c     # C 桥接层
├── include/
│   └── Halcon_Python.h     # 公共 C API 头文件
├── def/
│   └── Halcon_Python.def   # HALCON 算子定义
├── examples/
│   ├── python_example.hdev
│   ├── python_image_example.hdev
│   └── python_add3_u16_example.hdev
├── CMakeLists.txt
├── LICENSE
└── README.md
```

> `build/` `bin/` `doc/html/` 为构建产物，已加入 `.gitignore`。

---

## 构建

> **前置条件**：确保已设置 `HALCONROOT` 和 `HALCONEXAMPLES` 环境变量。
> 系统需安装 **Python 3.x + 开发库**（CMake 会自动检测）。

### Windows

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux

```bash
cmake -B build -S .
cmake --build build
```

输出文件位于 `bin/` 目录。CMake 通过 `find_package(Python3)` 自动查找系统 Python。

> **注意**：Windows 上 CMake 会优先链接 `python3.lib`（稳定 ABI → `python3.dll`），
> 确保扩展可在任意 Python 3.x 版本上运行，而非绑定编译时的特定版本。

---

## 安装

构建成功后，将扩展包注册到 HALCON：

1. 将 `bin/` 目录的内容复制到 HALCON 扩展包目录，或
2. 通过 `HALCONEXTENSIONS` 环境变量 / HALCON 设置将 HALCON 指向 `bin/` 目录。

HALCON 在下次启动时将自动识别新算子（`Py_Initialize`、`Py_EvalString` 等）。

---

## API 参考

| HDevelop 算子 | 说明 |
|---|---|
| `Py_Initialize()` | 启动内嵌 Python 解释器 |
| `Py_Finalize()` | 关闭解释器并释放资源 |
| `Py_EvalString(Code)` | 在共享工作空间中执行 Python 代码字符串 |
| `Py_Import(ModuleName)` | 导入 Python 模块（`'numpy'`、`'cv2'` 等） |
| `Py_SetScalar(Name, Value)` | 将 int / float / string 写入 Python 工作空间 |
| `Py_GetScalar(Name, Value)` | 从 Python 工作空间读取 int / float / string |
| `Py_SetArray(Rows, Cols, Name, Values)` | 将 M×N 矩阵作为 NumPy 数组发送到 Python |
| `Py_GetArray(Name, Rows, Cols, Values)` | 将 NumPy 数组作为扁平值元组接收回来 |
| `Py_SetVariable(DictHandle)` | 批量将 HALCON MatrixID 发送到 Python |
| `Py_GetVariable(DictHandle)` | 批量将 Python 数组接收到 HALCON MatrixID |
| `Py_SetImage(ImageName, Image)` | 将 HALCON 图像作为 NumPy 数组发送到 Python |
| `Py_GetImage(ImageName, Image)` | 将 NumPy 数组作为 HALCON 图像接收回来 |
| `Py_GetOutput(Buffersize, Output)` | 捕获最新的 Python stdout / stderr 输出 |

### 图像约定

| 通道数 | NumPy 形状 | 像素顺序 |
|---|---|---|
| 1（灰度） | `(H, W)` | — |
| 3（彩色） | `(H, W, 3)` | BGR（OpenCV 约定） |

---

## 示例

### IV 曲线拟合（`examples/python_example.hdev`）

```hdevelop
* 1. 启动解释器
Py_Initialize ()

* 2. 导入库
Py_EvalString ('import numpy as np')
Py_EvalString ('from scipy.optimize import curve_fit')

* 3. 发送测量数据
Py_SetArray (|U|, 1, 'U', real(U))
Py_SetArray (|I|, 1, 'I', real(I))

* 4. 在 Python 中运行拟合
Py_EvalString ('def model(x, a, b, c): return 10.675 - a*np.exp(x/(b*0.0256)) - x/c')
Py_EvalString ('popt, _ = curve_fit(model, U.flatten(), I.flatten(), p0=[0.5,1.5,10])')
Py_EvalString ('CC = popt')

* 5. 获取结果
Py_GetArray ('CC', M, N, VAL)
I0  := VAL[0]
Nid := VAL[1]
RSH := VAL[2]
```

### 图像处理（`examples/python_image_example.hdev`）

```hdevelop
Py_Initialize ()
Py_EvalString ('import numpy as np')
Py_EvalString ('import cv2')

* 将 HALCON 图像发送到 Python
Py_SetImage ('img', Image)

* 在 Python 中处理（例如 Canny 边缘检测）
Py_EvalString ('edges = cv2.Canny(img, 100, 200)')

* 将结果作为 HALCON 图像接收回来
Py_GetImage ('edges', EdgeImage)
```

---

## 许可证

MIT 许可证 — 详见 [LICENSE](LICENSE)。
