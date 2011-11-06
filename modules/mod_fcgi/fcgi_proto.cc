#include "fcgi_proto.h"
#include "core/buffer.h"
#include "utils/logger.h"

namespace tube {
namespace fcgi {


void
Record::append_to_buffer(Buffer& buffer)
{
    size_t len = sizeof(Record);
    byte data[len];
    memcpy(data, this, len);
    data[2] = (byte) (request_id >> 8) & 0xFF;
    data[3] = (byte) request_id & 0xFF;
    data[4] = (byte) (content_length >> 8) & 0xFF;
    data[5] = (byte) content_length & 0xFF;
    buffer.append(data, len);
}

void
Record::append_to_buffer_content_data(Buffer& buffer, const byte* ptr)
{
    if (content_length == 0) {
        // has nothing to do, don't access that pointer
        return;
    }
    // ptr has at least content_length size! otherwise will overflow
    buffer.append(ptr, content_length);
    // align the data with zero
    if (padding_length != 0) {
        byte data[padding_length];
        memset(data, 0, padding_length);
        buffer.append(data, padding_length);
    }
}

Record::Record(unsigned char record_type)
    : version(1), type(record_type), request_id(1), content_length(0),
      padding_length(0), reserved(0)
{}

Record::Record(const byte* ptr)
{
    size_t len = sizeof(Record);
    memcpy(this, ptr, len);
    byte* data = (byte*) this;
    request_id = (data[2] << 8) | data[3];
    content_length = (data[4] << 8) | data[5];
}

Record::Record(const Record& rhs)
{
    memcpy(this, &rhs, kRecordSize);
}

void
Record::set_content_length(size_t size)
{
    content_length = (unsigned short) size;
    if (content_length % 4 == 0) {
        padding_length = 0;
    } else {
        padding_length = 4 - content_length % 4;
    }
}

size_t
Record::total_length() const
{
    return content_length + padding_length;
}


FcgiEnvironment::FcgiEnvironment(int sock)
    : sock_(sock), state_(0)
{}

FcgiEnvironment::~FcgiEnvironment()
{}

bool
FcgiEnvironment::begin_request()
{
    if (state_ != 0) {
        return false;
    }
    Record rec(Record::kFcgiBeginRequest); // begin request packet
    byte data[8] = {0, 1, 1, 0, 0, 0, 0, 0}; // responder, keep connection
    rec.set_content_length(8);
    rec.append_to_buffer(buffer_);
    rec.append_to_buffer_content_data(buffer_, data);
    state_++;
    return true;
}

static void
append_int(std::vector<byte>& buf, unsigned int v)
{
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v) & 0xFF);
}

static size_t kEnvironmentSizeLimit = 65535;

bool
FcgiEnvironment::set_environment(const std::string& name,
                                 const std::string& value)
{
    if (state_ != 1) {
        return false;
    }
    if (kv_buf_.size() >= kEnvironmentSizeLimit) {
        return false;
    }
    if (name.length() > 127) {
        append_int(kv_buf_, (unsigned int) name.length());
    } else {
        kv_buf_.push_back(name.length());
    }
    if (value.length() > 127) {
        append_int(kv_buf_, (unsigned int) value.length());
    } else {
        kv_buf_.push_back(value.length());
    }
    kv_buf_.insert(kv_buf_.end(), name.begin(), name.end());
    kv_buf_.insert(kv_buf_.end(), value.begin(), value.end());
    return true;
}

bool
FcgiEnvironment::commit_environment()
{
    if (state_ != 1) {
        return false;
    }
    if (kv_buf_.empty()) {
        return false;
    }
    Record rec(Record::kFcgiParams);
    rec.set_content_length(kv_buf_.size());
    rec.append_to_buffer(buffer_);
    rec.append_to_buffer_content_data(buffer_, &kv_buf_[0]);
    kv_buf_.clear();
    return true;
}

void
FcgiEnvironment::done_environment()
{
    Record rec(Record::kFcgiParams);
    rec.set_content_length(0);
    rec.append_to_buffer(buffer_);
}

int
FcgiEnvironment::prepare_request(size_t size)
{
    if (state_ != 1) {
        return -1;
    }
    // send a black param packets indicate application start.
    done_environment();
    // preparing the stdin from data
    Record rec(Record::kFcgiStdin);
    rec.set_content_length(size);
    rec.append_to_buffer(buffer_);
    // all done. fire the request
    while (buffer_.size() > 0) {
        buffer_.write_to_fd(sock_);
    }
    return rec.total_length();
}

void
FcgiEnvironment::done_request()
{
    state_ = 0;
}

FcgiResponseParser::FcgiResponseParser(Buffer& buffer)
    : buffer_(buffer)
{}

FcgiResponseParser::~FcgiResponseParser()
{}

Record*
FcgiResponseParser::extract_record()
{
    byte data[kRecordSize];
    if (!buffer_.copy_front(data, kRecordSize)) {
        return NULL;
    }
    Record rec(data);
    if (buffer_.size() < rec.total_length()) {
        return NULL; // data hasn't been received completely
    }
    buffer_.pop(kRecordSize);
    return new Record(rec);
}

void
FcgiResponseParser::bypass_content(size_t size)
{
    buffer_.pop(size);
}

void
FcgiResponseParser::copy_content(Buffer& buf, size_t size)
{
    buffer_.copy_front(buf, size);
    buffer_.pop(size);
}

FcgiContentParser::FcgiContentParser()
{
    init();
}

FcgiContentParser::~FcgiContentParser()
{}

void
FcgiContentParser::init()
{
    done_parse_ = false;
    header_line_.first.clear();
    header_line_.second.clear();
    headers_.clear();

    init_parser();
}

void
FcgiContentParser::push_name(const char* str, size_t size)
{
    header_line_.first.insert(header_line_.first.end(), str, str + size);
}

void
FcgiContentParser::push_value(const char* str, size_t size)
{
    header_line_.second.insert(header_line_.second.end(), str, str + size);
}

void
FcgiContentParser::push_header()
{
    headers_.push_back(header_line_);
}

void
FcgiContentParser::done_parse()
{
    done_parse_ = true;
}

}
}
