// -*- mode: c++ -*-

#ifndef _HTTP_STAGES_H_
#define _HTTP_STAGES_H_

#include "core/stages.h"
#include "http/interface.h"

namespace tube {

class HttpConnectionFactory : public ConnectionFactory
{
public:
    static int kDefaultTimeout;
    static bool kCorkEnabled;
    virtual Connection* create_connection(int fd);
    virtual void        destroy_connection(Connection* conn);
};

class HttpParserStage : public ParserStage
{
    Stage* handler_stage_;
public:
    HttpParserStage();
    virtual ~HttpParserStage();

    virtual void initialize();
protected:
    int process_task(Connection* conn);
    void increase_load(long load);
};

class HttpHandlerStage : public Stage
{
public:
    static int kMaxContinuesRequestNumber;
    static bool kAutoTuning;

    HttpHandlerStage();
    virtual ~HttpHandlerStage();

    void    resched_continuation(HttpConnection* conn);

    virtual void sched_remove(Connection* conn);
protected:
    int process_task(Connection* conn);
private:
    void trigger_handler(HttpConnection* conn, HttpRequest& request,
                         HttpResponse& response);
    void log_respond(Connection* conn, HttpRequest& request,
                     HttpResponse& response);
};

}

#endif /* _HTTP_STAGES_H_ */
