#include "pch.h"

#include "http/http_stages.h"
#include "http/connection.h"
#include "http/http_wrapper.h"
#include "http/configuration.h"
#include "core/stages.h"
#include "core/pipeline.h"
#include "utils/logger.h"

namespace tube {

int HttpConnectionFactory::kDefaultTimeout = 15;
bool HttpConnectionFactory::kCorkEnabled = true;

Connection*
HttpConnectionFactory::create_connection(int fd)
{
    Connection* conn = new HttpConnection(fd);
    conn->set_idle_timeout(kDefaultTimeout);
    conn->set_cork_enabled(kCorkEnabled);
    return conn;
}

void
HttpConnectionFactory::destroy_connection(Connection* conn)
{
    delete conn;
}

HttpParserStage::HttpParserStage()
{
    // replace the connection factory
    pipeline_.set_connection_factory(new HttpConnectionFactory());
}

void
HttpParserStage::initialize()
{
    handler_stage_ = pipeline_.find_stage("http_handler");
}

HttpParserStage::~HttpParserStage()
{}

void
HttpParserStage::increase_load(long load)
{
    Scheduler* scheduler = handler_stage_->scheduler();
    if (scheduler != NULL && scheduler->controller()) {
        scheduler->controller()->increase_load(load);
    }
}

int
HttpParserStage::process_task(Connection* conn)
{
    Request req(conn);
    HttpConnection* http_connection = (HttpConnection*) conn;
    size_t orig_size = http_connection->get_request_data_list().size();
    size_t delta = 0;

    if (!http_connection->do_parse()) {
        // FIXME: if the protocol client sent is not HTTP, is it OK to close
        // the connection right away?
        LOG(WARNING, "corrupted protocol from %s. closing...",
            conn->address_string().c_str());
        increase_load(-1 * http_connection->get_request_data_list().size());
        conn->active_close();
    }
    if (http_connection->is_ready()) {
        delta = http_connection->get_request_data_list().size() - orig_size;
        // notify the controller increase the current load
        increase_load(delta);
        // add it into the next stage
        handler_stage_->sched_add(conn);
    }
    return 0; // release the lock whatever happened
}

int
HttpHandlerStage::kMaxContinuesRequestNumber = 3;

bool
HttpHandlerStage::kAutoTuning = false;

HttpHandlerStage::HttpHandlerStage()
    : Stage("http_handler")
{
    sched_ = new QueueScheduler();
    if (kAutoTuning) {
        sched_->set_controller(new Controller());
        sched_->controller()->set_stage(this);
        LOG(INFO, "auto-tuning for handler stage enabled.");
    }
}

HttpHandlerStage::~HttpHandlerStage()
{
    delete sched_->controller();
}

void
HttpHandlerStage::sched_remove(Connection* conn)
{
    HttpConnection* http_connection = (HttpConnection*) conn;
    if (sched_->controller()) {
        sched_->controller()->increase_load(
            -1 * http_connection->get_request_data_list().size());
    }
    Stage::sched_remove(conn);
}

void
HttpHandlerStage::log_respond(Connection* conn, HttpRequest& request,
                              HttpResponse& response)
{
    const HttpResponseStatus& status = response.responded_status();
    int log_level = INFO;
    if (status.status_code >= 400) {
        log_level = ERROR;
    }
    LOG(log_level, "%s %s %d from %s", request.method_string().c_str(),
        request.complete_uri().c_str(), status.status_code,
        conn->address_string().c_str());
}

void
HttpHandlerStage::trigger_handler(HttpConnection* conn, HttpRequest& request,
                                  HttpResponse& response)
{
    std::list<BaseHttpHandler*> chain;
    if (request.url_rule_item()) {
        chain = request.url_rule_item()->handlers;
    } else {
        // mis-configured, send an error
        response.write_string("This url is not configured.");
        response.respond(
            HttpResponseStatus::kHttpResponseServiceUnavailable);
        goto done;
    }
    if (request.keep_alive() && request.version_minor() == 0) {
        response.add_header("Connection", "Keep-Alive");
    }
    for (UrlRuleItem::HandlerChain::iterator it = chain.begin();
         it != chain.end(); ++it) {
        BaseHttpHandler* handler = *it;
        handler->handle_request(request, response);
        if (conn->has_continuation()) {
            // suspended. return immediately!
            return;
        }
        if (response.is_responded())
            goto done;
    }
    response.respond_with_message(
        HttpResponseStatus::kHttpResponseServiceUnavailable);

done:
    log_respond(conn, request, response);
    response.reset();
}

int
HttpHandlerStage::process_task(Connection* conn)
{
    HttpConnection* http_connection = (HttpConnection*) conn;
    std::list<HttpRequestData>& client_requests =
        http_connection->get_request_data_list();
    HttpResponse response(http_connection);
    size_t orig_size = client_requests.size();

    for (int i = 0; i < kMaxContinuesRequestNumber; i++) {
        if (client_requests.empty())
            break;
        HttpRequest request(http_connection, client_requests.front());
        trigger_handler(http_connection, request, response);
        if (conn->has_continuation()) {
            goto done;
        }
        client_requests.pop_front();
        if (!request.keep_alive()) {
            LOG(DEBUG, "active close after transfer finish");
            conn->set_close_after_finish(true);
            goto done;
        }
    }
    if (!client_requests.empty()) {
        LOG(DEBUG, "remaining req %lu", client_requests.size());
        sched_add(conn);
    }
done:
    if (sched_->controller()) {
        sched_->controller()->decrease_load(orig_size - client_requests.size());
    }
    return response.response_code();
}

void
HttpHandlerStage::resched_continuation(HttpConnection* conn)
{
    sched_add(conn);
    conn->unlock(); // unlock for scheduling
    pipeline_.reschedule_all();
}

}
