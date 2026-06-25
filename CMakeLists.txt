# CMakeLists.txt for HALCON Python Extension Package
# Mirrors Halcon_Matlab in structure — Python replaces the Matlab engine.

option(HALCON_XL "Use HALCON XL" OFF)
option(HALCON_DOTNET "Build .NET support" OFF)

if(HALCON_DOTNET)
  cmake_minimum_required(VERSION 4.1.1)
  set(_CSHARP CSharp)
else()
  cmake_minimum_required(VERSION 4.1.1)
endif()

if(POLICY CMP0068)
  cmake_policy(SET CMP0068 NEW)
endif()

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(Halcon_Python C CXX)

set(ProjectName Halcon_Python)
set(CMAKE_BUILD_TYPE Debug)

# ── Output directories (no Debug/Release sub-folders) ────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY          "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG    "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE  "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY          "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG    "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE  "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY          "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG    "${CMAKE_SOURCE_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE  "${CMAKE_SOURCE_DIR}/bin")

# ── HALCON cmake helpers ──────────────────────────────────────────────────────
file(TO_CMAKE_PATH "$ENV{HALCONEXAMPLES}" HALCONEXAMPLES)
list(APPEND CMAKE_MODULE_PATH "${HALCONEXAMPLES}/cmake")
include(UseHALCON)

# ── Create the HALCON extension package ──────────────────────────────────────
HALCON_AddExtensionPackage(Halcon_Python
  DEF_FILES
    def/Halcon_Python.def
  SOURCES
    source/Halcon_Python.c
    source/Halcon_Python.cpp
  CHAPTERS
    userextensions
  CLASSES
    Python
  OPERATORS
    Py_Initialize(Hproc_handle proc_handle);
    Py_Finalize(Hproc_handle proc_handle);
    Py_EvalString(Hproc_handle proc_handle);
    Py_Import(Hproc_handle proc_handle);
    Py_SetScalar(Hproc_handle proc_handle);
    Py_GetScalar(Hproc_handle proc_handle);
    Py_SetArray(Hproc_handle proc_handle);
    Py_GetArray(Hproc_handle proc_handle);
    Py_SetVariable(Hproc_handle proc_handle);
    Py_GetVariable(Hproc_handle proc_handle);
    Py_GetOutput(Hproc_handle proc_handle);
    Py_SetImage(Hproc_handle proc_handle);
    Py_GetImage(Hproc_handle proc_handle);
)

# ── HALCON paths ──────────────────────────────────────────────────────────────
set(HALCON_ROOT         $ENV{HALCONROOT})
set(HALCON_INCLUDE_DIR  ${HALCON_ROOT}/include/halconcpp)

# Platform-dependent HALCON library path and naming
if(WIN32)
    set(HALCON_ARCH_DIR      x64-win64)
    set(HALCON_LIB_SUFFIX    .lib)
elseif(UNIX)
    set(HALCON_ARCH_DIR      x64-linux)
    set(HALCON_LIB_SUFFIX    "")
endif()
set(HALCON_LIB_DIR  ${HALCON_ROOT}/lib/${HALCON_ARCH_DIR})

# ── Python — find system Python 3.x (cross-platform) ─────────────────────────
# Uses the stable ABI (Py_LIMITED_API) so any Python 3.x version works.
find_package(Python3 REQUIRED COMPONENTS Development)
message(STATUS "Python version:  ${Python3_VERSION}")
message(STATUS "Python include:  ${Python3_INCLUDE_DIRS}")

# On Windows, force python3.lib (stable ABI → python3.dll) instead of the
# version-specific python3xx.lib that find_package returns.
if(WIN32)
    set(PYTHON_LIB "${Python3_LIBRARY_DIRS}/python3.lib")
    if(NOT EXISTS "${PYTHON_LIB}")
        message(WARNING "python3.lib not found, falling back to ${Python3_LIBRARIES}")
        message(WARNING "The extension will be tied to Python ${Python3_VERSION}")
        set(PYTHON_LIB "${Python3_LIBRARIES}")
    endif()
else()
    set(PYTHON_LIB "${Python3_LIBRARIES}")
endif()
message(STATUS "Python lib:     ${PYTHON_LIB}")

# ── Additional project headers ────────────────────────────────────────────────
file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(${ProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${Python3_INCLUDE_DIRS}>
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(${ProjectName}
    ${HALCON_LIB_DIR}/halconcpp${HALCON_LIB_SUFFIX}
    ${PYTHON_LIB}
)

# ── Compiler options ──────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${ProjectName} PRIVATE /W3 /bigobj)
    set_property(TARGET ${ProjectName} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
elseif(UNIX)
    target_compile_options(${ProjectName} PRIVATE -Wall -fPIC)
endif()

message(STATUS "Halcon_Python extension configured.")
