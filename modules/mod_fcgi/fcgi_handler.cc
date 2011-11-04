#include "fcgi_handler.h"
#include "utils/logger.h"

namespace tube {
namespace fcgi {

FcgiHttpHandler::FcgiHttpHandler()
{}

FcgiHttpHandler::~FcgiHttpHandler()
{}

void
FcgiHttpHandler::load_param()
{}

void
FcgiHttpHandler::handle_request(HttpRequest& request, HttpResponse& response)
{}

}
}

extern "C" void
tube_http_fcgi_module_init()
{
    static tube::fcgi::FcgiHttpHandlerFactory factory;
    tube::BaseHttpHandlerFactory::register_factory(&factory);
}

extern "C" void
tube_http_fcgi_module_finit()
{
    // nothing to do here.
}
