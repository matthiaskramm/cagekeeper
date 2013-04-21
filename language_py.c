#include <Python.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include "util.h"
#include "language.h"

typedef struct _py_internal {
    PyObject*globals;
    PyObject*module;
    language_t*li;
    char*buffer;
} py_internal_t;

static PyTypeObject FunctionProxyClass;

typedef struct {
    PyObject_HEAD
    char*name;
    py_internal_t*py_internal;
    function_t*function;
} FunctionProxyObject;

value_t* pyobject_to_value(language_t*li, PyObject*o)
{
    if(PyUnicode_Check(o)) {
        return value_new_string(PyUnicode_AS_DATA(o));
    } else if(PyString_Check(o)) {
        return value_new_string(PyString_AsString(o));
    } else if(PyLong_Check(o)) {
        return value_new_int32(PyLong_AsLongLong(o));
    } else if(PyInt_Check(o)) {
        return value_new_int32(PyInt_AsLong(o));
    } else if(PyFloat_Check(o)) {
        return value_new_float32(PyFloat_AsDouble(o));
#if PY_MAJOR_VERSION >= 3
    } else if(PyDouble_Check(o)) {
        return value_new_float32(PyDouble_AsDouble(o));
#endif
    } else if(PyBool_Check(o)) {
        return value_new_boolean(o == Py_True);
    } else if(PyList_Check(o)) {
        int i;
        int l = PyList_GET_SIZE(o);
        value_t*array = array_new();
        for(i=0;i<l;i++) {
            PyObject*o = PyList_GET_ITEM(o, i);
            array_append(array, pyobject_to_value(li, o));
        }
        return array;
    } else if(PyTuple_Check(o)) {
        int i;
        int l = PyTuple_GET_SIZE(o);
        value_t*array = array_new();
        for(i=0;i<l;i++) {
            PyObject*e = PyTuple_GetItem(o, i);
            array_append(array, pyobject_to_value(li, e));
        }
        return array;
    } else {
        language_error(li, "Can't convert type %s", o->ob_type->tp_name);
        return NULL;
    }
}

static PyObject* value_to_pyobject(language_t*li, value_t*value)
{
    switch(value->type) {
        case TYPE_VOID:
            return Py_BuildValue("s", NULL);
        break;
        case TYPE_FLOAT32:
            return PyFloat_FromDouble(value->f32);
        break;
        case TYPE_INT32:
            return PyInt_FromLong(value->i32);
        break;
        case TYPE_BOOLEAN:
            return PyBool_FromLong(value->b);
        break;
        case TYPE_STRING: {
            return PyUnicode_FromString(value->str);
        }
        break;
        case TYPE_ARRAY: {
            PyObject *array = PyList_New(value->length);
            int i;
            for(i=0;i<value->length;i++) {
                PyObject*entry = value_to_pyobject(li, value->data[i]);
                PyList_SetItem(array, i, entry);
            }
            return array;
        }
        break;
        default: {
            assert(0);
        }
    }
}

static PyObject* python_method_proxy(PyObject* _self, PyObject* _args)
{
    FunctionProxyObject* self = (FunctionProxyObject*)_self;

#ifdef DEBUG
    printf("[python] %s", self->name);
    _args->ob_type->tp_print(_args, stdout, 0);
    printf("\n");
#endif

    language_t*li = self->py_internal->li;
    value_t*args = pyobject_to_value(li, _args);
    value_t*ret = self->function->call(self->function, args);
    value_destroy(args);

    PyObject*pret = value_to_pyobject(li, ret);
    value_destroy(ret);

    return pret;
}

static bool compile_script_py(language_t*li, const char*script)
{
    py_internal_t*py = (py_internal_t*)li->internal;
    dbg("[python] compiling script");

    // test memory allocation
    PyObject* tmp = PyString_FromString("test");
    Py_DECREF(tmp);

    PyObject* ret = PyRun_String(script, Py_file_input, py->globals, NULL);
    if(ret == NULL) {
        if(li->verbosity>0) {
            PyErr_Print();
        }
        PyErr_Clear();
    }
#ifdef DEBUG
    if(ret!=NULL) {
        dbg("[python] compile successful");
    } else {
        dbg("[python] compile error");
    }
#endif
    return ret!=NULL;
}

static bool is_function_py(language_t*li, const char*name)
{
    py_internal_t*py = (py_internal_t*)li->internal;
    dbg("[python] checking for function %s", name);
    PyObject* ret = PyRun_String(name, Py_eval_input, py->globals, py->globals);
    if(ret) {
        Py_DecRef(ret);
        return true;
    }
    PyErr_Clear();
    return false;
}

static value_t* call_function_py(language_t*li, const char*name, value_t*args)
{
    py_internal_t*py = (py_internal_t*)li->internal;
    dbg("[python] calling function %s", name);

    char*script = allocprintf("%s()", name);
    PyObject* ret = PyRun_String(script, Py_eval_input, py->globals, py->globals);
    if(ret == NULL) {
        if(li->verbosity>0) {
            PyErr_Print();
        }
        PyErr_Clear();
        free(script);
        return NULL;
    } else {
        free(script);
        return pyobject_to_value(li, ret);
    }
}

static void define_constant_py(language_t*li, const char*name, value_t*value)
{
    py_internal_t*py = (py_internal_t*)li->internal;
    dbg("[python] defining constant %s", name);
    PyDict_SetItem(py->globals, PyString_FromString(name), value_to_pyobject(li, value));
}

#if PY_MAJOR_VERSION < 3
#define PYTHON_HEAD_INIT \
    PyObject_HEAD_INIT(NULL) \
    0,
#else
#define PYTHON_HEAD_INIT \
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
#endif

static void functionproxy_dealloc(PyObject* _self) {
    FunctionProxyObject* self = (FunctionProxyObject*)_self;
    if(self->name)
        free(self->name);
    PyObject_Del(self);
}
static PyTypeObject FunctionProxyClass =
{
    PYTHON_HEAD_INIT
    tp_name: "FunctionProxy",
    tp_basicsize: sizeof(FunctionProxyObject),
    tp_itemsize: 0,
    tp_dealloc: functionproxy_dealloc,
};

static void define_function_py(language_t*li, const char*name, function_t*f)
{
    py_internal_t*py_internal = (py_internal_t*)li->internal;

    PyObject*dict = py_internal->globals;

    PyMethodDef*m = calloc(sizeof(PyMethodDef), 1);
    m->ml_name = name;
    m->ml_meth = python_method_proxy;
    m->ml_flags = METH_VARARGS;

    FunctionProxyObject*self = PyObject_New(FunctionProxyObject, &FunctionProxyClass);
    self->name = strdup(name);
    self->function = f;
    self->py_internal = py_internal;

    PyObject*cfunction = PyCFunction_New(m, (PyObject*)self);
    PyDict_SetItemString(dict, name, cfunction);
}

static int py_reference_count = 0;

static bool init_py(py_internal_t*py)
{
    dbg("[python] initializing interpreter");

    if(py_reference_count==0) {
        void*old = signal(2, SIG_IGN);
        Py_Initialize();
#if PY_MAJOR_VERSION < 3
        FunctionProxyClass.ob_type = &PyType_Type;
#endif
        signal(2, old);
    }
    py_reference_count++;

    py->globals = PyDict_New();
    py->buffer = malloc(65536);

    PyObject* module = PyImport_AddModule("__main__");
    PyObject* globals = PyModule_GetDict(module);

    PyDict_Update(py->globals, globals);

    //globals->ob_type->tp_print(globals, stdout, 0);
    
    PyDict_SetItem(py->globals, PyString_FromString("math"), PyImport_ImportModule("math"));

    /* compile an empty script so Python has a chance to load all the things
       it needs for compiling (encodingsmodule etc.) */
    PyRun_String("None", Py_file_input, py->globals, NULL);
}

static void destroy_py(language_t* li)
{
    py_internal_t*py = (py_internal_t*)li->internal;
    free(py->buffer);
    free(py);
    free(li);
    if(--py_reference_count==0) {
        Py_Finalize();
    }
}

language_t* python_interpreter_new()
{
    language_t * li = calloc(1, sizeof(language_t));
#ifdef DEBUG
    li->magic = LANG_MAGIC;
#endif
    li->name = "py";
    li->compile_script = compile_script_py;
    li->is_function = is_function_py;
    li->call_function = call_function_py;
    li->define_constant = define_constant_py;
    li->define_function = define_function_py;
    li->destroy = destroy_py;
    li->internal = calloc(1, sizeof(py_internal_t));

    py_internal_t*py = (py_internal_t*)li->internal;
    py->li = li;
    init_py(py);
    return li;
}
