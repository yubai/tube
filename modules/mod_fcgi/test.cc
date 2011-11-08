#include <stdio.h>

#include "connection_pool.h"
#include "fcgi_proto.h"

using namespace tube;

fcgi::ConnectionPool* conn_pool = NULL;

static const std::string kv_pairs[][2] = {
    {"QUERY_STRING", ""},
    {"REQUEST_METHOD", "GET"},
    {"CONTENT_TYPE", ""},
    {"CONTENT_LENGTH", ""},
    {"SCRIPT_NAME", "/info.php"},
    {"REQUEST_URI", "/info.php"},
    //{"DOCUMENT_URI", "/info.php"},
    //{"DOCUMENT_ROOT", "/usr/share/nginx/html"},
    {"SERVER_PROTOCOL", "HTTP/1.1"},
    {"GATEWAY_INTERFACE", "CGI/1.1"},
    //{"REDIRECT_STATUS", "200"},
    {"SCRIPT_FILENAME", "/usr/share/nginx/html/finfo.php"},
    {"HTTP_HOST", "localhost"},
};

static const size_t n_kv_pairs = sizeof(kv_pairs) / sizeof(std::string) / 2;

static bool
do_test(int sock)
{
    fcgi::FcgiEnvironment env(sock);
    Buffer read_buffer;
    fcgi::FcgiResponseParser parser(read_buffer);
    fcgi::FcgiContentParser content_parser;
    env.begin_request();
    for (size_t i = 0; i < n_kv_pairs; i++) {
        fprintf(stderr, "add header %s: %s\n", kv_pairs[i][0].c_str(),
                kv_pairs[i][1].c_str());
        env.set_environment(kv_pairs[i][0], kv_pairs[i][1]);
    }
    env.commit_environment();
    env.prepare_request(0); // zero size because it's GET
    while (env.result_buffer().size() > 0) {
        env.result_buffer().write_to_fd(sock);
    }
    env.done_request();

    // receiving the response
    while (true) {
        fcgi::Record* rec = parser.extract_record();
        if (rec == NULL) {
            if (read_buffer.read_from_fd(sock) < 0) {
                return false;
            }
            continue;
        }
        switch (rec->type) {
        case fcgi::Record::kFcgiEndRequest:
            // EOF
            parser.bypass_content(rec->total_length());
            goto done;
        case fcgi::Record::kFcgiStdout:
        case fcgi::Record::kFcgiStderr:
            size_t cnt = rec->content_length;
            for (Buffer::PageIterator it = read_buffer.page_begin();
                 it != read_buffer.page_end(); ++it) {
                size_t len = 0;
                const char* ptr =
                    (const char*) read_buffer.get_page_segment(*it, &len);
                if (cnt < len) len = cnt;
                if (!content_parser.is_done()) {
                    int rs = content_parser.parse(ptr, len);
                    if (rs < len) {
                        write(1, ptr + rs, len - rs);
                    }
                } else {
                    write(1, ptr, len);
                }
                cnt -= len;
                if (cnt == 0) break;
            }
            parser.bypass_content(rec->total_length());
        }
        delete rec;
    }

done:
    if (content_parser.has_error()) fprintf(stderr, "error!\n");
    if (content_parser.is_streaming()) fprintf(stderr, "streaming...\n");
    for (int i = 0; i < content_parser.headers().size(); i++) {
        fprintf(stderr, "header %s: %s\n", 
                content_parser.headers()[i].first.c_str(),
                content_parser.headers()[i].second.c_str());
    }
    return true;
}

const int kNRetries = 1;

int main()
{
    // setup the connection pool
    conn_pool = new fcgi::TcpConnectionPool("127.0.0.1:9000", 128);
    bool is_connected = false;
    int sock = conn_pool->alloc_connection(is_connected);
    if (!is_connected) {
        conn_pool->connect(sock);
    }
    for (int i = 0; i < kNRetries; i++) {
        while (!do_test(sock)) {
            fprintf(stderr, "error, retry connection");
            conn_pool->connect(sock);
        }
    }
    delete conn_pool;
}
