#include "driver/http_request.h"

#include <cstddef>
#include <string>

namespace prts::driver {

HttpRequest parseHttpRequestHead(const std::string& raw) {
    HttpRequest request;
    const std::size_t lineEnd = raw.find("\r\n");
    const std::string line = raw.substr(0, lineEnd == std::string::npos ? raw.size() : lineEnd);
    if (line.empty()) {
        request.error = "empty request";
        return request;
    }

    const std::size_t firstSpace = line.find(' ');
    if (firstSpace == std::string::npos) {
        request.error = "malformed request line";
        return request;
    }
    const std::size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string::npos) {
        request.error = "malformed request line";
        return request;
    }

    request.method = line.substr(0, firstSpace);
    request.path = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    request.version = line.substr(secondSpace + 1);

    if (request.method != "GET" && request.method != "HEAD") {
        request.error = "method not allowed";
        return request;
    }
    if (request.path.empty() || request.path.front() != '/') {
        request.error = "bad path";
        return request;
    }
    request.ok = true;
    return request;
}

}  // namespace prts::driver
