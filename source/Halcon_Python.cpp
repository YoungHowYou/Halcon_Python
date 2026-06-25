/* Halcon_Python.cpp
 *
 * Core implementation of the HALCON Python extension package.
 *
 * Design mirrors Halcon_Matlab.cpp:
 *   Py_Initialize / Py_Finalize   — interpreter lifecycle
 *   Py_EvalString                 — run arbitrary Python code
 *   Py_Import                     — import a module
 *   Py_SetScalar / Py_GetScalar   — transfer single values
 *   Py_SetArray  / Py_GetArray    — transfer M×N matrices  (= Matlab_engSetmxArray / GetmxArray)
 *   Py_SetVariable               — send dict of HALCON MatrixIDs to Python (= Matlab_engPutVariable)
 *   Py_GetVariable               — read Python arrays back into HALCON MatrixIDs (= Matlab_engGetVariable)
 *   Py_GetOutput                  — capture Python stdout            (= Matlab_engOutputBuffer)
 *
 * All array data in Python lives in a single shared globals dict (g_globals),
 * analogous to the Matlab workspace.  NumPy is used for matrix storage; no
 * NumPy C-API headers are needed — numpy is driven via PyRun_String commands.
 */

#define PY_SSIZE_T_CLEAN
#define Py_LIMITED_API 0x030a0000   /* Python 3.10 stable ABI — portable across 3.x */
#include <Python.h>

#ifdef _WIN32
#include "windows.h"
#endif

#include "Halcon.h"
#include "HalconCpp.h"
#include "HDevThread.h"
#include "Halcon_Python.h"

#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

#ifdef _WIN32
/* DllMain — Windows DLL entry point.  Linux shared objects don't need this. */
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    (void)hInst;
    return TRUE;
}
#endif

using namespace HalconCpp;

/* =========================================================================
 * Global interpreter state
 * ========================================================================= */

static PyObject* g_globals     = nullptr;   // Python globals dict (the "workspace")
static bool      g_initialized = false;

static PyObject* Globals() { return g_globals; }

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* Run a Python statement in the shared globals dict.
 * Returns true on success; prints error + returns false on failure. */
static bool PyExec(const char* code)
{
    PyObject* co = Py_CompileString(code, "<string>", Py_file_input);
    if (!co) { PyErr_Print(); return false; }
    PyObject* r = PyEval_EvalCode(co, Globals(), Globals());
    Py_DECREF(co);
    if (!r) { PyErr_Print(); return false; }
    Py_DECREF(r);
    return true;
}

/* Push a flat C double array into Python as a numpy array called __tmp_arr__,
 * then reshape it to (M, N) and store under `name`. */
static Herror PushArray(Hproc_handle proc_handle,
                         const char* name,
                         INT4_8 M, INT4_8 N,
                         const double* data, INT4_8 count)
{
    /* Build Python list in globals */
    PyObject* lst = PyList_New((Py_ssize_t)count);
    for (Py_ssize_t i = 0; i < (Py_ssize_t)count; i++)
        PyList_SetItem(lst, i, PyFloat_FromDouble(data[i]));

    PyDict_SetItemString(Globals(), "__tmp_arr__", lst);
    Py_DECREF(lst);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "import numpy as np\n"
        "%s = np.array(__tmp_arr__, dtype=float).reshape(%lld, %lld)\n"
        "del __tmp_arr__",
        name, (long long)M, (long long)N);

    if (!PyExec(cmd)) return H_ERR_WIPV2;
    return H_MSG_TRUE;
}

/* Retrieve a Python variable as flat doubles.  Shape is returned in M, N.
 * For 1-D arrays  → M=1, N=len.
 * For 0-D scalars → M=1, N=1.
 * Data is appended into `out` (std::vector). */
static Herror PullArray(const char* name,
                         INT4_8& M, INT4_8& N,
                         std::vector<double>& out)
{
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "import numpy as np\n"
        "__tmp_a__ = np.asarray(%s, dtype=float)\n"
        "__tmp_flat__  = __tmp_a__.flatten().tolist()\n"
        "__tmp_shape__ = list(__tmp_a__.shape)\n"
        "del __tmp_a__",
        name);

    if (!PyExec(cmd)) return H_ERR_WIPV2;

    /* --- shape --- */
    M = 1; N = 1;
    PyObject* sh = PyDict_GetItemString(Globals(), "__tmp_shape__");
    if (sh && PyList_Check(sh)) {
        Py_ssize_t ndim = PyList_Size(sh);
        if (ndim == 0) { M = 1; N = 1; }
        else if (ndim == 1) {
            M = 1;
            N = PyLong_AsLongLong(PyList_GetItem(sh, 0));
        } else {
            M = PyLong_AsLongLong(PyList_GetItem(sh, 0));
            N = PyLong_AsLongLong(PyList_GetItem(sh, 1));
        }
    }

    /* --- flat data --- */
    PyObject* fl = PyDict_GetItemString(Globals(), "__tmp_flat__");
    if (!fl || !PyList_Check(fl)) {
        PyExec("del __tmp_flat__, __tmp_shape__");
        return H_ERR_WIPV2;
    }
    Py_ssize_t total = PyList_Size(fl);
    out.resize((size_t)total);
    for (Py_ssize_t i = 0; i < total; i++)
        out[(size_t)i] = PyFloat_AsDouble(PyList_GetItem(fl, i));

    PyExec("del __tmp_flat__, __tmp_shape__");
    return H_MSG_TRUE;
}

/* =========================================================================
 * Operator implementations
 * ========================================================================= */

/* -------------------------------------------------------------------------
 * Py_Initialize  ↔  Matlab_engOpen
 * ------------------------------------------------------------------------- */
Herror HPy_Initialize(Hproc_handle proc_handle)
{
    if (g_initialized) return H_MSG_TRUE;

    Py_Initialize();
    g_initialized = true;

    /* Create shared globals dict with builtins */
    PyObject* builtins = PyImport_ImportModule("builtins");
    g_globals = PyDict_New();
    PyDict_SetItemString(g_globals, "__builtins__", builtins);
    Py_XDECREF(builtins);

    /* Redirect stdout AND stderr → StringIO so Py_GetOutput captures both
     * normal output and error tracebacks. */
    PyExec(
        "import io as _io, sys as _sys\n"
        "_py_buf = _io.StringIO()\n"
        "_py_stdout_orig = _sys.stdout\n"
        "_py_stderr_orig = _sys.stderr\n"
        "_sys.stdout = _py_buf\n"
        "_sys.stderr = _py_buf\n"
        "import numpy as np\n"          /* pre-import numpy for array ops */
    );

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_Finalize  ↔  Matlab_engClose
 * ------------------------------------------------------------------------- */
Herror HPy_Finalize(Hproc_handle proc_handle)
{
    if (!g_initialized) return H_MSG_TRUE;

    /* Restore stdout + stderr before shutting down */
    if (g_globals) {
        PyExec("_sys.stdout = _py_stdout_orig\n_sys.stderr = _py_stderr_orig");
        Py_DECREF(g_globals);
        g_globals = nullptr;
    }

    Py_Finalize();
    g_initialized = false;
    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_EvalString  ↔  Matlab_engEvalString
 * ------------------------------------------------------------------------- */
Herror HPy_EvalString(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024 * 512);   /* up to 512 KB code */
    Hcpar PyCode;
    HGetSPar(proc_handle, 1, STRING_PAR, &PyCode, 1);

    if (!PyExec(PyCode.par.s)) return H_ERR_WIPV2;
    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_Import
 * Accepts:  "numpy"  →  runs  "import numpy"
 *           "numpy as np"  →  runs  "import numpy as np"
 * ------------------------------------------------------------------------- */
Herror HPy_Import(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);
    Hcpar ModuleName;
    HGetSPar(proc_handle, 1, STRING_PAR, &ModuleName, 1);

    char cmd[1024];
    /* If user already wrote the full statement keep it, otherwise prepend "import " */
    if (strncmp(ModuleName.par.s, "import ", 7) == 0 ||
        strncmp(ModuleName.par.s, "from ",   5) == 0) {
        snprintf(cmd, sizeof(cmd), "%s", ModuleName.par.s);
    } else {
        snprintf(cmd, sizeof(cmd), "import %s", ModuleName.par.s);
    }

    if (!PyExec(cmd)) return H_ERR_WIPV2;
    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_SetScalar  — set int / float / string
 * ------------------------------------------------------------------------- */
Herror HPy_SetScalar(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);
    Hcpar Name, Value;
    HGetSPar(proc_handle, 1, STRING_PAR, &Name,  1);
    HGetSPar(proc_handle, 2, UNDEF_PAR,  &Value, 1);

    PyObject* py_val = nullptr;
    switch (Value.type) {
        case LONG_PAR:   py_val = PyLong_FromLongLong(Value.par.l);       break;
        case DOUBLE_PAR: py_val = PyFloat_FromDouble(Value.par.d);        break;
        case STRING_PAR: py_val = PyUnicode_FromString(Value.par.s);      break;
        default:         return H_ERR_WIPV2;
    }

    PyDict_SetItemString(Globals(), Name.par.s, py_val);
    Py_DECREF(py_val);
    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_GetScalar  — get int / float / string
 * ------------------------------------------------------------------------- */
Herror HPy_GetScalar(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);
    Hcpar Name;
    HGetSPar(proc_handle, 1, STRING_PAR, &Name, 1);

    PyObject* val = PyDict_GetItemString(Globals(), Name.par.s);
    if (!val) return H_ERR_WIPV2;   /* variable not found */

    if (PyBool_Check(val)) {
        /* bool before int — bool is a subclass of int in Python */
        INT4_8 l = (val == Py_True) ? 1 : 0;
        HPutElem(proc_handle, 1, &l, 1, LONG_PAR);
    } else if (PyLong_Check(val)) {
        INT4_8 l = PyLong_AsLongLong(val);
        HPutElem(proc_handle, 1, &l, 1, LONG_PAR);
    } else if (PyFloat_Check(val)) {
        double d = PyFloat_AsDouble(val);
        HPutElem(proc_handle, 1, &d, 1, DOUBLE_PAR);
    } else {
        /* String or anything else → convert to str */
        PyObject* s_obj = PyObject_Str(val);
        const char* s = PyUnicode_AsUTF8AndSize(s_obj, NULL);
        char* tmp;
        HAllocTmp(proc_handle, &tmp, (Hlong)(strlen(s) + 1));
        strcpy(tmp, s);
        HPutElem(proc_handle, 1, &tmp, 1, STRING_PAR);
        Py_DECREF(s_obj);
    }

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_SetArray  ↔  Matlab_engSetmxArray
 *   HDevelop:  Py_SetArray (|I|, 1, 'I', real(I))
 * ------------------------------------------------------------------------- */
Herror HPy_SetArray(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    Hcpar M, N, Name;
    HGetSPar(proc_handle, 1, LONG_PAR,   &M,    1);
    HGetSPar(proc_handle, 2, LONG_PAR,   &N,    1);
    HGetSPar(proc_handle, 3, STRING_PAR, &Name, 1);

    const Hcpar*  VAL;
    INT4_8  num;
    HGetPPar(proc_handle, 4, &VAL, &num);

    if (num != M.par.l * N.par.l) return H_ERR_WIPT4;

    /* Convert to double array (HALCON coerces int→double via type_list: real) */
    std::vector<double> data((size_t)num);
    for (INT4_8 i = 0; i < num; i++) {
        data[(size_t)i] = (VAL[i].type == DOUBLE_PAR)
                          ? VAL[i].par.d
                          : (double)VAL[i].par.l;
    }

    return PushArray(proc_handle, Name.par.s, M.par.l, N.par.l, data.data(), num);
}

/* -------------------------------------------------------------------------
 * Py_GetArray  ↔  Matlab_engGetmxArray
 *   HDevelop:  Py_GetArray ('CC', M, N, VAL)
 * ------------------------------------------------------------------------- */
Herror HPy_GetArray(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    Hcpar Name;
    HGetSPar(proc_handle, 1, STRING_PAR, &Name, 1);

    INT4_8 M, N;
    std::vector<double> data;

    Herror err = PullArray(Name.par.s, M, N, data);
    if (err != H_MSG_TRUE) return err;

    INT4_8 total = (INT4_8)data.size();

    /* Allocate HALCON-managed output buffer for VAL */
    double* out_buf;
    HAllocTmp(proc_handle, &out_buf, (Hlong)(total * sizeof(double)));
    for (INT4_8 i = 0; i < total; i++) out_buf[i] = data[(size_t)i];

    HPutElem(proc_handle, 1, &M,       1,     LONG_PAR);
    HPutElem(proc_handle, 2, &N,       1,     LONG_PAR);
    HPutElem(proc_handle, 3, out_buf,  total, DOUBLE_PAR);

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_SetVariable  ↔  Matlab_engPutVariable
 *
 * DictHandle contains {varName: MatrixID, ...}.
 * Each HALCON matrix is read and sent to Python as a numpy array.
 *
 * HDevelop usage:
 *   create_matrix (M, N, Values, MatrixID)
 *   set_dict_tuple (PyDict, 'A', MatrixID)
 *   Py_SetVariable (PyDict)
 * ------------------------------------------------------------------------- */
Herror HPy_SetVariable(Hproc_handle proc_handle)
{
    const Hcpar*  dict_par;
    INT4_8  num;
    HGetPPar(proc_handle, 1, &dict_par, &num);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    HTuple keys;
    GetDictParam(hv_Dict, "keys", HTuple(), &keys);

    for (Hlong i = 0; i < keys.Length(); i++) {
        HTuple key(keys[i]);

        HTuple MatrixID;
        GetDictTuple(hv_Dict, key, &MatrixID);

        HTuple hv_M, hv_N, values;
        GetSizeMatrix(MatrixID, &hv_M, &hv_N);
        GetFullMatrix(MatrixID, &values);

        INT4_8 M = hv_M.L(), N = hv_N.L();
        INT4_8 total = M * N;

        std::vector<double> data((size_t)total);
        for (INT4_8 j = 0; j < total; j++)
            data[(size_t)j] = values[j].D();

        PushArray(proc_handle, key.S().Text(), M, N, data.data(), total);
    }

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_GetVariable  ↔  Matlab_engGetVariable
 *
 * DictHandle contains {varName: placeholder, ...}.
 * Each named Python variable is read back and stored as a MatrixID in the dict.
 *
 * HDevelop usage:
 *   create_dict (PyDict)
 *   set_dict_tuple (PyDict, 'popt', 0)   * placeholder
 *   Py_GetVariable (PyDict)
 *   get_dict_tuple (PyDict, 'popt', MatrixID)
 *   get_full_matrix (MatrixID, Values)
 * ------------------------------------------------------------------------- */
Herror HPy_GetVariable(Hproc_handle proc_handle)
{
    const Hcpar*  dict_par;
    INT4_8  num;
    HGetPPar(proc_handle, 1, &dict_par, &num);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    HTuple keys;
    GetDictParam(hv_Dict, "keys", HTuple(), &keys);

    for (Hlong i = 0; i < keys.Length(); i++) {
        HTuple key(keys[i]);
        const char* name = key.S();
        if (!name || !*name) continue;

        INT4_8 M, N;
        std::vector<double> data;
        if (PullArray(name, M, N, data) != H_MSG_TRUE) continue;

        HTuple values;
        for (size_t j = 0; j < data.size(); j++)
            values.Append(data[j]);

        HTuple MatrixID;
        CreateMatrix(M, N, values, &MatrixID);
        SetDictTuple(hv_Dict, key, MatrixID);
    }

    return H_MSG_TRUE;
}

/* =========================================================================
 * Image transfer helpers
 * ========================================================================= */

/* Map a HALCON image type name to the matching ctypes and numpy dtype strings.
 * Returns false for unknown types (falls back to byte/uint8). */
static void HalconTypeToCTypes(const char* halcon_type,
                                const char*& ctype_str,
                                const char*& dtype_str)
{
    if      (strcmp(halcon_type, "byte")  == 0) { ctype_str = "c_uint8";  dtype_str = "uint8";   }
    else if (strcmp(halcon_type, "uint2") == 0) { ctype_str = "c_uint16"; dtype_str = "uint16";  }
    else if (strcmp(halcon_type, "int4")  == 0) { ctype_str = "c_int32";  dtype_str = "int32";   }
    else if (strcmp(halcon_type, "int8")  == 0) { ctype_str = "c_int64";  dtype_str = "int64";   }
    else if (strcmp(halcon_type, "real")  == 0) { ctype_str = "c_float";  dtype_str = "float32"; }
    else                                        { ctype_str = "c_uint8";  dtype_str = "uint8";   }
}

/* Map a numpy dtype string to a HALCON image type name. */
static const char* NumpyDtypeToHalcon(const char* dtype_str)
{
    if      (strcmp(dtype_str, "uint8")   == 0) return "byte";
    else if (strcmp(dtype_str, "uint16")  == 0) return "uint2";
    else if (strcmp(dtype_str, "int32")   == 0) return "int4";
    else if (strcmp(dtype_str, "int64")   == 0) return "int8";
    else if (strcmp(dtype_str, "float32") == 0) return "real";
    else if (strcmp(dtype_str, "float64") == 0) return "real"; /* converted below */
    else                                         return "byte";
}

/* -------------------------------------------------------------------------
 * SetImageInGlobals — copy one HALCON image into Python globals as numpy array.
 *
 * Single-channel → shape (H, W),         dtype matches HALCON pixel type.
 * 3-channel color → shape (H, W, 3) BGR, dtype matches HALCON pixel type.
 *   HALCON stores channels as R/G/B planes; we interleave as B/G/R (OpenCV).
 *
 * Data is copied into Python bytes objects in C++ via PyBytes_FromStringAndSize.
 * This avoids ctypes.from_address() which can fail on protected memory regions.
 * ------------------------------------------------------------------------- */
static size_t HalconElemSize(const char* halcon_type)
{
    if (strcmp(halcon_type, "uint2") == 0) return 2;
    if (strcmp(halcon_type, "int4")  == 0) return 4;
    if (strcmp(halcon_type, "int8")  == 0) return 8;
    if (strcmp(halcon_type, "real")  == 0) return 4;
    return 1; /* byte */
}

static Herror SetImageInGlobals(const char* varname, const HObject& hImage)
{
    try {
        HTuple numCh;
        CountChannels(hImage, &numCh);
        Hlong nch = numCh[0].L();
        bool is_color = (nch >= 3);
        HTuple imgType, width, height;

        if (!is_color) {
            /* ---- single channel ---- */
            HTuple ptr_t;
            GetImagePointer1(hImage, &ptr_t, &imgType, &width, &height);

            std::string itype(imgType[0].S().Text());
            INT4_8 W = width[0].L(), H = height[0].L();

            const char* ctype_unused; const char* dtype;
            HalconTypeToCTypes(itype.c_str(), ctype_unused, dtype);
            size_t esz  = HalconElemSize(itype.c_str());
            const char* raw = reinterpret_cast<const char*>(
                static_cast<uintptr_t>(ptr_t[0].L()));

            PyObject* py_bytes = PyBytes_FromStringAndSize(raw, (Py_ssize_t)(W * H * esz));
            PyObject* py_W     = PyLong_FromLongLong(W);
            PyObject* py_H     = PyLong_FromLongLong(H);
            PyDict_SetItemString(Globals(), "__ibytes__", py_bytes);
            PyDict_SetItemString(Globals(), "__iW__",     py_W);
            PyDict_SetItemString(Globals(), "__iH__",     py_H);
            Py_DECREF(py_bytes); Py_DECREF(py_W); Py_DECREF(py_H);

            char cmd[512];
            snprintf(cmd, sizeof(cmd),
                "import numpy as _np\n"
                "%s = _np.frombuffer(__ibytes__, dtype=_np.%s).reshape(__iH__, __iW__).copy()\n"
                "del __ibytes__, __iW__, __iH__",
                varname, dtype);
            if (!PyExec(cmd)) return H_ERR_WIPV2;

        } else {
            /* ---- 3-channel color: HALCON planes are R, G, B ---- */
            HTuple ptr1, ptr2, ptr3;
            GetImagePointer3(hImage, &ptr1, &ptr2, &ptr3, &imgType, &width, &height);

            std::string itype(imgType[0].S().Text());
            INT4_8 W = width[0].L(), H = height[0].L();

            const char* ctype_unused; const char* dtype;
            HalconTypeToCTypes(itype.c_str(), ctype_unused, dtype);
            size_t esz  = HalconElemSize(itype.c_str());
            size_t psz  = (size_t)(W * H) * esz;

            const char* rp = reinterpret_cast<const char*>(static_cast<uintptr_t>(ptr1[0].L()));
            const char* gp = reinterpret_cast<const char*>(static_cast<uintptr_t>(ptr2[0].L()));
            const char* bp = reinterpret_cast<const char*>(static_cast<uintptr_t>(ptr3[0].L()));

            PyObject* py_r = PyBytes_FromStringAndSize(rp, (Py_ssize_t)psz);
            PyObject* py_g = PyBytes_FromStringAndSize(gp, (Py_ssize_t)psz);
            PyObject* py_b = PyBytes_FromStringAndSize(bp, (Py_ssize_t)psz);
            PyObject* py_W = PyLong_FromLongLong(W);
            PyObject* py_H = PyLong_FromLongLong(H);
            PyDict_SetItemString(Globals(), "__ir__", py_r);
            PyDict_SetItemString(Globals(), "__ig__", py_g);
            PyDict_SetItemString(Globals(), "__ib__", py_b);
            PyDict_SetItemString(Globals(), "__iW__", py_W);
            PyDict_SetItemString(Globals(), "__iH__", py_H);
            Py_DECREF(py_r); Py_DECREF(py_g); Py_DECREF(py_b);
            Py_DECREF(py_W); Py_DECREF(py_H);

            char cmd[1024];
            snprintf(cmd, sizeof(cmd),
                "import numpy as _np\n"
                "__r = _np.frombuffer(__ir__, dtype=_np.%s).reshape(__iH__, __iW__)\n"
                "__g = _np.frombuffer(__ig__, dtype=_np.%s).reshape(__iH__, __iW__)\n"
                "__b = _np.frombuffer(__ib__, dtype=_np.%s).reshape(__iH__, __iW__)\n"
                "%s = _np.dstack((__b, __g, __r)).copy()\n"
                "del __ir__, __ig__, __ib__, __iW__, __iH__, __r, __g, __b",
                dtype, dtype, dtype, varname);
            if (!PyExec(cmd)) return H_ERR_WIPV2;
        }

    } catch (const HalconCpp::HException&) {
        return H_ERR_WIPV2;
    } catch (...) {
        return H_ERR_WIPV2;
    }

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * GetImageFromGlobals — create a HALCON image from a Python numpy array.
 *
 * Accepted shapes:
 *   (H, W)      → single-channel HALCON image
 *   (H, W, 1)   → single-channel HALCON image
 *   (H, W, 3)   → 3-channel HALCON image  (assumes BGR / OpenCV channel order)
 *
 * Accepted dtypes: uint8 → byte, uint16 → uint2, int32 → int4,
 *                  int64 → int8, float32/float64 → real
 * ------------------------------------------------------------------------- */
static Herror GetImageFromGlobals(const char* varname, HObject& hImageOut)
{
    /* Step 1: gather array metadata in Python */
    char setup[1024];
    snprintf(setup, sizeof(setup),
        "import numpy as _np\n"
        "__ia__ = _np.ascontiguousarray(%s)\n"
        "__idt__ = str(__ia__.dtype)\n"
        "__ind__ = int(__ia__.ndim)\n"
        "__ish__ = list(__ia__.shape)",
        varname);
    if (!PyExec(setup)) return H_ERR_WIPV2;

    PyObject* py_dt = PyDict_GetItemString(Globals(), "__idt__");
    PyObject* py_nd = PyDict_GetItemString(Globals(), "__ind__");
    PyObject* py_sh = PyDict_GetItemString(Globals(), "__ish__");

    if (!py_dt || !py_nd || !py_sh) {
        PyExec("del __ia__, __idt__, __ind__, __ish__");
        return H_ERR_WIPV2;
    }

    const char* dtype_str = PyUnicode_AsUTF8AndSize(py_dt, NULL);
    INT4_8 ndim  = PyLong_AsLongLong(py_nd);
    INT4_8 H_img = PyList_Size(py_sh) >= 1 ? PyLong_AsLongLong(PyList_GetItem(py_sh, 0)) : 1;
    INT4_8 W_img = PyList_Size(py_sh) >= 2 ? PyLong_AsLongLong(PyList_GetItem(py_sh, 1)) : 1;
    INT4_8 C_img = (ndim >= 3 && PyList_Size(py_sh) >= 3) ?
                   PyLong_AsLongLong(PyList_GetItem(py_sh, 2)) : 1;

    /* float64 → float32 (HALCON "real" is 32-bit) */
    if (strcmp(dtype_str, "float64") == 0) {
        PyExec("__ia__ = __ia__.astype(_np.float32, copy=False)");
        dtype_str = "float32";
    }
    const char* halcon_type = NumpyDtypeToHalcon(dtype_str);

    Herror err = H_MSG_TRUE;
    try {
        if (ndim == 2 || C_img == 1) {
            /* ---- single channel ---- */
            if (ndim == 3 && C_img == 1) {
                PyExec("__ia__ = _np.ascontiguousarray(__ia__[:,:,0])");
            }
            PyExec("__ibytes__ = __ia__.tobytes()");
            PyObject* py_bytes = PyDict_GetItemString(Globals(), "__ibytes__");
            if (!py_bytes) { err = H_ERR_WIPV2; goto cleanup; }

            const char* raw = PyBytes_AsString(py_bytes);
            HObject tmp1;
            GenImage1(&tmp1,
                      HTuple(halcon_type),
                      HTuple((Hlong)W_img), HTuple((Hlong)H_img),
                      HTuple((Hlong)(uintptr_t)raw));
            CopyImage(tmp1, &hImageOut);
            PyExec("del __ibytes__");

        } else if (C_img == 3) {
            /* ---- 3-channel BGR (OpenCV) → HALCON R/G/B planes ---- */
            PyExec(
                "__ch0__ = _np.ascontiguousarray(__ia__[:,:,0]).tobytes()\n"  /* B */
                "__ch1__ = _np.ascontiguousarray(__ia__[:,:,1]).tobytes()\n"  /* G */
                "__ch2__ = _np.ascontiguousarray(__ia__[:,:,2]).tobytes()"    /* R */
            );
            PyObject* pb = PyDict_GetItemString(Globals(), "__ch0__");
            PyObject* pg = PyDict_GetItemString(Globals(), "__ch1__");
            PyObject* pr = PyDict_GetItemString(Globals(), "__ch2__");
            if (!pb || !pg || !pr) {
                PyExec("del __ch0__, __ch1__, __ch2__");
                err = H_ERR_WIPV2; goto cleanup;
            }
            const char* ptr_b = PyBytes_AsString(pb);
            const char* ptr_g = PyBytes_AsString(pg);
            const char* ptr_r = PyBytes_AsString(pr);

            HObject tmp3;
            GenImage3(&tmp3,
                      HTuple(halcon_type),
                      HTuple((Hlong)W_img), HTuple((Hlong)H_img),
                      HTuple((Hlong)(uintptr_t)ptr_r),
                      HTuple((Hlong)(uintptr_t)ptr_g),
                      HTuple((Hlong)(uintptr_t)ptr_b));
            CopyImage(tmp3, &hImageOut);
            PyExec("del __ch0__, __ch1__, __ch2__");

        } else {
            err = H_ERR_WIPV2;
        }

    } catch (const HalconCpp::HException&) {
        err = H_ERR_WIPV2;
    } catch (...) {
        err = H_ERR_WIPV2;
    }

cleanup:
    PyExec("del __ia__, __idt__, __ind__, __ish__");
    return err;
}

/* -------------------------------------------------------------------------
 * Py_SetImage
 *
 * Reads image objects from a HALCON dict and transfers each one to Python
 * as a numpy array.  The dict key becomes the Python variable name.
 *
 * HDevelop usage:
 *   create_dict (ImgDict)
 *   set_dict_object (Image,  ImgDict, 'src')
 *   set_dict_object (Mask,   ImgDict, 'mask')
 *   Py_SetImage (ImgDict)
 *   Py_EvalString ('result = cv2.GaussianBlur(src, (15,15), 0)')
 * ------------------------------------------------------------------------- */
Herror HPy_SetImage(Hproc_handle proc_handle)
{
    const Hcpar*  dict_par;
    INT4_8  num;
    HGetPPar(proc_handle, 1, &dict_par, &num);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    /* Collect image-entry keys.
     * Try "object_keys" first; fall back to "keys" and filter per key. */
    HTuple keys;
    try {
        GetDictParam(hv_Dict, "object_keys", HTuple(), &keys);
    } catch (...) {
        keys = HTuple();
    }

    if (keys.Length() == 0) {
        try {
            GetDictParam(hv_Dict, "keys", HTuple(), &keys);
        } catch (...) {
            return H_MSG_TRUE;
        }
    }

    for (Hlong i = 0; i < keys.Length(); i++) {
        HTuple key(keys[i]);
        const char* varname = keys[i].C();
        if (!varname || !*varname) continue;

        /* GetDictObject throws if the value is tuple-valued */
        HObject hImage;
        try {
            GetDictObject(&hImage, hv_Dict, key);
        } catch (...) {
            continue;
        }

        Herror err = SetImageInGlobals(varname, hImage);
        if (err != H_MSG_TRUE) return err;
    }

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_GetImage
 *
 * Reads named Python numpy arrays and stores them as HALCON image objects
 * back into the dict (under the same key).
 *
 * The dict must be pre-populated with the desired keys (any placeholder
 * value is fine — it will be overwritten with the image object).
 *
 * HDevelop usage:
 *   create_dict (OutDict)
 *   set_dict_tuple (OutDict, 'result', 0)   * placeholder key
 *   Py_GetImage (OutDict)
 *   get_dict_object (ResultImage, OutDict, 'result')
 * ------------------------------------------------------------------------- */
Herror HPy_GetImage(Hproc_handle proc_handle)
{
    const Hcpar*  dict_par;
    INT4_8  num;
    HGetPPar(proc_handle, 1, &dict_par, &num);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    HTuple keys;
    GetDictParam(hv_Dict, "keys", HTuple(), &keys);

    for (Hlong i = 0; i < keys.Length(); i++) {
        HTuple key(keys[i]);

        const char* varname = keys[i].C();
        if (!varname || !*varname) continue; /* skip integer/empty keys */

        HObject hImage;
        Herror err = GetImageFromGlobals(varname, hImage);
        if (err != H_MSG_TRUE) continue; /* skip variables that can't be converted */

        try {
            SetDictObject(hImage, hv_Dict, key);
        } catch (...) {
            /* ignore dict write errors and continue */
        }
    }

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_SetPythonTuple — set a Python variable from a HALCON tuple,
 *                     and store the tuple in the dict under the same key.
 *
 *   set_python_tuple (DictHandle, 'x', 42)
 *   → Python globals['x'] = 42, dict['x'] = 42
 *
 * Supports int, float, string scalars and mixed tuples (→ Python list).
 * ------------------------------------------------------------------------- */
Herror HPy_SetPythonTuple(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);

    const Hcpar* dict_par; INT4_8 dnum;
    HGetPPar(proc_handle, 1, &dict_par, &dnum);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    Hcpar keyPar;
    HGetSPar(proc_handle, 2, STRING_PAR, &keyPar, 1);
    const char* varname = keyPar.par.s;

    const Hcpar* valPar; INT4_8 vnum;
    HGetPPar(proc_handle, 3, &valPar, &vnum);

    /* Build Python object from the tuple elements */
    PyObject* py_val = nullptr;
    if (vnum == 1) {
        switch (valPar[0].type) {
            case LONG_PAR:   py_val = PyLong_FromLongLong(valPar[0].par.l);  break;
            case DOUBLE_PAR: py_val = PyFloat_FromDouble(valPar[0].par.d);   break;
            case STRING_PAR: py_val = PyUnicode_FromString(valPar[0].par.s); break;
            default:         return H_ERR_WIPV2;
        }
    } else {
        py_val = PyList_New((Py_ssize_t)vnum);
        for (INT4_8 i = 0; i < vnum; i++) {
            PyObject* elem = nullptr;
            switch (valPar[i].type) {
                case LONG_PAR:   elem = PyLong_FromLongLong(valPar[i].par.l);  break;
                case DOUBLE_PAR: elem = PyFloat_FromDouble(valPar[i].par.d);   break;
                case STRING_PAR: elem = PyUnicode_FromString(valPar[i].par.s); break;
                default:         elem = Py_None; Py_INCREF(elem);              break;
            }
            PyList_SetItem(py_val, (Py_ssize_t)i, elem);
        }
    }
    PyDict_SetItemString(Globals(), varname, py_val);
    Py_DECREF(py_val);

    /* Also store in the dict */
    HTuple hv_Value;
    for (INT4_8 i = 0; i < vnum; i++) {
        switch (valPar[i].type) {
            case LONG_PAR:   hv_Value.Append((Hlong)valPar[i].par.l);          break;
            case DOUBLE_PAR: hv_Value.Append(valPar[i].par.d);                 break;
            case STRING_PAR: hv_Value.Append(HTuple(valPar[i].par.s));         break;
            default: break;
        }
    }
    SetDictTuple(hv_Dict, HTuple(varname), hv_Value);

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_GetPythonTuple — read a Python variable and return it as a HALCON tuple,
 *                     also store the value in the dict.
 *
 *   get_python_tuple (DictHandle, 'x', Value)
 *   → reads Python globals['x'], returns as Value, stores dict['x'] = Value
 *
 * Scalars return as single element; lists return as HALCON tuple.
 * ------------------------------------------------------------------------- */
Herror HPy_GetPythonTuple(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 1024);

    const Hcpar* dict_par; INT4_8 dnum;
    HGetPPar(proc_handle, 1, &dict_par, &dnum);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    Hcpar keyPar;
    HGetSPar(proc_handle, 2, STRING_PAR, &keyPar, 1);
    const char* varname = keyPar.par.s;

    PyObject* val = PyDict_GetItemString(Globals(), varname);
    if (!val) return H_ERR_WIPV2;

    /* Helper lambda: convert one PyObject to HALCON output */
    auto putSingle = [&](PyObject* v) {
        if (PyBool_Check(v)) {
            INT4_8 l = (v == Py_True) ? 1 : 0;
            HPutElem(proc_handle, 1, &l, 1, LONG_PAR);
        } else if (PyLong_Check(v)) {
            INT4_8 l = PyLong_AsLongLong(v);
            HPutElem(proc_handle, 1, &l, 1, LONG_PAR);
        } else if (PyFloat_Check(v)) {
            double d = PyFloat_AsDouble(v);
            HPutElem(proc_handle, 1, &d, 1, DOUBLE_PAR);
        } else {
            PyObject* s_obj = PyObject_Str(v);
            const char* s = PyUnicode_AsUTF8AndSize(s_obj, NULL);
            char* tmp;
            HAllocTmp(proc_handle, &tmp, (Hlong)(strlen(s) + 1));
            strcpy(tmp, s);
            HPutElem(proc_handle, 1, &tmp, 1, STRING_PAR);
            Py_DECREF(s_obj);
        }
    };

    HTuple hv_Value;

    if (PyList_Check(val) || PyTuple_Check(val)) {
        Py_ssize_t n = PyList_Check(val) ? PyList_Size(val) : PyTuple_Size(val);
        std::vector<double>  dvals;
        std::vector<INT4_8>  lvals;
        bool all_int = true, all_num = true;
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject* item = PyList_Check(val) ? PyList_GetItem(val, i) : PyTuple_GetItem(val, i);
            if (PyFloat_Check(item)) { all_int = false; }
            else if (!PyLong_Check(item)) { all_int = false; all_num = false; }
        }
        if (all_num && all_int && n > 0) {
            INT4_8* buf;
            HAllocTmp(proc_handle, &buf, (Hlong)(n * sizeof(INT4_8)));
            for (Py_ssize_t i = 0; i < n; i++) {
                PyObject* item = PyList_Check(val) ? PyList_GetItem(val, i) : PyTuple_GetItem(val, i);
                buf[i] = PyLong_AsLongLong(item);
                hv_Value.Append((Hlong)buf[i]);
            }
            HPutElem(proc_handle, 1, buf, (INT4_8)n, LONG_PAR);
        } else if (all_num && n > 0) {
            double* buf;
            HAllocTmp(proc_handle, &buf, (Hlong)(n * sizeof(double)));
            for (Py_ssize_t i = 0; i < n; i++) {
                PyObject* item = PyList_Check(val) ? PyList_GetItem(val, i) : PyTuple_GetItem(val, i);
                buf[i] = PyFloat_Check(item) ? PyFloat_AsDouble(item) : (double)PyLong_AsLongLong(item);
                hv_Value.Append(buf[i]);
            }
            HPutElem(proc_handle, 1, buf, (INT4_8)n, DOUBLE_PAR);
        } else if (n == 0) {
            /* empty list → empty string */
            const char* empty = "";
            char* tmp;
            HAllocTmp(proc_handle, &tmp, 1);
            tmp[0] = '\0';
            HPutElem(proc_handle, 1, &tmp, 1, STRING_PAR);
        } else {
            /* mixed or string list → return first element as scalar */
            putSingle(PyList_Check(val) ? PyList_GetItem(val, 0) : PyTuple_GetItem(val, 0));
        }
    } else {
        putSingle(val);
        if (PyBool_Check(val) || PyLong_Check(val))
            hv_Value.Append((Hlong)PyLong_AsLongLong(val));
        else if (PyFloat_Check(val))
            hv_Value.Append(PyFloat_AsDouble(val));
        else {
            PyObject* s_obj = PyObject_Str(val);
            hv_Value.Append(HTuple(PyUnicode_AsUTF8AndSize(s_obj, NULL)));
            Py_DECREF(s_obj);
        }
    }

    if (hv_Value.Length() > 0)
        SetDictTuple(hv_Dict, HTuple(varname), hv_Value);

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_SetPythonObject — transfer a HALCON image (iconic input) to Python
 *                      as a numpy array, and store it in the dict.
 *
 *   set_python_object (Image, DictHandle, 'src')
 *   → Python globals['src'] = numpy array, dict['src'] = Image
 * ------------------------------------------------------------------------- */
Herror HPy_SetPythonObject(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    /* 1. Get input iconic object */
    Hkey obj_key;
    HGetObj(proc_handle, 1, 1, &obj_key);
    HObject hImage(obj_key);

    /* 2. Get control parameters */
    const Hcpar* dict_par; INT4_8 dnum;
    HGetPPar(proc_handle, 1, &dict_par, &dnum);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    Hcpar keyPar;
    HGetSPar(proc_handle, 2, STRING_PAR, &keyPar, 1);
    const char* varname = keyPar.par.s;

    /* 3. Store image in dict */
    SetDictObject(hImage, hv_Dict, HTuple(varname));

    /* 4. Push image to Python globals */
    return SetImageInGlobals(varname, hImage);
}

/* -------------------------------------------------------------------------
 * Py_GetPythonObject — read a Python numpy array and return it as a HALCON
 *                      image (iconic output), also store in the dict.
 *
 *   get_python_object (Image, DictHandle, 'result')
 *   → reads Python globals['result'], outputs Image, stores dict['result']
 * ------------------------------------------------------------------------- */
Herror HPy_GetPythonObject(Hproc_handle proc_handle)
{
    HAllocStringMem(proc_handle, 512);

    /* 1. Get control parameters */
    const Hcpar* dict_par; INT4_8 dnum;
    HGetPPar(proc_handle, 1, &dict_par, &dnum);
    HHandle hv_DictHandle((Hlong)dict_par[0].par.h);
    HTuple  hv_Dict(hv_DictHandle);

    Hcpar keyPar;
    HGetSPar(proc_handle, 2, STRING_PAR, &keyPar, 1);
    const char* varname = keyPar.par.s;

    /* 2. Get image from Python */
    HObject hImage;
    Herror err = GetImageFromGlobals(varname, hImage);
    if (err != H_MSG_TRUE) return err;

    /* 3. Store image in dict */
    SetDictObject(hImage, hv_Dict, HTuple(varname));

    /* 4. Copy to output iconic parameter */
    Hkey out_key;
    HCopyObj(proc_handle, hImage.Key(), 1, &out_key);

    return H_MSG_TRUE;
}

/* -------------------------------------------------------------------------
 * Py_GetOutput  ↔  Matlab_engOutputBuffer
 *
 *   Size > 0   →  just clears the buffer, returns ""  (like setting up a buffer)
 *   Size == -1 →  returns captured stdout and clears   (like reading the buffer)
 *   Size == 0  →  returns captured stdout without clearing
 *
 * HDevelop usage:
 *   Py_GetOutput (1024, Msg)    * set up / clear
 *   Py_GetOutput (-1,   Msg)    * read & clear
 * ------------------------------------------------------------------------- */
Herror HPy_GetOutput(Hproc_handle proc_handle)
{
    Hcpar Size;
    HGetSPar(proc_handle, 1, LONG_PAR, &Size, 1);

    const char* code = nullptr;
    if (Size.par.l == 0) {
        /* read without clearing */
        code = "__tmp_out__ = _py_buf.getvalue()";
    } else if (Size.par.l == -1) {
        /* read and clear */
        code = "__tmp_out__ = _py_buf.getvalue()\n"
               "_py_buf.seek(0)\n"
               "_py_buf.truncate(0)";
    } else {
        /* positive: clear (= set-up phase) */
        code = "_py_buf.seek(0)\n"
               "_py_buf.truncate(0)\n"
               "__tmp_out__ = ''";
    }

    PyExec(code);

    const char* output = "";
    PyObject* out_obj = PyDict_GetItemString(Globals(), "__tmp_out__");
    if (out_obj && PyUnicode_Check(out_obj))
        output = PyUnicode_AsUTF8AndSize(out_obj, NULL);

    char* tmp;
    HAllocTmp(proc_handle, &tmp, (Hlong)(strlen(output) + 1));
    strcpy(tmp, output);
    HPutElem(proc_handle, 1, &tmp, 1, STRING_PAR);

    PyExec("del __tmp_out__");
    return H_MSG_TRUE;
}
