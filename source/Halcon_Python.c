/* Halcon_Python.c
 * C bridge layer — forwards every CHalcon operator call to its C++ implementation.
 * Pattern identical to Halcon_Matlab: C stub → C++ body. */

#include "Halcon_Python.h"

Herror CPy_Initialize(Hproc_handle proc_handle)  { return HPy_Initialize(proc_handle);  }
Herror CPy_Finalize(Hproc_handle proc_handle)    { return HPy_Finalize(proc_handle);    }
Herror CPy_EvalString(Hproc_handle proc_handle)  { return HPy_EvalString(proc_handle);  }
Herror CPy_Import(Hproc_handle proc_handle)      { return HPy_Import(proc_handle);      }
Herror CPy_SetScalar(Hproc_handle proc_handle)   { return HPy_SetScalar(proc_handle);   }
Herror CPy_GetScalar(Hproc_handle proc_handle)   { return HPy_GetScalar(proc_handle);   }
Herror CPy_SetArray(Hproc_handle proc_handle)    { return HPy_SetArray(proc_handle);    }
Herror CPy_GetArray(Hproc_handle proc_handle)    { return HPy_GetArray(proc_handle);    }
Herror CPy_SetVariable(Hproc_handle proc_handle) { return HPy_SetVariable(proc_handle); }
Herror CPy_GetVariable(Hproc_handle proc_handle) { return HPy_GetVariable(proc_handle); }
Herror CPy_GetOutput(Hproc_handle proc_handle)   { return HPy_GetOutput(proc_handle);   }
Herror CPy_SetImage(Hproc_handle proc_handle)        { return HPy_SetImage(proc_handle);        }
Herror CPy_GetImage(Hproc_handle proc_handle)        { return HPy_GetImage(proc_handle);        }
Herror CPy_SetPythonTuple(Hproc_handle proc_handle)  { return HPy_SetPythonTuple(proc_handle);  }
Herror CPy_GetPythonTuple(Hproc_handle proc_handle)  { return HPy_GetPythonTuple(proc_handle);  }
Herror CPy_SetPythonObject(Hproc_handle proc_handle) { return HPy_SetPythonObject(proc_handle); }
Herror CPy_GetPythonObject(Hproc_handle proc_handle) { return HPy_GetPythonObject(proc_handle); }
