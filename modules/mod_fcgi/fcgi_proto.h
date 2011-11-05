// -*- mode: c++ -*-
#ifndef _FCGI_PROTO_H_
#define _FCGI_PROTO_H_

#include <string>
#include <unistd.h>

#include "core/buffer.h"
#include "utils/misc.h"

namespace tube {
namespace fcgi {

class FcgiEnvironment
{
    int sock_;
    int state_;
    Buffer buffer_;
    std::vector<byte> kv_buf_;
public:
    FcgiEnvironment(int sock);
    virtual ~FcgiEnvironment();

    bool begin_request();
    bool set_environment(const std::string& name, const std::string& value);
    bool commit_environment();

    int  prepare_request(size_t size);
    void done_request();

private:
    void done_environment();
};

class FcgiResponseReader
{
    int sock_;
    bool eof_;
    bool has_error_;
    size_t n_bytes_left_;
    size_t n_bytes_padding_;

    byte* header_;
    size_t header_offset_;
public:
    FcgiResponseReader(int sock);
    virtual ~FcgiResponseReader();

    ssize_t read_response(byte* data, size_t size);
    bool    eof() { return eof_; }
    bool    has_error() { return has_error_; }
private:
    void    recv_malform_packet(size_t size);
    void    recv_end_request();
};

class FcgiContentParser
{
public:
    typedef std::pair<std::string, std::string> Pair;

    FcgiContentParser();
    virtual ~FcgiContentParser();

    void init();
    int  parse(const char* str, size_t size);
    bool has_error() const;
    bool is_done() const { return done_parse_; }

    std::vector<Pair> headers() const { return headers_; }

private:
    void init_parser();
protected:
    void push_name(const char* str, size_t size);
    void push_value(const char* str, size_t size);
    void push_header();
    void done_parse();
private:
    const char* name_mark_;
    const char* value_mark_;
    int         state_;
    bool        done_parse_;

    Pair              header_line_;
    std::vector<Pair> headers_;
};

}
}

#endif /* _FCGI_PROTO_H_ */
