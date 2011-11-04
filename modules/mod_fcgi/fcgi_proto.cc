#include "fcgi_proto.h"
#include "core/buffer.h"
#include "utils/logger.h"

namespace tube {
namespace fcgi {

// fcgi protocol header
struct Record
{
    unsigned char  version;
    unsigned char  type;
    unsigned short request_id;
    unsigned short content_length;
    unsigned char  padding_length;
    unsigned char  reserved;

    void append_to_buffer(Buffer& buffer) {
        size_t len = sizeof(Record);
        byte data[len];
        memcpy(data, this, len);
        data[2] = (byte) (request_id >> 8) & 0xFF;
        data[3] = (byte) request_id & 0xFF;
        data[4] = (byte) (content_length >> 8) & 0xFF;
        data[5] = (byte) content_length & 0xFF;
        buffer.append(data, len);
    }

    void append_to_buffer_content_data(Buffer& buffer, const byte* ptr) {
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

    Record(unsigned char record_type)
        : version(1), type(record_type), request_id(1), content_length(0),
          padding_length(0), reserved(0)
    {}

    Record(const byte* ptr) {
        size_t len = sizeof(Record);
        memcpy(this, ptr, len);
        byte* data = (byte*) this;
        request_id = (data[2] << 8) | data[3];
        content_length = (data[4] << 8) | data[5];
    }

    void set_content_length(size_t size) {
        content_length = (unsigned short) size;
        if (content_length % 4 == 0) {
            padding_length = 0;
        } else {
            padding_length = 4 - content_length % 4;
        }
    }

    size_t total_length() const {
        return content_length + padding_length;
    }
};

static const size_t kRecordSize = sizeof(Record);

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
    Record rec(kFcgiBeginRequest); // begin request packet
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
    Record rec(kFcgiParams);
    rec.set_content_length(kv_buf_.size());
    rec.append_to_buffer(buffer_);
    rec.append_to_buffer_content_data(buffer_, &kv_buf_[0]);
    kv_buf_.clear();
    return true;
}

void
FcgiEnvironment::done_environment()
{
    Record rec(kFcgiParams);
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
    Record rec(kFcgiStdin);
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

FcgiResponseReader::FcgiResponseReader(int sock)
    : sock_(sock), eof_(false), has_error_(false), n_bytes_left_(0),
      n_bytes_padding_(0), header_offset_(0)
{
    header_ = new byte[kRecordSize];
}

FcgiResponseReader::~FcgiResponseReader()
{
    delete [] header_;
}

ssize_t
FcgiResponseReader::read_response(byte* data, size_t size)
{
    bool reparse = false;
    while (header_offset_ < kRecordSize) {
        reparse = true;
        ssize_t res = ::read(sock_, header_ + header_offset_,
                             kRecordSize - header_offset_);
        if (res <= 0) {
            return res;
        }
        header_offset_ += res;
    }
    if (reparse) {
        Record rec(header_);
        if (rec.type == kFcgiEndRequest) {
            eof_ = true;
            recv_end_request();
            return has_error_ ? -1 : 0;
        } else if (rec.type == kFcgiStdout || rec.type == kFcgiStderr) {
            n_bytes_left_ = rec.total_length(); // include the padding
            n_bytes_padding_ = rec.padding_length;
        } else {
            LOG(ERROR, "malform packet! check fcgi implementation!");
            has_error_ = true;
            eof_ = true;
            return -1;
        }
    }
    if (n_bytes_left_ == 0) {
        header_offset_ = 0;
        return 0;
    }

    if (n_bytes_left_ < size) {
        size = n_bytes_left_;
    }
    ssize_t res = ::read(sock_, data, size);
    if (res < 0) {
        return res;
    }
    n_bytes_left_ -= res;
    if (n_bytes_padding_ > n_bytes_left_) {
        res -= n_bytes_padding_ - n_bytes_left_; // remove the padding bytes
    }
    return res;
}

void
FcgiResponseReader::recv_end_request()
{
    // receive end request type
    byte data[8];
    size_t off = 0;
    while (off < 8) {
        ssize_t res = ::read(sock_, data + off, 8 - off);
        if (res < 0) {
            has_error_ = true;
            return;
        }
        off += res;
    }
    if (data[4] != 0) {
        has_error_ = true;
    }
}

}
}
