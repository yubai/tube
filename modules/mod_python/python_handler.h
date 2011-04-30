// -*- mode: c++ -*-

#ifndef _PYTHON_HANDLER_H_
#define _PYTHON_HANDLER_H_

#include <Python.h>
#include <string>

#include "utils/misc.h"
#include "http/http_wrapper.h"
#include "http/interface.h"

namespace tube {
namespace python {

class PythonHttpHandler : public BaseHttpHandler
{
    std::string pymodule_name_;
    std::string pycons_name_;

    PyObject* py_handler_;
public:
    PythonHttpHandler();

    virtual void load_param();
    virtual void handle_request(HttpRequest& request, HttpResponse& response);
private:
    PyObject* load_python_handler();
};

class PythonHttpHandlerFactory : public BaseHttpHandlerFactory
{
public:
    virtual BaseHttpHandler* create() const {
        return new PythonHttpHandler();
    }
    virtual std::string module_name() const {
        return std::string("python");
    }
    virtual std::string vendor_name() const {
        return std::string("tube");
    }
};

}
}

#endif /* _PYTHON_HANDLER_H_ */
