#include "pch.h"

#include <sstream>
#include <dirent.h>
#include <sys/types.h>
#include <cstdlib>
#include <locale.h>
#include <langinfo.h>

#include "http/static_handler.h"
#include "utils/logger.h"

namespace tube {

// remove all . and .. directory access
static std::string
string_remove_dots(const std::string& path)
{
    std::string res;
    for (size_t i = 0; i < path.length(); i++) {
        res += path[i];
        if (path[i] == '/') {
            size_t j = path.find('/', i + 1);
            if (j == std::string::npos) {
                j = path.length();
            }
            std::string ent = path.substr(i + 1, j - i - 1);
            if (ent != "." && ent != "..") {
                res.append(ent);
            }
            i = j - 1;
        }
    }
    return res;
}

std::string
StaticHttpHandler::remove_path_dots(const std::string& path)
{
    return string_remove_dots(path);
}

StaticHttpHandler::StaticHttpHandler()
{
    setlocale(LC_CTYPE, "");
    charset_ = nl_langinfo(CODESET);
    // setting up the default configuration parameters
    add_option("doc_root", "/var/www");
    add_option("error_root", "");
    add_option("allow_index", "true");
    add_option("index_page_css", "");
    add_option("max_cache_entry", "0");
    add_option("max_entry_size", "4096");
}

void
StaticHttpHandler::load_param()
{
    doc_root_ = option("doc_root");
    error_root_ = option("error_root");
    allow_index_ = utils::parse_bool(option("allow_index"));
    index_page_css_ = option("index_page_css");

    io_cache_.set_max_cache_entry(atoi(option("max_cache_entry").c_str()));
    io_cache_.set_max_entry_size(atoi(option("max_entry_size").c_str()));
}

// currently we only support single range
static void
parse_range(const std::string& range_desc, off64_t& offset, off64_t& length)
{
    offset = 0;
    length = -1;
    int end_byte = 0;

    size_t i = 0;
    while (i < range_desc.length()) {
        i++;
        if (range_desc[i - 1] == '=') {
            break;
        }
    }

    while (i < range_desc.length()) {
        char ch = range_desc[i];
        if (ch >= '0' && ch <= '9') {
            offset = 10 * offset + (ch - '0');
        } else if (ch == '-') {
            break;
        } else {
            return;
        }
        i++;
    }

    while (i < range_desc.length()) {
        char ch = range_desc[i];
        if (ch >= '0' && ch <= '9') {
            end_byte = 10 * end_byte + (ch - '0');
            length = end_byte - offset + 1;
        } else {
            return;
        }
        i++;
    }
}

static std::string
build_range_response(off64_t offset, off64_t length, off64_t file_size)
{
    std::stringstream ss;
    ss << "bytes " << offset << '-' << offset + length - 1 << '/' << file_size;
    return ss.str();
}

#define MAX_TIME_LEN 128

static std::string
build_last_modified(const time_t* last_modified_time)
{
    struct tm gmt;
    gmtime_r(last_modified_time, &gmt); // thread safe
    char time_str[MAX_TIME_LEN];
    strftime(time_str, MAX_TIME_LEN, "%a, %d %b %Y %T GMT", &gmt);
    return std::string(time_str);
}

typedef std::vector<std::string> Tokens;

static Tokens
tokenize_string(const std::string& str, const char* delim)
{
    std::vector<std::string> vec;
    std::string last;
    for (size_t i = 0; i < str.length(); i++) {
        if (strchr(delim, str[i]) != NULL) {
            if (last != "") {
                vec.push_back(last);
                last = "";
            }
        } else {
            last += str[i];
        }
    }
    if (last != "") {
        vec.push_back(last);
    }
    return vec;
}

static Tokens
tokenize_date(const std::string& date)
{
    return tokenize_string(date, ", -");
}

static Tokens
tokenize_time(const std::string& time)
{
    return tokenize_string(time, ":");
}

static bool
parse_digits(const std::string& number, const size_t max_ndigits, int& res)
{
    if (number.length() > max_ndigits) return false;
    res = 0;
    for (size_t i = 0; i < number.length(); i++) {
        if (number[i] < '0' || number[i] > '9')
            return false;
        res = res * 10 + number[i] - '0';
    }
    return true;
}

static bool
parse_month(const std::string& month, int& res)
{
    static const char* months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
        "Nov", "Dec"
    };
    if (month.length() != 3)
        return false;
    for (res = 0; res < 12; res++) {
        if (month == months[res]) {
            return true;
        }
    }
    return false;
}

static bool
parse_datetime(const std::string& datetime, struct tm* tm_struct)
{
    Tokens toks = tokenize_date(datetime);

    if (toks.size() !=6 || toks[5] != "GMT") {
        return false;
    }
    // we'll ignore the first token. since mktime will add for us
    if (!parse_digits(toks[1], 2, tm_struct->tm_mday))
        return false;
    if (!parse_month(toks[2], tm_struct->tm_mon))
        return false;
    if (!parse_digits(toks[3], 4, tm_struct->tm_year))
        return false;
    if (tm_struct->tm_year > 100) {
        tm_struct->tm_year -= 1900;
    }
    Tokens time_toks = tokenize_time(toks[4]);
    if (time_toks.size() != 3) {
        return false;
    }
    if (!parse_digits(time_toks[0], 2, tm_struct->tm_hour)) {
        return false;
    }
    if (!parse_digits(time_toks[1], 2, tm_struct->tm_min)) {
        return false;
    }
    if (!parse_digits(time_toks[2], 2, tm_struct->tm_sec)) {
        return false;
    }
    return true;
}

bool
StaticHttpHandler::validate_client_cache(const std::string& path,
                                         struct stat64 stat,
                                         HttpRequest& request)
{
    std::string modified_since =
        request.find_header_value("If-Modified-Since");
    struct tm tm_struct;
    if (parse_datetime(modified_since, &tm_struct)) {
        struct tm gmt;
        gmtime_r(&stat.st_mtime, &gmt);
        time_t orig_mtime = mktime(&gmt);
        time_t modified_time = mktime(&tm_struct);
        if (orig_mtime > modified_time) {
            return false;
        }
        return true;
    }
    return false;
}

int
StaticHttpHandler::try_open_file(const std::string& path, HttpRequest& request,
                                 HttpResponse& response)
{
    int file_desc = 0;
    if (request.method() != HTTP_HEAD) {
        file_desc = ::open(path.c_str(), O_RDONLY);
        // cannot open, this is access forbidden
        if (file_desc < 0) {
            respond_error(HttpResponseStatus::kHttpResponseForbidden,
                          request, response);
        }
    }
    return file_desc;
}

void
StaticHttpHandler::respond_file_content(const std::string& path,
                                        struct stat64 stat,
                                        HttpRequest& request,
                                        HttpResponse& response)
{
    byte* cached_entry = NULL;
    off64_t file_size = -1;
    std::string range_str;
    HttpResponseStatus ret_status = HttpResponseStatus::kHttpResponseOK;
    off64_t offset = 0, length = -1;

    if (request.method() != HTTP_HEAD) {
        cached_entry = io_cache_.access_cache(path, stat.st_mtime,
                                              stat.st_size);
    }

    int file_desc = try_open_file(path, request, response);
    if (file_desc < 0) {
        goto done;
    }

    file_size = stat.st_size;

    if (validate_client_cache(path, stat, request)) {
        response.respond(HttpResponseStatus::kHttpResponseNotModified);
        goto done;
    }

    range_str = request.find_header_value("Range");
    if (range_str != "") {
        // parse range
        parse_range(range_str, offset, length);
        // incomplete range, doesn't have a length
        // we'll transfer all begining from that offset
        if (length < 0) {
            length = file_size - offset;
        }
        // exceeded file size, this is invalid range
        if ((size_t) (offset + length) > file_size) {
            respond_error(
                HttpResponseStatus::kHttpResponseRequestedRangeNotSatisfiable,
                request, response);
            goto done;
        }
        response.add_header("Content-Range",
                            build_range_response(offset, length, file_size));
        ret_status = HttpResponseStatus::kHttpResponsePartialContent;
    } else {
        offset = 0;
        length = file_size;
    }

    response.disable_prepare_buffer();
    response.add_header("Last-Modified", build_last_modified(&stat.st_mtime));
    response.set_content_length(length);
    response.respond(ret_status);
    if (request.method() != HTTP_HEAD) {
        if (cached_entry) {
            response.write_data(cached_entry + offset, length);
        } else {
            response.write_file(file_desc, offset, length);
        }
    }
done:
    if (cached_entry) {
        ::close(file_desc);
    }
    delete [] cached_entry;
}

class HttpResponseStream
{
    HttpResponse& response_;
    bool          should_write_;
public:
    HttpResponseStream(HttpRequest& request, HttpResponse& response)
        : response_(response), should_write_(true) {
        if (request.method() == HTTP_HEAD) {
            should_write_ = false;
        }
    }

    template <typename T>
    HttpResponseStream& operator<<(const T& obj) {
        std::stringstream ss; ss << obj;
        return (*this) << ss.str();
    }

    HttpResponseStream& operator<<(const std::string& str) {
        do_write(str.c_str(), str.length());
        return *this;
    }

    HttpResponseStream& operator<<(const char* str) {
        do_write(str, strlen(str));
        return *this;
    }

    void do_write(const char* str, size_t len) {
        if (should_write_) {
            response_.write_string(str);
        } else {
            response_.set_content_length(response_.content_length() + len);
        }
    }
};

static void
add_directory_entry(HttpResponseStream& ss, std::string entry,
                    struct stat64* stat)
{
    if (stat == NULL) {
        ss << "<tr class=\"parent\"><td><a href=\"..\">Parent Directory</a>"
           << "</td>";
    } else if (S_ISDIR(stat->st_mode)) {
        ss << "<tr class=\"directory\">"
           << "<td><a href=\"" << entry << "/\">" << entry << "/</a></td>"
           << "<td>-</td>";
    } else if (S_ISREG(stat->st_mode)) {
        ss << "<tr class=\"regular\">"
           << "<td><a href=\"" << entry << "\">" << entry << "</a></td>"
           << "<td>" << stat->st_size << "</td>";
    }
    if (stat != NULL) {
        char time_str[MAX_TIME_LEN];
        struct tm localtime;
        localtime_r(&(stat->st_mtime), &localtime);
        strftime(time_str, MAX_TIME_LEN, "%F %T", &localtime);
        ss << "<td>" << time_str << "</td>";
    }
    ss << "</tr>" << HttpResponse::kHtmlNewLine;
}

static void
list_directory_html(HttpResponseStream& ss, const std::string& path, DIR* dirp)
{
    dirent* ent = NULL;
    while ((ent = readdir(dirp))) {
        std::string ent_name = ent->d_name;
        std::string ent_path = path + "/" + ent->d_name;
        struct stat64 buf;
        if (ent_name == "." || ent_name == "..") {
            continue;
        }
        if (::stat64(ent_path.c_str(), &buf) < 0) {
            continue;
        }
        add_directory_entry(ss, ent_name, &buf);
    }
}

void
StaticHttpHandler::respond_directory_list(const std::string& path,
                                          const std::string& href_path,
                                          HttpRequest& request,
                                          HttpResponse& response)
{
    DIR* dirp = opendir(path.c_str());
    if (!dirp) {
        respond_error(HttpResponseStatus::kHttpResponseForbidden,
                      request, response);
        return;
    }
    HttpResponseStream ss(request, response);

    ss << "<html><head>"
       << "<meta http-equiv=\"Content-Type\"  content=\"text/html; charset="
       << charset_ << "\">"
       <<"<title>Directory List " << href_path << "</title>"
       << HttpResponse::kHtmlNewLine;

    if (index_page_css_ != "") {
        ss << "<link rel=\"stylesheet\" type=\"text/css\" href=\""
           << index_page_css_ << "\"/>" << HttpResponse::kHtmlNewLine;
    }
    ss << "</head><body>" << HttpResponse::kHtmlNewLine
       << "<h1>Index of " << href_path << "</h1>" << HttpResponse::kHtmlNewLine
       << "<table>" << HttpResponse::kHtmlNewLine;

    if (href_path != "/") add_directory_entry(ss, "..", NULL);

    ss << "<table>";
    list_directory_html(ss, path, dirp);
    ss << "</table></body></html>" << HttpResponse::kHtmlNewLine;
    closedir(dirp);

    response.add_header("Content-Type", "text/html");
    response.respond(HttpResponseStatus::kHttpResponseOK);
}

void
StaticHttpHandler::respond_error(const HttpResponseStatus& error,
                                 HttpRequest& request,
                                 HttpResponse& response)
{
    std::stringstream path;
    int file_desc = -1;
    struct stat64 buf;

    if (error_root_ == "") {
        goto default_resp;
    }
    path << error_root_ << "/" << error.status_code << ".html";
    file_desc = ::open(path.str().c_str(), O_RDONLY);
    if (file_desc < 0) {
        goto default_resp;
    }
    if (::fstat64(file_desc, &buf) < 0) {
        ::close(file_desc);
        goto default_resp;
    }
    response.disable_prepare_buffer();
    response.add_header("Content-Type", "text/html");
    response.set_content_length(buf.st_size);
    response.respond(error);
    response.write_file(file_desc, 0, -1);
    return;
default_resp:
    response.respond_with_message(error);
}

void
StaticHttpHandler::handle_request(HttpRequest& request, HttpResponse& response)
{
    std::string filename = HttpRequest::url_decode(request.path());
    filename = remove_path_dots(filename);

    if (request.method() != HTTP_GET && request.method() != HTTP_POST
        && request.method() != HTTP_HEAD) {
        respond_error(HttpResponseStatus::kHttpResponseBadRequest, request,
                      response);
    }

    struct stat64 buf;
    std::string filepath = doc_root_ + filename;
    if (::stat64(filepath.c_str(), &buf) < 0) {
        LOG(DEBUG, "Cannot stat file %s", filepath.c_str());
        respond_error(HttpResponseStatus::kHttpResponseNotFound,
                      request, response);
        return;
    }

    if (S_ISREG(buf.st_mode)) {
        respond_file_content(filepath, buf, request, response);
    } else if (S_ISDIR(buf.st_mode)) {
        if (allow_index_) {
            respond_directory_list(filepath, filename, request, response);
        } else {
            respond_error(HttpResponseStatus::kHttpResponseForbidden,
                          request, response);
        }
    } else {
        respond_error(HttpResponseStatus::kHttpResponseForbidden,
                      request, response);
    }
}

}

extern "C" void
tube_http_static_module_init(void)
{
    static tube::StaticHttpHandlerFactory static_handler_factory;
    tube::BaseHttpHandlerFactory::register_factory(&static_handler_factory);
}
