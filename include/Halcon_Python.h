#pragma once
#ifndef HALCON_PYTHON_H
#define HALCON_PYTHON_H

#ifndef __APPLE__
#include "Halcon.h"
#else
#  ifndef HC_LARGE_IMAGES
#    include <HALCON/Halcon.h>
#  else
#    include <HALCONxl/Halcon.h>
#  endif
#endif

#define HPy_EXPORTS_API __declspec(dllexport)

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * HALCON Python Extension Package
 *
 * API mirrors the Halcon_Matlab extension:
 *   Py_Initialize      ↔  Matlab_engOpen
 *   Py_Finalize        ↔  Matlab_engClose
 *   Py_EvalString      ↔  Matlab_engEvalString
 *   Py_SetArray        ↔  Matlab_engSetmxArray
 *   Py_GetArray        ↔  Matlab_engGetmxArray
 *   Py_SetVariable     ↔  Matlab_engPutVariable  (dict of MatrixIDs)
 *   Py_GetVariable     ↔  Matlab_engGetVariable  (dict of MatrixIDs)
 *   Py_GetOutput       ↔  Matlab_engOutputBuffer
 *   Py_SetScalar       —  set int/float/string scalar
 *   Py_GetScalar       —  get int/float/string scalar
 *   Py_Import          —  import a Python module
 * ========================================================================= */

extern HPy_EXPORTS_API Herror HPy_Initialize(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_Finalize(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_EvalString(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_Import(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_SetScalar(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_GetScalar(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_SetArray(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_GetArray(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_SetVariable(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_GetVariable(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_GetOutput(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_SetImage(Hproc_handle proc_handle);
extern HPy_EXPORTS_API Herror HPy_GetImage(Hproc_handle proc_handle);

#ifdef __cplusplus
}
#endif

#endif /* HALCON_PYTHON_H */
