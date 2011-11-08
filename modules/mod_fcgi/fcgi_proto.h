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

    Buffer& result_buffer() { return buffer_; }

private:
    void done_environment();
};

// fcgi protocol header
struct Record
{
    unsigned char  version;
    unsigned char  type;
    unsigned short request_id;
    unsigned short content_length;
    unsigned char  padding_length;
    unsigned char  reserved;

    static const unsigned char kFcgiBeginRequest = 1;
    static const unsigned char kFcgiAbortRequest = 2;
    static const unsigned char kFcgiEndRequest = 3;
    static const unsigned char kFcgiParams = 4;
    static const unsigned char kFcgiStdin = 5;
    static const unsigned char kFcgiStdout = 6;
    static const unsigned char kFcgiStderr = 7;
    static const unsigned char kFcgiData = 8;
    static const unsigned char kFcgiGetValues = 9;
    static const unsigned char kFcgiGetValuesResult = 10;
    static const unsigned char kFcgiUnknownType = 11;

    Record(unsigned char record_type);
    Record(const byte* ptr);
    Record(const Record& rhs);

    void append_to_buffer(Buffer& buffer);
    void append_to_buffer_content_data(Buffer& buffer, const byte* ptr);
    void set_content_length(size_t size);
    size_t total_length() const;
};

static const size_t kRecordSize = sizeof(Record);

class FcgiResponseParser
{
    Buffer& buffer_;
public:
    FcgiResponseParser(Buffer& buffer);
    virtual ~FcgiResponseParser();

    /**
     * Extract a record header from buffer. if incomplete buffer, return a NULL
     * Record header need to freed after use.
     */
    Record* extract_record();
    void    bypass_content(size_t tot_size);
    void    copy_content(Buffer& buf, size_t size);
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
    bool is_streaming() const { return is_streaming_; }
    const std::string& status_text() const { return status_text_; }

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
    bool        is_streaming_;
    std::string status_text_;

    Pair              header_line_;
    std::vector<Pair> headers_;
};

}
}

#endif /* _FCGI_PROTO_H_ */
