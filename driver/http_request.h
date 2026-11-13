#pragma once

#include <string>

namespace prts::driver {

// Minimal HTTP request head, enough for a read-only viewer.
struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    bool ok{false};
    std::string error;
};

// Parses "GET /path HTTP/1.1\r\nHeader: ...\r\n\r\n". Only the request line is
// interpreted; headers are ignored.
HttpRequest parseHttpRequestHead(const std::string& raw);

}  // namespace prts::driver
