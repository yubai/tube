#include "pch.h"

#include <string.h>

#include "http/capi.h"
#include "http/interface.h"
#include "http/configuration.h"

typedef tube::BaseHttpHandler* tube_http_handler_handle_t;

#define HTTP_REQUEST(ptr) ((tube::HttpRequest*) (ptr))
#define HTTP_RESPONSE(ptr) ((tube::HttpResponse*) (ptr))
#define HANDLER_IMPL(ptr) ((tube::CHttpHandlerAdapter*) (ptr))

namespace tube {

class CHttpHandlerAdapter : public BaseHttpHandler
{
    tube_http_handler_t* handler_;
public:
    CHttpHandlerAdapter(tube_http_handler_t* handler) : handler_(handler) {
        handler_->handle = this;
    }

    virtual ~CHttpHandlerAdapter() {}

    virtual void handle_request(HttpRequest& request, HttpResponse& response) {
        handler_->handle_request(handler_, &request, &response);
    }

    virtual void load_param() {
        handler_->load_param(handler_);
    }
};

class CHttpHandlerFactoryAdapter : public BaseHttpHandlerFactory
{
    tube_http_handler_desc_t* desc_;
public:
    CHttpHandlerFactoryAdapter(tube_http_handler_desc_t* desc)
        : desc_(desc) {}

    virtual BaseHttpHandler* create() const {
        return new CHttpHandlerAdapter(desc_->create_handler());
    }

    virtual std::string module_name() const {
        return desc_->module_name;
    }

    virtual std::string vendor_name() const {
        return desc_->vender_name;
    }
};

}

#define EXPORT_API extern "C"

#define MAX_OPTION_LEN 128
#define MAX_HEADER_VALUE_LEN 128

EXPORT_API const char*
tube_http_handler_get_name(tube_http_handler_t* handler)
{
    return HANDLER_IMPL(handler->handle)->name().c_str();
}

EXPORT_API char*
tube_http_handler_option(tube_http_handler_t* handler, const char* name)
{
    std::string str = HANDLER_IMPL(handler->handle)->option(name);
    return strndup(str.c_str(), MAX_OPTION_LEN);
}

EXPORT_API void
tube_http_handler_add_option(tube_http_handler_t* handler, const char* name,
                             const char* value)
{
    HANDLER_IMPL(handler->handle)->add_option(name, value);
}

EXPORT_API void
tube_http_handler_descriptor_register(tube_http_handler_desc_t* desc)
{
    tube::BaseHttpHandlerFactory::register_factory(
        new tube::CHttpHandlerFactoryAdapter(desc));
}

// request api
#define REQ_DATA(request) ((request)->request_data_ref())

#define DEF_PRIMITIVE(type, name)                                       \
    extern "C" type tube_http_request_get_##name(tube_http_request_t* req) { \
        return HTTP_REQUEST(req)->name(); }                             \

#define DEF_DATA(type, name, mem)                                       \
    extern "C" type tube_http_request_get_##name(tube_http_request_t* req) { \
        return REQ_DATA(HTTP_REQUEST(req)).mem; }                       \

DEF_PRIMITIVE(short, method);
DEF_PRIMITIVE(u64, content_length);
DEF_PRIMITIVE(short, transfer_encoding);
DEF_PRIMITIVE(short, version_major);
DEF_PRIMITIVE(short, version_minor);
DEF_PRIMITIVE(int, keep_alive);

DEF_DATA(const char*, path, path.c_str());
DEF_DATA(const char*, uri, uri.c_str());
DEF_DATA(const char*, query_string, query_string.c_str());
DEF_DATA(const char*, fragment, fragment.c_str());
DEF_DATA(const char*, method_string, method_string());

EXPORT_API void
tube_http_request_set_uri(tube_http_request_t* request, const char* uri)
{
    HTTP_REQUEST(request)->set_uri(std::string(uri));
}

EXPORT_API const char*
tube_http_request_find_header_value(tube_http_request_t* request,
                                    const char* key)
{
    tube::HttpHeaderEnumerate& headers = HTTP_REQUEST(request)->headers();
    for (size_t i = 0; i < headers.size(); i++) {
        if (headers[i].key == key) {
            return headers[i].value.c_str();
        }
    }
    return NULL;
}

EXPORT_API int
tube_http_request_has_header(tube_http_request_t* request, const char* key)
{
    return tube_http_request_find_header_value(request, key) != NULL;
}

EXPORT_API char**
tube_http_request_find_header_values(tube_http_request_t* request,
                                     const char* key, size_t* size)
{
    std::vector<std::string> values =
        HTTP_REQUEST(request)->find_header_values(key);
    char** array = (char**) malloc(sizeof(char*) * values.size());
    for (size_t i = 0; i < values.size(); i++) {
        array[i] = strndup(values[i].c_str(), MAX_HEADER_VALUE_LEN);
    }
    *size = values.size();
    return array;
}

EXPORT_API ssize_t
tube_http_request_read_data(tube_http_request_t* request, void* ptr,
                            size_t size)
{
    return HTTP_REQUEST(request)->read_data((tube::byte*) ptr, size);
}

// response api
EXPORT_API void
tube_http_response_add_header(tube_http_response_t* response, const char* key,
                              const char* value)
{
    HTTP_RESPONSE(response)->add_header(key, value);
}

EXPORT_API void
tube_http_response_set_has_content_length(tube_http_response_t* response,
                                          int val)
{
    HTTP_RESPONSE(response)->set_has_content_length(val);
}

EXPORT_API int
tube_http_response_has_content_length(tube_http_response_t* response)
{
    return HTTP_RESPONSE(response)->has_content_length();
}

EXPORT_API void
tube_http_response_set_content_length(tube_http_response_t* response,
                                      u64 length)
{
    HTTP_RESPONSE(response)->set_content_length(length);
}

EXPORT_API u64
tube_http_response_get_content_length(tube_http_response_t* response)
{
    return HTTP_RESPONSE(response)->content_length();
}

EXPORT_API void
tube_http_response_disable_prepare_buffer(tube_http_response_t* response)
{
    HTTP_RESPONSE(response)->disable_prepare_buffer();
}

EXPORT_API ssize_t
tube_http_response_write_data(tube_http_response_t* response, const void* ptr,
                              size_t size)
{
    return HTTP_RESPONSE(response)->write_data((const tube::byte*) ptr,
                                               size);
}

EXPORT_API ssize_t
tube_http_response_write_string(tube_http_response_t* response, const char* str)
{
    return HTTP_RESPONSE(response)->write_string(str);
}

EXPORT_API void
tube_http_response_write_file(tube_http_response_t* response, int file_desc,
                              off64_t offset, off64_t length)
{
    HTTP_RESPONSE(response)->write_file(file_desc, offset, length);
}

EXPORT_API void
tube_http_response_flush_data(tube_http_response_t* response)
{
    HTTP_RESPONSE(response)->flush_data();
}

EXPORT_API void
tube_http_response_close(tube_http_response_t* response)
{
    HTTP_RESPONSE(response)->close();
}

EXPORT_API void
tube_http_response_respond(tube_http_response_t* response, int status_code,
                           const char* reason)
{
    HTTP_RESPONSE(response)->respond(tube::HttpResponseStatus(status_code,
                                                              reason));
}
