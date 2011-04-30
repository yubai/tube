from gateway cimport *
from libc.stdlib cimport calloc, malloc, free

cdef extern from "Python.h":
    object PyBytes_FromStringAndSize(char *s, Py_ssize_t len)

cdef class Request:
    cdef tube_http_request_t* __req

    def __cinit__(self):
        self.__req = NULL

    def __init__(self, long obj):
        cdef long ptr = obj
        self.__req = <tube_http_request_t*> ptr

    def get_method(self):
        return tube_http_request_get_method_string(self.__req)

    def get_content_length(self):
        return tube_http_request_get_content_length(self.__req)

    def get_protocol_version(self):
        return (tube_http_request_get_version_major(self.__req),
                tube_http_request_get_version_minor(self.__req))

    def is_keep_alive(self):
        if tube_http_request_get_keep_alive(self.__req):
            return True
        return False

    def get_path(self):
        return tube_http_request_get_path(self.__req)

    def get_uri(self):
        return tube_http_request_get_uri(self.__req)

    def get_query_string(self):
        return tube_http_request_get_query_string(self.__req)

    def get_fragment(self):
        return tube_http_request_get_fragment(self.__req)

    def set_uri(self, const_char_ptr uri):
        tube_http_request_set_uri(self.__req, uri)

    def find_header_value(self, const_char_ptr key):
        return tube_http_request_find_header_value(self.__req, key)

    def has_header_value(self, const_char_ptr key):
        if tube_http_request_has_header(self.__req, key):
            return True
        return False

    def find_header_values(self, const_char_ptr key):
        cdef size_t size = 0
        cdef char** ret = tube_http_request_find_header_values(self.__req, key,
                                                               &size)
        cdef char* line = NULL
        cdef size_t i = 0
        res = []
        for i in range(size):
            line = ret[i]
            res.append(line)
            free(line)
        free(ret)
        return res

    def read(self, ssize_t size):
        cdef ssize_t nread = -1
        cdef char* ptr = <char*> malloc(size)
        cdef bytes res
        with nogil:
            nread = tube_http_request_read_data(self.__req, ptr, size)
        if nread < 0:
            free(ptr)
            return None
        res = PyBytes_FromStringAndSize(ptr, nread)
        free(ptr)
        return res

cdef class Response:
    cdef tube_http_response_t* __res

    def __cinit__(self):
        self.__res = NULL

    def __init__(self, long obj):
        cdef long ptr = obj
        self.__res = <tube_http_response_t*> ptr

    def add_header(self, const_char_ptr key, const_char_ptr value):
        tube_http_response_add_header(self.__res, key, value)

    def set_has_content_length(self, val):
        if val:
            tube_http_response_set_has_content_length(self.__res, 1)
        else:
            tube_http_response_set_has_content_length(self.__res, 0)

    def has_content_length(self):
        if (tube_http_response_has_content_length(self.__res)):
            return True
        return False

    def set_content_length(self, u64 val):
        tube_http_response_set_content_length(self.__res, val)

    def get_content_length(self):
        return tube_http_response_get_content_length(self.__res)

    def disable_prepare_buffer(self):
        tube_http_response_disable_prepare_buffer(self.__res)

    def write(self, bytes string):
        cdef char* content = string
        cdef size_t nwrite = len(string)
        with nogil:
            res = tube_http_response_write_data(self.__res, content, nwrite)
        return res

    def flush(self):
        with nogil:
            tube_http_response_flush_data(self.__res)

    def close(self):
        with nogil:
            tube_http_response_close(self.__res)

    def respond(self, int status_code, const_char_ptr reason):
        with nogil:
            tube_http_response_respond(self.__res, status_code, reason)

class HttpHandler:
    def __handle_request(self, request, response):
        self.handle_request(Request(request), Response(response))
