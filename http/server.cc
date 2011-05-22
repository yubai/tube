#include "pch.h"

#include <unistd.h>
#include <signal.h>

#include "http/static_handler.h"
#include "http/http_wrapper.h"
#include "http/configuration.h"
#include "http/http_stages.h"
#include "http/module.h"

#include "core/server.h"
#include "core/stages.h"
#include "core/wrapper.h"
#include "utils/logger.h"
#include "utils/exception.h"

namespace tube {

class WebServer : public Server
{
    HttpParserStage* parser_stage_;
    HttpHandlerStage* handler_stage_;
public:
    WebServer() {
        parser_stage_ = new HttpParserStage();
        handler_stage_ = new HttpHandlerStage();
    }

    virtual ~WebServer() {
        delete parser_stage_;
        delete handler_stage_;
    }
};

}

static std::string pid_file;
static std::string conf_file;
static std::string module_path;
static int global_uid = -1;

static void
show_usage(int argc, char* argv[])
{
    printf("Usage: %s -c config_file [ -m module_path -u uid -p pidfile ]\n",
           argv[0]);
    puts("");
    puts("  -c\t\t Specify the configuration file. Required.");
    puts("  -m\t\t Specify the module path. Optional.");
    puts("  -u\t\t Set uid before server starts. Optional.");
    puts("  -p\t\t Output pid into a file. Optional");
    puts("  -h\t\t Help");
    exit(-1);
}

static void
parse_opt(int argc, char* argv[])
{
    int opt = 0;
    while ((opt = getopt(argc, argv, "p:c:m:u:h")) != -1) {
        switch (opt) {
        case 'p':
            pid_file = std::string(optarg);
            break;
        case 'c':
            conf_file = std::string(optarg);
            break;
        case 'm':
            module_path = std::string(optarg);
            break;
        case 'u':
            global_uid = atoi(optarg);
            break;
        case 'h':
            show_usage(argc, argv);
            break;
        default:
            fprintf(stderr, "Try `%s -h' for help.\n", argv[0]);
            exit(-1);
            break;
        }
    }
    if (conf_file == "") {
        fprintf(stderr, "Must specify the configuration file.\n");
        exit(-1);
    }
}

static void
webserver_init(int argc, char* argv[])
{
    parse_opt(argc, argv);

    // setting up uid
    if (global_uid >= 0) {
        if (setuid(global_uid) < 0) {
            perror("setuid");
            exit(-1);
        }
    }
    // output pid to pid_file
    if (pid_file != "") {
        std::ofstream fout(pid_file.c_str());
        fout << getpid();
    }

    // loading the configuration files
    tube::ServerConfig& cfg = tube::ServerConfig::instance();
    cfg.set_config_filename(conf_file);
    cfg.load_static_config();

    // laoding and initialize all modules
    tube_module_load_dir(module_path.c_str());
    tube_module_initialize_all();
}

static void
on_quit_signal(int sig)
{
    exit(0);

}

int
main(int argc, char* argv[])
{
    webserver_init(argc, argv);
    tube::ServerConfig& cfg = tube::ServerConfig::instance();
    tube::WebServer server;
    try {
        cfg.load_config();
        server.bind(cfg.address().c_str(), cfg.port().c_str());
        server.listen(cfg.listen_queue_size());
        server.initialize_stages();
        server.start_stages();
        ::signal(SIGINT, on_quit_signal);

        server.main_loop();
    } catch (tube::utils::SyscallException ex) {
        fprintf(stderr, "Cannot start server: %s\n", ex.what());
        exit(-1);
    } catch (const std::invalid_argument& ex) {
        fprintf(stderr, "Invalid argument: %s\n", ex.what());
        exit(-1);
    }

    return 0;
}
