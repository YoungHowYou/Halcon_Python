# Halcon_Python

A HALCON Extension Package that embeds a Python interpreter inside HALCON, enabling HDevelop programs to execute Python code and exchange data (scalars, arrays, images) bidirectionally between HALCON and the Python scientific ecosystem (NumPy, SciPy, matplotlib, OpenCV, etc.).

The API mirrors the existing **Halcon_Matlab** extension — only the engine calls change.

---

## Features

- Execute arbitrary Python code from HDevelop via `Py_EvalString`
- Import Python modules (`numpy`, `scipy`, `cv2`, etc.) with `Py_Import`
- Transfer scalars (int / float / string) in both directions
- Transfer matrices / arrays as NumPy arrays
- Transfer HALCON images to Python as NumPy arrays (byte, uint2, int4, int8, real; mono & color)
- Capture Python `stdout` / `stderr` with `Py_GetOutput`
- Single shared workspace (global Python `dict`) across all calls

---

## Prerequisites

| Requirement | Notes |
|---|---|
| [MVTec HALCON](https://www.mvtec.com/halcon) | `HALCONROOT` and `HALCONEXAMPLES` env vars must be set |
| CMake ≥ 4.1.1 | |
| MSVC (Visual Studio 2019+) | Tested on Windows x64 |
| Embedded Python 3.x | Included under `3rd/python/` (headers + import libs) |

---

## Directory Structure

```
Halcon_Python/
├── source/
│   ├── Halcon_Python.cpp   # Core C++ implementation (~1000 lines)
│   └── Halcon_Python.c     # Thin C bridge layer
├── include/
│   └── Halcon_Python.h     # Public C API header
├── def/
│   └── Halcon_Python.def   # HALCON operator definitions (EN/DE)
├── examples/
│   ├── python_example.hdev         # IV-curve fitting with scipy
│   └── python_image_example.hdev   # Image transfer demo
├── help/                   # Pre-built HALCON help files
├── doc/html/               # HTML reference documentation
├── 3rd/python/             # Embedded Python distribution
│   ├── include/            # Python C headers
│   └── libs/               # python3x.lib import libraries
├── bin/                    # Build output (DLL + registration files)
├── build/                  # CMake build directory
├── CMakeLists.txt
├── LICENSE
└── README.md
```

---

## Build

```bash
# 1. Create and enter the build directory
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# 2. Build (Debug or Release)
cmake --build build --config Debug
```

Output is placed in `bin/`. CMake auto-detects `python3x.lib` from `3rd/python/libs/`.

> **Note:** `HALCONROOT` and `HALCONEXAMPLES` must be set before running CMake.

---

## Installation

After a successful build, register the extension package with HALCON:

1. Copy the contents of `bin/` to your HALCON extension package directory, or
2. Point HALCON to `bin/` via the `HALCONEXTENSIONS` environment variable / HALCON preferences.

HALCON will pick up the new operators (`Py_Initialize`, `Py_EvalString`, …) on the next start.

---

## API Reference

| HDevelop Operator | Description |
|---|---|
| `Py_Initialize()` | Start the embedded Python interpreter |
| `Py_Finalize()` | Shut down the interpreter and free resources |
| `Py_EvalString(Code)` | Execute a Python code string in the shared workspace |
| `Py_Import(ModuleName)` | Import a Python module (`'numpy'`, `'cv2'`, …) |
| `Py_SetScalar(Name, Value)` | Write an int / float / string into the Python workspace |
| `Py_GetScalar(Name, Value)` | Read an int / float / string from the Python workspace |
| `Py_SetArray(Rows, Cols, Name, Values)` | Send an M×N matrix to Python as a NumPy array |
| `Py_GetArray(Name, Rows, Cols, Values)` | Receive a NumPy array back as a flat value tuple |
| `Py_SetVariable(DictHandle)` | Batch-send HALCON MatrixIDs to Python |
| `Py_GetVariable(DictHandle)` | Batch-receive Python arrays into HALCON MatrixIDs |
| `Py_SetImage(ImageName, Image)` | Send a HALCON image to Python as a NumPy array |
| `Py_GetImage(ImageName, Image)` | Receive a NumPy array back as a HALCON image |
| `Py_GetOutput(Buffersize, Output)` | Capture the latest Python stdout / stderr |

### Image conventions

| Channels | NumPy shape | Pixel order |
|---|---|---|
| 1 (mono) | `(H, W)` | — |
| 3 (color) | `(H, W, 3)` | BGR (OpenCV convention) |

---

## Examples

### IV-Curve Fitting (`examples/python_example.hdev`)

```hdevelop
* 1. Start interpreter
Py_Initialize ()

* 2. Import libraries
Py_EvalString ('import numpy as np')
Py_EvalString ('from scipy.optimize import curve_fit')

* 3. Send measurement data
Py_SetArray (|U|, 1, 'U', real(U))
Py_SetArray (|I|, 1, 'I', real(I))

* 4. Run fitting in Python
Py_EvalString ('def model(x, a, b, c): return 10.675 - a*np.exp(x/(b*0.0256)) - x/c')
Py_EvalString ('popt, _ = curve_fit(model, U.flatten(), I.flatten(), p0=[0.5,1.5,10])')
Py_EvalString ('CC = popt')

* 5. Retrieve result
Py_GetArray ('CC', M, N, VAL)
I0  := VAL[0]
Nid := VAL[1]
RSH := VAL[2]
```

### Image Processing (`examples/python_image_example.hdev`)

```hdevelop
Py_Initialize ()
Py_EvalString ('import numpy as np')
Py_EvalString ('import cv2')

* Send HALCON image to Python
Py_SetImage ('img', Image)

* Process in Python (e.g., Canny edge detection)
Py_EvalString ('edges = cv2.Canny(img, 100, 200)')

* Retrieve result as HALCON image
Py_GetImage ('edges', EdgeImage)
```

---

## License

MIT License — see [LICENSE](LICENSE) for details.
