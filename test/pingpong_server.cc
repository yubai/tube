#include <string>
#include <iostream>
#include <cassert>
#include <signal.h>

#include "utils/logger.h"
#include "utils/misc.h"
#include "core/stages.h"
#include "core/server.h"
#include "core/wrapper.h"

using namespace tube;

class PingPongParser : public ParserStage
{
public:
    virtual int process_task(Connection* conn) {
        Buffer& buf = conn->in_stream().buffer();
        Response res(conn);
        size_t len = 0;
        while (true) {
            byte* data = buf.get_page_segment(buf.first_page(), &len);
            if (len == 0)
                break;
            if (res.write_data(data, len) <= 0) {
                res.close();
            }
            buf.pop(len);
        }
        return res.response_code();
    }
};

class PingPongServer : public Server
{
    PingPongParser* parser_stage_;
public:
    PingPongServer() {
        utils::set_fdtable_size(20000);
        utils::logger.set_level(DEBUG);

        parser_stage_ = new PingPongParser();
    }

    virtual ~PingPongServer() {
        delete parser_stage_;
    }
};

static PingPongServer server;

static void
on_quit_signal(int sig)
{
    exit(0);
}

int
main(int argc, char *argv[])
{
    server.bind("0.0.0.0", "7000");
    server.initialize_stages();
    server.start_stages();
    server.listen(128);

    ::signal(SIGINT, on_quit_signal);
    server.main_loop();
    return 0;
}

