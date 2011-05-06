#include "pch.h"

#include "http/static_handler.h"
#include "http/http_wrapper.h"
#include "http/configuration.h"
#include "http/http_stages.h"
#include "http/module.h"

#include "core/server.h"
#include "core/stages.h"
#include "core/wrapper.h"
#include "utils/logger.h"

using namespace tube;

class WebServer : public Server
{
    HttpParserStage* parser_stage_;
    HttpHandlerStage* handler_stage_;
public:
    WebServer() {
        utils::logger.set_level(DEBUG);
        parser_stage_ = new HttpParserStage();
        handler_stage_ = new HttpHandlerStage();
    }

    virtual ~WebServer() {
        delete parser_stage_;
        delete handler_stage_;
    }
};

int
main(int argc, char *argv[])
{
    tube_module_initialize_all();
    WebServer server;
    ServerConfig& cfg = ServerConfig::instance();

    cfg.load_config_file("./test/test-conf.yaml");
    server.bind(cfg.address().c_str(), cfg.port().c_str());
    server.initialize_stages();
    server.start_stages();
    server.listen(cfg.listen_queue_size());
    server.main_loop();
    return 0;
}
