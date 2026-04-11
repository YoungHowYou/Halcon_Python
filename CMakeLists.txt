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
set(HALCON_LIB_DIR      ${HALCON_ROOT}/lib/x64-win64)

# ── Embedded Python paths (3rd/python) ───────────────────────────────────────
set(PYTHON_ROOT         "${CMAKE_CURRENT_SOURCE_DIR}/3rd/python")
set(PYTHON_INCLUDE_DIR  "${PYTHON_ROOT}/include")
set(PYTHON_LIBRARY_DIR  "${PYTHON_ROOT}/libs")

message(STATUS "Python include: ${PYTHON_INCLUDE_DIR}")
message(STATUS "Python libs:    ${PYTHON_LIBRARY_DIR}")

# ── Additional project headers ────────────────────────────────────────────────
file(GLOB_RECURSE PROJECT_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
target_sources(${ProjectName} PRIVATE ${PROJECT_HEADERS})

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(${ProjectName}
    PUBLIC
        $<BUILD_INTERFACE:${HALCON_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PYTHON_INCLUDE_DIR}>
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_directories(${ProjectName} PRIVATE ${PYTHON_LIBRARY_DIR})

# Detect python3x.lib automatically
file(GLOB PYTHON_LIBS "${PYTHON_LIBRARY_DIR}/python3*.lib")
if(NOT PYTHON_LIBS)
    message(WARNING "No python3*.lib found in ${PYTHON_LIBRARY_DIR}")
endif()

target_link_libraries(${ProjectName}
    ${HALCON_LIB_DIR}/halconcpp.lib
    ${PYTHON_LIBS}
)

# ── Compiler options ──────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(${ProjectName} PRIVATE /W3 /bigobj)
    set_property(TARGET ${ProjectName} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

message(STATUS "Halcon_Python extension configured.")
