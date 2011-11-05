#include <cstdlib>
#include <cstring>
#include "fcgi_proto.h"

%%{
    machine fcgi_content;

    action mark_name {
        name_mark_ = p;
    }
    action done_name {
        push_name(name_mark_, p - name_mark_);
        name_mark_ = NULL;
    }
    action mark_value {
        value_mark_ = p;
    }
    action done_value {
        push_value(value_mark_, p - value_mark_);
        value_mark_ = NULL;
    }
    action end_header {
        push_header();
    }
    action done_headers {
        done_parse();
    }

    newline = ("\r\n" | "\n");
    CTL = (cntrl | 127);
    tspecials = ("(" | ")" | "<" | ">" | "@" | "," | ";" | ":" | "\\" | "\"" 
                | "/" | "[" | "]" | "?" | "=" | "{" | "}" | " " | "\t");

    token = (ascii -- (CTL | tspecials));
    name = (token -- ":")+;
    value = token+;

    Name = name >mark_name %done_name;
    Value = value >mark_value %done_value;

    header = Name ": " Value newline;
    Header = header >end_header;
    headers = (Header*) newline;
    Headers = headers %done_headers;
    main := Headers;
}%%

namespace tube {
namespace fcgi {

%% write data;

void
FcgiContentParser::init_parser()
{
    name_mark_ = value_mark_ = NULL;
    int cs = 0;
    %% write init;
    state_ = cs;
}

int
FcgiContentParser::parse(const char* buf, size_t len)
{
    const char* p = buf;
    const char* pe = buf + len;
    const char* eof = pe;
    int cs = state_;
    if (name_mark_) name_mark_ = buf;
    if (value_mark_) value_mark_ = buf;
    %% write exec;
    if (name_mark_) push_name(name_mark_, p - name_mark_);
    if (value_mark_) push_value(value_mark_, p - value_mark_);
    state_ = cs;
    return p - buf;
}

bool
FcgiContentParser::has_error() const
{
    return state_ == fcgi_content_error; // return if in error state
}

}
}
