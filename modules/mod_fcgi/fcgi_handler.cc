#include "fcgi_handler.h"
#include "fcgi_completion_stage.h"
#include "utils/logger.h"

namespace tube {
namespace fcgi {

FcgiHttpHandler::FcgiHttpHandler()
    : conn_pool_(NULL)
{
    add_option("connection_type", "");
    add_option("connection_address", "");
    add_option("connection_pool_size", "-1");
    add_option("script_filename", "");
    add_option("script_dirname", "");

    completion_stage_ = (FcgiCompletionStage*)
        Pipeline::instance().find_stage("fcgi_completion");
}

FcgiHttpHandler::~FcgiHttpHandler()
{
    delete conn_pool_;
}

void
FcgiHttpHandler::load_param()
{
    std::string conn_type = option("connection_type");
    std::string conn_addr = option("connection_address");
    int pool_size = utils::parse_int(option("connection_pool_size"));
    if (conn_type == "tcp") {
        // fprintf(stderr, "tcp %s\n", conn_addr.c_str());
        conn_pool_ = new TcpConnectionPool(conn_addr, pool_size);
    } else if (conn_type == "unix") {
        conn_pool_ = new UnixConnectionPool(conn_addr, pool_size);
    }
    script_filename_ = option("script_filename");
    script_dirname_ = option("script_dirname");
}

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

    if (!script_filename_.empty()) {
        cgi_env.set_environment("SCRIPT_FILENAME", script_filename_);
    } else {
        cgi_env.set_environment("SCRIPT_FILENAME", script_dirname_ + "/"
                                + request.path());
    }

    for (size_t i = 0; i < request.headers().size(); i++) {
        std::string cgi_key = "HTTP_";
        cgi_key += utils::string_to_upper_case(request.headers()[i].key);
        cgi_env.set_environment(cgi_key, request.headers()[i].value);
    }
}

void
FcgiHttpHandler::process_continuation_start(HttpRequest& request,
                                            HttpResponse& response,
                                            HttpConnection* conn,
                                            FcgiCompletionContinuation* cont)
{
    bool is_connected = false;
    int sock_fd = conn_pool_->alloc_connection(is_connected);
    if (sock_fd < 0) {
        response.respond_with_message(
            HttpResponseStatus::kHttpResponseBadGateway);
        return;
    }
    if (!is_connected) {
        if (!conn_pool_->connect(sock_fd)) {
            conn_pool_->reclaim_inactive_connection(sock_fd);
            response.respond_with_message(
                HttpResponseStatus::kHttpResponseBadGateway);
            return;
        }
    }
    request.disable_poll();

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
    // fprintf(stderr, "sending to completion stage %p\n", conn);
    yield(response, conn, cont);
}

void
FcgiHttpHandler::process_headers_done(HttpRequest& request,
                                      HttpResponse& response,
                                      HttpConnection* conn,
                                      FcgiCompletionContinuation* cont)
{
    FcgiContentParser& content_parser = cont->content_parser;
    // done with the headers. here, we should decide to do output buffering
    // or streaming
    if (content_parser.has_error()) {
        response.respond_with_message(
            HttpResponseStatus::kHttpResponseBadGateway);
        return;
    }
    if (content_parser.is_streaming()) {
        // fprintf(stderr, "streaming...\n");
        make_response(response, content_parser, cont);
    }
    // fprintf(stderr, "completion stage said that header is complete\n");
    while (completion_stage_->run_parser(cont)) {
        if (cont->task_len == 0) {
            cont->status = kCompletionEOF;
            process_eof(request, response, conn, cont);
            return;
        }
        if (cont->content_parser.has_error()) {
            cont->status = kCompletionError;
            process_error(request, response, conn, cont);
            return;
        }
    }
    cont->status = kCompletionReadFcgi;
    yield(response, conn, cont);
}

void
FcgiHttpHandler::process_continue(HttpRequest& request,
                                  HttpResponse& response,
                                  HttpConnection* conn,
                                  FcgiCompletionContinuation* cont)
{
    // streaming partially complete
    cont->status = kCompletionReadFcgi;
    yield(response, conn, cont);
}

void
FcgiHttpHandler::process_eof(HttpRequest& request,
                             HttpResponse& response,
                             HttpConnection* conn,
                             FcgiCompletionContinuation* cont)
{
    // fprintf(stderr, "we're done\n");
    conn_pool_->reclaim_connection(cont->sock_fd);
    // all done. should make a response
    FcgiContentParser& content_parser = cont->content_parser;
    if (content_parser.is_streaming()) {
        response.force_responded();
    } else {
        response.set_content_length(cont->output_buffer.size());
        make_response(response, content_parser, cont);
    }
    conn->out_stream().append_buffer(cont->output_buffer);

    // reclaim the connection
    conn_pool_->reclaim_connection(cont->sock_fd);
    request.enable_poll();
}

void
FcgiHttpHandler::process_error(HttpRequest& request,
                               HttpResponse& response,
                               HttpConnection* conn,
                               FcgiCompletionContinuation* cont)
{
    // fprintf(stderr, "we're in error\n");
    if (cont->need_reconnect) {
        conn_pool_->reclaim_inactive_connection(cont->sock_fd);
        if (cont->content_parser.is_streaming()) {
            // upstream error when streaming...
            // close the connection
            response.close();
            return;
        }
    } else {
        conn_pool_->reclaim_connection(cont->sock_fd);
    }
    response.respond_with_message(
        HttpResponseStatus::kHttpResponseBadGateway);
    request.enable_poll();
}

void
FcgiHttpHandler::handle_request(HttpRequest& request, HttpResponse& response)
{
    if (conn_pool_ == NULL || (script_filename_.empty()
                               && script_dirname_.empty())) {
        return; // bypass
    }

    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) response.restore_continuation();
    HttpConnection* conn = (HttpConnection*) request.connection();
    // fprintf(stderr, "handle_request %p cont: %p\n", conn, cont);
    if (cont == NULL) {
        process_continuation_start(request, response, conn, cont);
    } else if (cont->status == kCompletionHeadersDone) {
        process_headers_done(request, response, conn, cont);
    } else if (cont->status == kCompletionContinue) {
        process_continue(request, response, conn, cont);
    } else if (cont->status == kCompletionEOF) {
        process_eof(request, response, conn, cont);
    } else if (cont->status == kCompletionError) {
        process_error(request, response, conn, cont);
    } else {
        LOG(ERROR, "Unknow continuation status %d", cont->status);
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
