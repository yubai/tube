#include "pch.h"

#include "http/http_wrapper.h"
#include "http/http_parser.h"
#include "utils/misc.h"

namespace tube {

// some common http response status defined by the standard
// copied from http://www.w3.org/Protocols/rfc2616/rfc2616-sec6.html

const HttpResponseStatus
HttpResponseStatus::kHttpResponseContinue =
    HttpResponseStatus(100, "Continue");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseSwitchingProtocols =
    HttpResponseStatus(101, "Switching Protocols");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseOK =
    HttpResponseStatus(200, "OK");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseCreated =
    HttpResponseStatus(201, "Created");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseAccepted =
    HttpResponseStatus(202, "Accepted");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseNonAuthoritativeInformation =
    HttpResponseStatus(203, "Non-Authoritative Information");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseNoContent =
    HttpResponseStatus(204, "No Content");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseResetContent =
    HttpResponseStatus(205, "Reset Content");

const HttpResponseStatus
HttpResponseStatus::kHttpResponsePartialContent =
    HttpResponseStatus(206, "Partial Content");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseMultipleChoices =
    HttpResponseStatus(300, "Multiple Choices");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseMovedPermanently =
    HttpResponseStatus(301, "Moved Permanently");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseFound =
    HttpResponseStatus(302, "Found");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseSeeOther =
    HttpResponseStatus(303, "See Other");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseNotModified =
    HttpResponseStatus(304, "Not Modified");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseUseProxy =
    HttpResponseStatus(305, "Use Proxy");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseTemporaryRedirect =
    HttpResponseStatus(307, "Temporary Redirect");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseBadRequest =
    HttpResponseStatus(400, "Bad Request");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseUnauthorized =
    HttpResponseStatus(401, "Unauthorized");

const HttpResponseStatus
HttpResponseStatus::kHttpResponsePaymentRequired =
    HttpResponseStatus(402, "Payment Required");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseForbidden =
    HttpResponseStatus(403, "Forbidden");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseNotFound =
    HttpResponseStatus(404, "Not Found");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseMethodNotAllowed =
    HttpResponseStatus(405, "Method Not Allowed");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseNotAcceptable =
    HttpResponseStatus(406, "Not Acceptable");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseProxyAuthenticationRequired =
    HttpResponseStatus(407, "Proxy Authentication Required");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseRequestTimeout =
    HttpResponseStatus(408, "Request Time-out");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseConflict =
    HttpResponseStatus(409, "Conflict");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseGone =
    HttpResponseStatus(410, "Gone");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseLengthRequired =
    HttpResponseStatus(411, "Length Required");

const HttpResponseStatus
HttpResponseStatus::kHttpResponsePreconditionFailed =
    HttpResponseStatus(412, "Precondition Failed");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseRequestEntityTooLarge =
    HttpResponseStatus(413, "Request Entity Too Large");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseRequestUriTooLarge =
    HttpResponseStatus(414, "Request-URI Too Large");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseUnsupportedMediaType =
    HttpResponseStatus(415, "Unsupported Media Type");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseRequestedRangeNotSatisfiable =
    HttpResponseStatus(416, "Requested range not satisfiable");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseExpectationFailed =
    HttpResponseStatus(417, "Expectation Failed");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseInternalServerError =
    HttpResponseStatus(500, "Internal Server Error");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseNotImplemented =
    HttpResponseStatus(501, "Not Implemented");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseBadGateway =
    HttpResponseStatus(502, "Bad Gateway");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseServiceUnavailable =
    HttpResponseStatus(503, "Service Unavailable");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseGatewayTimeout =
    HttpResponseStatus(504, "Gateway Time-out");

const HttpResponseStatus
HttpResponseStatus::kHttpResponseHttpVersionNotSupported =
    HttpResponseStatus(505, "HTTP Version not supported");

// end of common http response status definition

HttpResponseStatus::HttpResponseStatus(int code,
                                       const std::string& reason_string)
    : status_code(code), reason(reason_string), is_text(false)
{}

HttpResponseStatus::HttpResponseStatus(const std::string& status_text)
    : status_code(-1), reason(status_text), is_text(true), text(status_text)
{}

HttpRequest::HttpRequest(HttpConnection* conn, const HttpRequestData& request)
    : Request(conn), request_(request)
{
    conn->set_bytes_should_skip(request.content_length);
}

ssize_t
HttpRequest::read_data(byte* ptr, size_t size)
{
    ssize_t nread = Request::read_data(ptr, size);
    HttpConnection* conn = (HttpConnection*) conn_;
    if (nread > 0) {
        conn->set_bytes_should_skip(conn->bytes_should_skip() - nread);
    }
    return nread;
}

std::string
HttpRequest::method_string() const
{
    return request_.method_string();
}

bool
HttpRequest::has_header(const std::string& key) const
{
    const HttpHeaderEnumerate& headers = request_.headers;
    for (size_t i = 0; i < headers.size(); i++) {
        if (headers[i].key == key)
            return true;
    }
    return false;
}

std::vector<std::string>
HttpRequest::find_header_values(const std::string& key) const
{
    const HttpHeaderEnumerate& headers = request_.headers;
    std::vector<std::string> result;
    for (size_t i = 0; i < headers.size(); i++) {
        if (headers[i].key == key)
            result.push_back(headers[i].value);
    }
    return result;
}

static const std::string kHttpQualityIndicator = ";q=";

static HttpHeaderQualityValue
parse_quality_value(const std::string& str)
{
    size_t pos = str.find(kHttpQualityIndicator);
    if (pos == std::string::npos) {
        return HttpHeaderQualityValue(str);
    } else {
        std::string value = str.substr(0, pos);
        std::string quality = str.substr(pos + kHttpQualityIndicator.length());
        return HttpHeaderQualityValue(value, atof(quality.c_str()));
    }
}

static HttpHeaderQualityValues
parse_quality_header(const std::string& str)
{
    HttpHeaderQualityValues res;
    size_t pos = 0;
    size_t endpos = 0;
    while (true) {
        endpos = str.find(", ", pos);
        if (endpos == std::string::npos) {
            if (str.length() - pos > 0) {
                res.push_back(parse_quality_value(str.substr(pos)));
            }
            break;
        }
        if (endpos > pos) {
            res.push_back(parse_quality_value(str.substr(pos, endpos - pos)));
        }
        pos = endpos + 2;
    }
    return res;
}

HttpHeaderQualityValues
HttpRequest::find_header_quality_values(const std::string& key) const
{
    std::vector<std::string> headers = find_header_values(key);
    HttpHeaderQualityValues res;

    for (size_t i = 0; i < headers.size(); i++) {
        const std::string& header = headers[i];
        // parse the header string
        HttpHeaderQualityValues quality_header = parse_quality_header(header);
        res.insert(res.end(), quality_header.begin(), quality_header.end());
    }
    return res;
}

std::string
HttpRequest::find_header_value(const std::string& key) const
{
    const HttpHeaderEnumerate& headers = request_.headers;
    for (size_t i = 0; i < headers.size(); i++) {
        if (headers[i].key == key)
            return headers[i].value;
    }
    return "";
}

const std::string HttpResponse::kHttpVersion = "HTTP/1.1";
const std::string HttpResponse::kHttpNewLine = "\r\n";
const std::string HttpResponse::kHtmlNewLine = "\n";

HttpResponse::HttpResponse(HttpConnection* conn)
    : Response(conn), responded_status_(0, "")
{
    reset();
}

void
HttpResponse::add_header(const std::string& key, const std::string& value)
{
    if (utils::ignore_compare(key, std::string("content-length"))) {
        content_length_ = atoll(value.c_str());
    } else {
        headers_.push_back(HttpHeaderItem(key, value));
    }
}

ssize_t
HttpResponse::write_data(const byte* ptr, size_t size)
{
    if (use_prepare_buffer_) {
        prepare_buffer_.append(ptr, size);
        return size;
    } else {
        return Response::write_data(ptr, size);
    }
}

void
HttpResponse::respond_with_message(const HttpResponseStatus& status)
{
    reset();
    *this << "<html><head><title>" << status.reason << "</title></head>"
          << "<body><h1>" << status.reason << "</h1></body></html>";
    add_header("Content-Type", "text/html");
    respond(status);
}

void
HttpResponse::respond(const HttpResponseStatus& status)
{
    // construct the header and send it long with the prepare buffer
    if (content_length_ < 0)
        set_content_length(prepare_buffer_.size());

    // turn off the prepare buffer to use write_string
    use_prepare_buffer_ = false;
    if (status.is_text) {
        *this << kHttpVersion << " " << status.text << kHttpNewLine;
    } else {
        *this << kHttpVersion << " " << status.status_code << " "
              << status.reason << kHttpNewLine;
    }

    for (size_t i = 0; i < headers_.size(); i++) {
        const HttpHeaderItem& item = headers_[i];
        *this << item.key << ": " << item.value << kHttpNewLine;
    }
    if (has_content_length_) {
        *this << "Content-Length: " << content_length_ << kHttpNewLine;
    }
    *this << kHttpNewLine;

    if (prepare_buffer_.size() > 0) {
        // send the body if have any
        conn_->out_stream().append_buffer(prepare_buffer_);
    }
    is_responded_ = true;
    responded_status_ = status;
}

void
HttpResponse::reset()
{
    prepare_buffer_ = Buffer(); // create a new empty buffer;
    content_length_ = -1;
    headers_.clear();
    has_content_length_ = true;
    use_prepare_buffer_ = true;
    is_responded_ = false;
}

static const size_t kMaxNumberLength = 32;

template <typename T> HttpResponse&
format_number(HttpResponse& response, const char* fmt, T num)
{
    char str[kMaxNumberLength];
    snprintf(str, kMaxNumberLength, fmt, num);
    response.write_string(str);
    return response;
}

HttpResponse&
HttpResponse::operator<<(int num)
{
    return format_number(*this, "%d", num);
}

HttpResponse&
HttpResponse::operator<<(unsigned int num)
{
    return format_number(*this, "%u", num);
}

HttpResponse&
HttpResponse::operator<<(long num)
{
    return format_number(*this, "%ld", num);
}

HttpResponse&
HttpResponse::operator<<(unsigned long num)
{
    return format_number(*this, "%lu", num);
}

HttpResponse&
HttpResponse::operator<<(long long num)
{
    return format_number(*this, "%lld", num);
}

HttpResponse&
HttpResponse::operator<<(unsigned long long num)
{
    return format_number(*this, "%llu", num);
}

// decode and encode url
static int8
char_to_hex(char ch)
{
    static const char* hex_char = "0123456789ABCDEF";
    const char* ptr = strchr(hex_char, ch);
    if (ptr == NULL)
        return -1;
    else
        return ptr - hex_char;
}

static std::string
http_url_decode(const std::string& url)
{
    std::string res;
    for (size_t i = 0; i < url.length(); i++) {
        int8 p = 0, q = 0;
        unsigned char ch = 0;
        if (url[i] != '%')
            goto pass;
        if (i + 2 >= url.length())
            goto pass;
        p = char_to_hex(url[i + 1]);
        q = char_to_hex(url[i + 2]);
        if (p == -1 || q == -1)
            goto pass;
        ch = (p << 4) | q;
        res += ch;
        i += 2;
        continue;
    pass:
        res += url[i];
    }
    return res;
}

std::string
HttpRequest::url_decode(const std::string& url)
{
    return http_url_decode(url);
}

void
HttpRequest::suspend_continuation(void* continuation)
{
    HttpConnection* conn = (HttpConnection*) conn_;
    conn->set_continuation(continuation);
}

void*
HttpRequest::restore_continuation()
{
    HttpConnection* conn = (HttpConnection*) conn_;
    void* res = conn->get_continuation();
    conn->reset_continuation();
    return res;
}

}
