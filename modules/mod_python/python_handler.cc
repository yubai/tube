#include "python_handler.h"
#include "utils/logger.h"
#include "utils/cdecl.h"

namespace tube {
namespace python {

PyObject*
PythonHttpHandler::load_python_handler()
{
    PyObject* module = NULL;
    PyObject* module_dict = NULL;
    PyObject* func_obj = NULL;
    PyObject* handler_obj = NULL;

    module = PyImport_ImportModule(pymodule_name_.c_str());
    if (module == NULL) {
        LOG(ERROR, "Cannot import module %s", pymodule_name_.c_str());
        goto error;
    }
    module_dict = PyModule_GetDict(module);
    if (module_dict == NULL) {
        LOG(ERROR, "Cannot get dict from module %s", pymodule_name_.c_str());
        goto error;
    }
    func_obj = PyDict_GetItemString(module_dict, pycons_name_.c_str());
    if (func_obj == NULL || !PyCallable_Check(func_obj)) {
        PyErr_Print();
        LOG(ERROR, "Cannot found callable %s in module %s",
            pycons_name_.c_str(), pymodule_name_.c_str());
        goto error;
    }
    handler_obj = PyObject_CallObject(func_obj, NULL);
    if (handler_obj == NULL) {
        LOG(ERROR, "Cannot create handler object");
        goto error;
    }
    return handler_obj;
error:
    Py_XDECREF(module);
    return NULL;
}

PythonHttpHandler::PythonHttpHandler()
    : py_handler_(NULL)
{
    add_option("module_name", "");
    add_option("class_name", "");
    add_option("module_path", "");
}

void
PythonHttpHandler::load_param()
{
    pymodule_name_ = option("module_name");
    pycons_name_ = option("class_name");
    std::string module_path = option("module_path");

    if (pymodule_name_ == "" || pycons_name_ == "") {
        LOG(ERROR, "module_name and class_name arguemnt are required.");
    }
    PyGILState_STATE state = PyGILState_Ensure();
    if (module_path != "") {
        PyRun_SimpleString("import sys");
        std::string eval_str = "sys.path.append(r'";
        eval_str += module_path + "')";
        PyRun_SimpleString(eval_str.c_str());
    }
    py_handler_ = load_python_handler();
    PyGILState_Release(state);
}

void
PythonHttpHandler::handle_request(HttpRequest& request, HttpResponse& response)
{
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject* method = NULL;
    PyObject* pyreq = PyInt_FromLong((long) &request);
    PyObject* pyres = PyInt_FromLong((long) &response);
    PyObject* args = NULL;
    PyObject* ret = NULL;

    if (py_handler_ == NULL) {
        goto error;
    }
    method = PyObject_GetAttrString(py_handler_, "__handle_request");
    if (method == NULL || !PyCallable_Check(method)) {
        goto error;
    }
    args = PyTuple_Pack(2, pyreq, pyres);
    ret = PyObject_CallObject(method, args);
    if (ret == NULL) {
        PyErr_Print();
        Py_DECREF(args);
        goto error;
    } else {
        Py_DECREF(ret);
    }
    Py_DECREF(args);
    goto done;
error:
    PyGILState_Release(state);
    response.respond_with_message(
        HttpResponseStatus::kHttpResponseInternalServerError);
    return;
done:
    PyGILState_Release(state);
    return;
}

}
}

extern "C" void
tube_http_python_module_init()
{
    Py_InitializeEx(0);
    PyEval_InitThreads();
    PyEval_SaveThread();
    static tube::python::PythonHttpHandlerFactory python_handler_factory;
    tube::BaseHttpHandlerFactory::register_factory(&python_handler_factory);
}

extern "C" void
tube_http_python_module_finit()
{
    Py_Finalize();
}
