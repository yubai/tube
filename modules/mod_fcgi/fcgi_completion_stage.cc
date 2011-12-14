#include <cstdio>
#include <limits.h>
#include <signal.h>

#include "fcgi_completion_stage.h"

namespace tube {
namespace fcgi {

const size_t FcgiCompletionContinuation::kTaskBufferLimit = 64; // just for test

FcgiCompletionStage::FcgiCompletionStage()
    : PollStage("fcgi_completion")
{}

FcgiCompletionStage::~FcgiCompletionStage()
{}

void
FcgiCompletionStage::initialize()
{
    write_back_stage_ = pipeline_.find_stage("write_back");
}

bool
FcgiCompletionStage::sched_add(Connection* conn)
{
    HttpConnection* connection = (HttpConnection*) conn;
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) connection->get_continuation();
    utils::Lock lk(mutex_);
    current_poller_ = (current_poller_ + 1) % pollers_.size();
    Poller& poller = *pollers_[current_poller_];

    int flag = kPollerEventHup | kPollerEventError;
    // fprintf(stderr, "completion roger that!\n");
    if (cont->status == kCompletionReadClient) {
        return poller.add_fd(conn->fd(), conn, kPollerEventRead | flag);
    } else if (cont->status == kCompletionWriteFcgi) {
        return poller.add_fd(cont->sock_fd, conn, kPollerEventWrite | flag);
    } else if (cont->status == kCompletionReadFcgi) {
        return poller.add_fd(cont->sock_fd, conn, kPollerEventRead | flag);
    } else {
        fprintf(stderr, "wtf\n");
        return false;
    }
}

void
FcgiCompletionStage::sched_remove(Connection* conn)
{
    HttpConnection* connection = (HttpConnection*) conn;
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) connection->get_continuation();
    if (cont == NULL) return;
    utils::Lock lk(mutex_);
    for (size_t i = 0; i < pollers_.size(); i++) {
        if (cont->status == kCompletionReadClient) {
            if (pollers_[i]->remove_fd(conn->fd())) return;
        } else {
            if (pollers_[i]->remove_fd(cont->sock_fd)) return;
        }
    }
}

void
FcgiCompletionStage::main_loop()
{
    Poller* poller = PollerFactory::instance().create_poller(poller_name_);
    Poller::EventCallback evthdl =
        boost::bind(&FcgiCompletionStage::handle_connection, this,
                    boost::ref(*poller), _1, _2);
    poller->set_event_handler(evthdl);
    add_poll(poller);
    poller->handle_event(timeout_);
    PollerFactory::instance().destroy_poller(poller);
    delete poller;
}

void
FcgiCompletionStage::handle_connection(Poller& poller, Connection* conn,
                                       PollerEvent evt)
{
    if ((evt & kPollerEventHup) || (evt & kPollerEventError)) {
        handle_error(poller, conn);
    } else if (evt & kPollerEventRead) {
        handle_read(poller, conn);
    } else if (evt & kPollerEventWrite) {
        handle_write(poller, conn);
    }
}

bool
FcgiCompletionStage::run_parser(FcgiCompletionContinuation* cont)
{
    Record* rec = cont->response_parser.extract_record();
    if (rec == NULL) {
        return false;
    }
    size_t n_left = rec->content_length;
    switch (rec->type) {
    case Record::kFcgiEndRequest:
        cont->response_parser.bypass_content(rec->total_length());
        if (cont->content_parser.is_done()) {
            cont->task_len = 0;
        }
        break;
    case Record::kFcgiStdout:
    case Record::kFcgiStderr:
        for (Buffer::PageIterator it = cont->task_buffer.page_begin();
             it != cont->task_buffer.page_end(); ++it) {
            size_t len = 0;
            const byte* ptr =
                (const byte*) cont->task_buffer.get_page_segment(*it, &len);
            if (n_left < len) len = n_left;
            // feed the string into content parser
            int rs = 0;
            if (!cont->content_parser.is_done()) {
                rs = cont->content_parser.parse((const char*) ptr, len);
            }
            if (cont->content_parser.has_error()) {
                goto error;
            }
            cont->output_buffer.append(ptr + rs, len - rs);
            n_left -= len;
            if (n_left == 0) break;
        }
        cont->response_parser.bypass_content(rec->total_length());
        break;
    default:
        cont->response_parser.bypass_content(rec->total_length());
        break;
    }
    delete rec;
    return true;
error:
    delete rec;
    return false;
}

void
FcgiCompletionStage::handle_read(Poller& poller, Connection* conn)
{
    // fprintf(stderr, "handle_read\n");
    // status kCompletionReadClient and kCompletionReadFcgi are supposed to be
    // handled in here
    HttpConnection* connection = (HttpConnection*) conn;
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) connection->get_continuation();
    ssize_t rs = -1;
    if (cont->status == kCompletionReadClient) {
        // transfer from conn->fd() to task_buffer
        rs = cont->task_buffer.read_from_fd(connection->fd());
        if (rs < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            handle_error(poller, conn);
        }
        if (rs > 0) {
            cont->task_len -= rs;
        }
        transfer_status(poller, connection);
    } else if (cont->status == kCompletionReadFcgi) {
        // transfer from cont->sock_fd to task_buffer
        rs = cont->task_buffer.read_from_fd(cont->sock_fd);
        if (rs < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            handle_error(poller, connection);
        }
        // need to parse the buffer using cont->response_parser and
        // cont->content_parser
        while (run_parser(cont)) {
            FcgiCompletionStatus status = transfer_status(poller, connection);
            if (status == kCompletionHeadersDone) {
                // headers done
                return;
            }
            if (cont->content_parser.has_error()) {
                handle_error(poller, conn);
                return;
            }
        }
    }
}

void
FcgiCompletionStage::handle_write(Poller& poller, Connection* conn)
{
    // only status kCompletionWriteFcgi should be handled
    // fprintf(stderr, "handle write\n");
    HttpConnection* connection = (HttpConnection*) conn;
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) connection->get_continuation();
    ssize_t rs = -1;
    if (cont->status == kCompletionWriteFcgi) {
        // write data from task_buffer into cont->sock_fd
        rs = cont->task_buffer.write_to_fd(cont->sock_fd);
        if (rs < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            handle_error(poller, conn);
        }
        transfer_status(poller, connection);
    }
}

void
FcgiCompletionStage::handle_error(Poller& poller, Connection* conn)
{
    // fprintf(stderr, "handle_error %p\n", conn);
    HttpConnection* connection = (HttpConnection*) conn;
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) connection->get_continuation();

    if (cont->status == kCompletionWriteFcgi
        || cont->status == kCompletionReadFcgi) {
        poller.remove_fd(cont->sock_fd);
        cont->need_reconnect = true;
    } else {
        poller.remove_fd(conn->fd());
    }
    cont->status = kCompletionError;
    // fprintf(stderr, "resched %p\n", conn);
    conn->resched_continuation();
}

FcgiCompletionStatus
FcgiCompletionStage::transfer_status(Poller& poller, HttpConnection* conn)
{
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) conn->get_continuation();
    FcgiCompletionStatus new_status = cont->status;

    if (cont->status == kCompletionReadClient) {
        if (cont->task_buffer.size()
            > FcgiCompletionContinuation::kTaskBufferLimit
            || cont->task_len == 0) {
            poller.remove_fd(conn->fd());
            new_status = cont->status = kCompletionWriteFcgi;
            poller.add_fd(cont->sock_fd, conn, kPollerEventWrite
                          | kPollerEventHup | kPollerEventError);
        }
    } else if (cont->status == kCompletionWriteFcgi) {
        if (cont->task_buffer.size() == 0 && cont->task_len == 0) {
            new_status = cont->status = kCompletionReadFcgi;
            cont->task_len = -1;
            poller.change_fd(cont->sock_fd, conn, kPollerEventRead
                             | kPollerEventHup | kPollerEventError);
        }
    } else if (cont->status == kCompletionReadFcgi) {
        if (!cont->content_parser.is_done()) {
            return new_status;
        }
        bool streaming = cont->content_parser.is_streaming();
        if (cont->task_len == -1) {
            new_status = cont->status = kCompletionHeadersDone;
            cont->task_len = INT_MAX;
            poller.remove_fd(cont->sock_fd);
            // fprintf(stderr, "resched %p\n", conn);
            conn->resched_continuation();
        } else if (cont->task_len == 0) {
            // EOF, no fcgi read should be done anymore.
            new_status = cont->status = kCompletionEOF;
            poller.remove_fd(cont->sock_fd);
            // fprintf(stderr, "resched %p\n", conn);
            conn->resched_continuation();
        } else if (streaming && cont->output_buffer.size()
                   > FcgiCompletionContinuation::kTaskBufferLimit) {
            // otherwise, transfer is not completed.
            new_status = cont->status = kCompletionContinue;
            poller.remove_fd(cont->sock_fd);
            stream_write_back(conn);
        }
    }
    return new_status;
}

void
FcgiCompletionStage::stream_write_back(HttpConnection* conn)
{
    FcgiCompletionContinuation* cont =
        (FcgiCompletionContinuation*) conn->get_continuation();
    conn->out_stream().append_buffer(cont->output_buffer);
    conn->set_cork();
    // fprintf(stderr, "stream write back\n");
    write_back_stage_->sched_add(conn);
}

}
}
