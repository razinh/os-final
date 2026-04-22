#pragma once

#include <stdint.h>
#include "lib/kstd.h"

namespace net {

class HttpRequest {
public:
    enum class Method {
        GET,
        POST,
        PUT,
        DELETE,
        PATCH,
        HEAD
    };

    HttpRequest(Method method, const char* host, const char* path);

    // add a custom header to the request
    void add_header(const char* key, const char* value);

    // build the final request string
    size_t build(char* buffer, size_t buffer_size) const;

private:
    Method method_;
    const char* host_;
    const char* path_;

    struct Header {
        const char* key;
        const char* value;
    };
    Header custom_headers_[8];
    size_t num_headers_;
};

class HttpResponse {
public:
    HttpResponse();

    // parse http response from a buffer
    bool parse(const char* data, size_t length);

    // accessors
    int status_code() const { return status_code_; };
    const char* status_message() const { return status_message_; }
    const char* body() const { return body_; }
    size_t body_length() const { return body_length_; }

    // get specific header value
    const char* get_header(const char* key) const;

    // debug output
    void print() const;

private:
    int status_code_;
    char status_message_[64];

    struct Header {
        char key[64];
        char value[256];
    };
    Header headers_[16];
    size_t num_headers_;

    const char* body_;
    size_t body_length_;

    bool parse_status_line(const char* line);
    bool parse_header_line(const char* line);
};

class HttpClient {
public:
    static int get(const char* host, uint16_t port, const char* path,
                   char* response_buffer, size_t buffer_size);

    static int post(const char* host, uint16_t port, const char* path,
                    const char* body, size_t body_length,
                    char* response_buffer, size_t buffer_size);

    static int put(const char* host, uint16_t port, const char* path,
                   const char* body, size_t body_length,
                   char* response_buffer, size_t buffer_size);

    static int delete_request(const char* host, uint16_t port, const char* path,
                              char* response_buffer, size_t buffer_size);

    static int patch(const char* host, uint16_t port, const char* path,
                     const char* body, size_t body_length,
                     char* response_buffer, size_t buffer_size);

private:
    static int do_request(HttpRequest::Method method, const char* host,
                          uint16_t port, const char* path,
                          const char* request_body, size_t request_body_length,
                          char* response_buffer, size_t buffer_size);
};

} // namespace net