// -*- mode: c++ -*-

#ifndef _FCGI_HANDLER_H_
#define _FCGI_HANDLER_H_

#include <string>

#include "utils/misc.h"
#include "http/http_wrapper.h"
#include "http/interface.h"

namespace tube {
namespace fcgi {

class FcgiHttpHandler : public BaseHttpHandler
{
public:
    FcgiHttpHandler();
    virtual ~FcgiHttpHandler();

    virtual void load_param();
    virtual void handle_request(HttpRequest& request, HttpResponse& response);
};

class FcgiHttpHandlerFactory : public BaseHttpHandlerFactory
{
public:
    virtual BaseHttpHandler* create() const {
        return new FcgiHttpHandler();
    }
    virtual std::string module_name() const {
        return std::string("fastcgi");
    }
    virtual std::string vendor_name() const {
        return std::string("tube");
    }
};

}
}

#endif /* _FCGI_HANDLER_H_ */
