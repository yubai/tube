#include "fcgi_handler.h"
#include "fcgi_completion_stage.h"
#include "utils/logger.h"

namespace tube {
namespace fcgi {

FcgiHttpHandler::FcgiHttpHandler()
{
    // temporal test!
    conn_pool_ = new TcpConnectionPool("127.0.0.1:9000", 128);
    completion_stage_ = (FcgiCompletionStage*)
        Pipeline::instance().find_stage("fcgi_completion");
}

FcgiHttpHandler::~FcgiHttpHandler()
{
    delete conn_pool_;
}

void
FcgiHttpHandler::load_param()
{}

void
FcgiHttpHandler::setup_environment(HttpRequest& request,
                                   FcgiEnvironment& cgi_env)
{
    cgi_env.set_environment("QUERY_STRING", request.query_string());
    cgi_env.set_environment("CONTENT_TYPE",
                            request.find_header_value("Content-type"));
    std::stringstream ss;
    ss << request.content_length();
    cgi_env.set_environment("CONTENT_LENGTH", ss.str());
    cgi_env.set_environment("REQUEST_METHOD", request.method_string());
    cgi_env.set_environment("REQUEST_URI", request.uri());
    cgi_env.set_environment("SCRIPT_NAME", request.path());
    cgi_env.set_environment("SERVER_PROTOCOL", "HTTP/1.1");
    cgi_env.set_environment("GATEWAY_INTERFACE", "CGI/1.1");

    // just for testing!!
    cgi_env.set_environment("SCRIPT_FILENAME", "/usr/share/nginx/html/info.php");

    for (size_t i = 0; i < request.headers().size(); i++) {
        std::string cgi_key = "HTTP_";
        cgi_key += utils::string_to_upper_case(request.headers()[i].key);
        cgi_env.set_environment(cgi_key, request.headers()[i].value);
    }
}

void
FcgiHttpHandler::handle_request(HttpRequest& request, HttpResponse& response)
{
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) response.restore_continuation();
    HttpConnection* conn = (HttpConnection*) request.connection();

    if (cont == NULL) {
        bool is_connected = false;
        int sock_fd = conn_pool_->alloc_connection(is_connected);
        if (!is_connected) {
            fprintf(stderr, "connecting...\n");
            conn_pool_->connect(sock_fd);
        }
        utils::set_socket_blocking(sock_fd, false);
        FcgiEnvironment cgi_env(sock_fd);
        cgi_env.begin_request();
        setup_environment(request, cgi_env);
        cgi_env.commit_environment();
        cgi_env.prepare_request(request.content_length());
        // all setups are done, need to create the continuation
        cont = new FcgiCompletionContinuation();
        cont->sock_fd = sock_fd;
        cont->task_buffer = cgi_env.result_buffer();
        cont->task_buffer.append(conn->in_stream().buffer());
        if (cont->task_buffer.size() < request.content_length()) {
            cont->task_len =
                request.content_length() - cont->task_buffer.size();
        }
        if (cont->task_len > 0) {
            cont->status = kCompletionReadClient;
        } else {
            cont->status = kCompletionWriteFcgi;
        }
        fprintf(stderr, "sending to completion stage\n");
        yield(response, conn, cont);
    } else if (cont->status == kCompletionHeadersDone) {
        FcgiContentParser& content_parser = cont->content_parser;
        // done with the headers. here, we should decide to do output buffering
        // or streaming
        if (content_parser.has_error()) {
            response.respond_with_message(
                HttpResponseStatus::kHttpResponseBadGateway);
            return;
        }
        if (content_parser.is_streaming()) {
            make_response(response, content_parser, cont);
        }
        cont->status = kCompletionReadFcgi;
        fprintf(stderr, "completion stage said that header is complete\n");
        yield(response, conn, cont);
    } else if (cont->status == kCompletionContinue) {
        // streaming partially complete
        cont->status = kCompletionReadFcgi;
        fprintf(stderr, "need to go on\n");
        yield(response, conn, cont);
    } else if (cont->status == kCompletionEOF) {
        fprintf(stderr, "we're done\n");
        conn_pool_->reclaim_connection(cont->sock_fd);
        // all done. should make a response
        FcgiContentParser& content_parser = cont->content_parser;
        if (content_parser.is_streaming()) {
            response.force_responded();
        } else {
            response.set_content_length(cont->output_buffer.size());
            make_response(response, content_parser, cont);
            conn->out_stream().append_buffer(cont->output_buffer);
        }
    }
}

void
FcgiHttpHandler::yield(HttpResponse& response, HttpConnection* conn,
                       FcgiCompletionContinuation* cont)
{
    response.suspend_continuation(cont);
    completion_stage_->sched_add(conn);
}

void
FcgiHttpHandler::make_response(HttpResponse& response,
                               FcgiContentParser& content_parser,
                               FcgiCompletionContinuation* cont)
{
    response.disable_prepare_buffer();
    for (size_t i = 0; i < content_parser.headers().size(); i++) {
        std::string key = content_parser.headers()[i].first;
        std::string value = content_parser.headers()[i].second;
        response.add_header(key, value);
    }
    if (content_parser.status_text() != "") {
        response.respond(HttpResponseStatus(content_parser.status_text()));
    } else {
        response.respond(HttpResponseStatus::kHttpResponseOK);
    }
}

}
}

static tube::fcgi::FcgiCompletionStage* fcgi_completion_stage = NULL;

extern "C" void
tube_http_fcgi_module_init()
{
    static tube::fcgi::FcgiHttpHandlerFactory factory;
    fcgi_completion_stage = new tube::fcgi::FcgiCompletionStage();
    tube::BaseHttpHandlerFactory::register_factory(&factory);
}

extern "C" void
tube_http_fcgi_module_finit()
{
    delete fcgi_completion_stage;
}
