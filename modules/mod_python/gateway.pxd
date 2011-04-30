cdef extern from *:
    ctypedef char* const_char_ptr "const char*"
    ctypedef void* const_void_ptr "const void*"
    ctypedef void* void_ptr "void*"

cdef extern from "sys/types.h":
    ctypedef long int off64_t

cdef extern from "http/capi.h" nogil:
    ctypedef unsigned char          u8
    ctypedef unsigned short int     u16
    ctypedef unsigned int           u32
    ctypedef unsigned long long int u64
    ctypedef char          int8
    ctypedef short int     int16
    ctypedef int           int23
    ctypedef long long int int64

    ctypedef void tube_http_request_t
    ctypedef void tube_http_response_t

    short          tube_http_request_get_method(tube_http_request_t* request)
    u64            tube_http_request_get_content_length(tube_http_request_t* request)
    short          tube_http_request_get_transfer_encoding(tube_http_request_t* request)

    short          tube_http_request_get_version_major(tube_http_request_t* request)
    short          tube_http_request_get_version_minor(tube_http_request_t* request)
    int            tube_http_request_get_keep_alive(tube_http_request_t* request)
    const_char_ptr tube_http_request_get_path(tube_http_request_t* request)
    const_char_ptr tube_http_request_get_uri(tube_http_request_t* request)
    const_char_ptr tube_http_request_get_query_string(tube_http_request_t* request)
    const_char_ptr tube_http_request_get_fragment(tube_http_request_t* request)
    const_char_ptr tube_http_request_get_method_string(tube_http_request_t* request)
    void           tube_http_request_set_uri(tube_http_request_t* request, const_char_ptr uri)
    const_char_ptr tube_http_request_find_header_value(tube_http_request_t* request, const_char_ptr key)
    int            tube_http_request_has_header(tube_http_request_t* request, const_char_ptr key)

    char**         tube_http_request_find_header_values(tube_http_request_t* request, const_char_ptr key, size_t* size)

    ssize_t        tube_http_request_read_data(tube_http_request_t* request, void* ptr, size_t size)

    void           tube_http_response_add_header(tube_http_response_t* response, const_char_ptr key, const_char_ptr value)
    void           tube_http_response_set_has_content_length(tube_http_response_t* response, int val)
    int            tube_http_response_has_content_length(tube_http_response_t* response)
    void           tube_http_response_set_content_length(tube_http_response_t* response, u64 length)

    u64            tube_http_response_get_content_length(tube_http_response_t* response)
    void           tube_http_response_disable_prepare_buffer(tube_http_response_t* response)

    ssize_t        tube_http_response_write_data(tube_http_response_t* response, const_void_ptr ptr, size_t size)
    ssize_t        tube_http_response_write_string(tube_http_response_t* response, const_char_ptr str)
    void           tube_http_response_flush_data(tube_http_response_t* response)
    void           tube_http_response_close(tube_http_response_t* response)
    void           tube_http_response_respond(tube_http_response_t* response, int status_code, const_char_ptr reason)
