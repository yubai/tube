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

    if (!http_connection->do_parse()) {
        // FIXME: if the protocol client sent is not HTTP, is it OK to close
        // the connection right away?
        LOG(WARNING, "corrupted protocol from %s. closing...",
            conn->address_string().c_str());
        increase_load(-1 * http_connection->get_request_data_list().size());
        conn->active_close();
    }
    std::list<HttpRequestData>& request_data_list =
        http_connection->get_request_data_list();
    if (!request_data_list.empty()) {
        // notify the controller increase the current load
        increase_load(request_data_list.size() - orig_size);
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

int
HttpHandlerStage::process_task(Connection* conn)
{
    HttpConnection* http_connection = (HttpConnection*) conn;
    std::list<BaseHttpHandler*> chain;
    std::list<HttpRequestData>& client_requests =
        http_connection->get_request_data_list();
    HttpResponse response(conn);
    size_t orig_size = client_requests.size();

    for (int i = 0; i < kMaxContinuesRequestNumber; i++) {
        if (client_requests.empty())
            break;
        HttpRequest request(conn, client_requests.front());
        client_requests.pop_front();
        if (request.url_rule_item()) {
            chain = request.url_rule_item()->handlers;
        } else {
            // mis-configured, send an error
            response.write_string("This url is not configured.");
            response.respond(
                HttpResponseStatus::kHttpResponseServiceUnavailable);
            continue;
        }
        if (request.keep_alive() && request.version_minor() == 0) {
            response.add_header("Connection", "Keep-Alive");
        }
        for (UrlRuleItem::HandlerChain::iterator it = chain.begin();
             it != chain.end(); ++it) {
            BaseHttpHandler* handler = *it;
            handler->handle_request(request, response);
            if (response.is_responded())
                break;
        }
        if (!response.is_responded()) {
            response.respond_with_message(
                HttpResponseStatus::kHttpResponseServiceUnavailable);
        }
        response.reset();
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

}
