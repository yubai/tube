// -*- mode: c++ -*-

#ifndef _FCGI_HANDLER_H_
#define _FCGI_HANDLER_H_

#include <string>

#include "utils/misc.h"
#include "http/http_wrapper.h"
#include "http/interface.h"
#include "connection_pool.h"
#include "fcgi_completion_stage.h"

namespace tube {
namespace fcgi {

class FcgiHttpHandler : public BaseHttpHandler
{
public:
    FcgiHttpHandler();
    virtual ~FcgiHttpHandler();

    virtual void load_param();
    virtual void handle_request(HttpRequest& request, HttpResponse& response);
private:

    void yield(HttpResponse& response, HttpConnection* conn,
               FcgiCompletionContinuation* cont);

    void make_response(HttpResponse& response,
                       FcgiContentParser& content_parser,
                       FcgiCompletionContinuation* cont);
    void setup_environment(HttpRequest& request, FcgiEnvironment& cgi_env);

    void process_continuation_start(HttpRequest& request,
                                    HttpResponse& response,
                                    HttpConnection* conn,
                                    FcgiCompletionContinuation* cont);

    void process_headers_done(HttpRequest& request,
                              HttpResponse& response,
                              HttpConnection* conn,
                              FcgiCompletionContinuation* cont);

    void process_continue(HttpRequest& request,
                          HttpResponse& response,
                          HttpConnection* conn,
                          FcgiCompletionContinuation* cont);

    void process_eof(HttpRequest& request,
                     HttpResponse& response,
                     HttpConnection* conn,
                     FcgiCompletionContinuation* cont);

    void process_abort(HttpRequest& request,
                       HttpResponse& response,
                       HttpConnection* conn,
                       FcgiCompletionContinuation* cont);

private:
    ConnectionPool* conn_pool_;
    FcgiCompletionStage* completion_stage_;
    std::string script_filename_;
    std::string script_dirname_;
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
