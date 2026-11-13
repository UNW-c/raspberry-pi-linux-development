#include <string>

#include "driver/http_request.h"
#include "test/test_framework.h"

PRTS_TEST(http_request_parses_get) {
    const auto request =
        prts::driver::parseHttpRequestHead("GET /stream HTTP/1.1\r\nHost: x\r\n\r\n");
    PRTS_CHECK(request.ok);
    PRTS_CHECK_EQ(request.method, std::string("GET"));
    PRTS_CHECK_EQ(request.path, std::string("/stream"));
    PRTS_CHECK_EQ(request.version, std::string("HTTP/1.1"));
}

PRTS_TEST(http_request_parses_head_and_root) {
    const auto request = prts::driver::parseHttpRequestHead("HEAD / HTTP/1.1\r\n\r\n");
    PRTS_CHECK(request.ok);
    PRTS_CHECK_EQ(request.method, std::string("HEAD"));
    PRTS_CHECK_EQ(request.path, std::string("/"));
}

PRTS_TEST(http_request_rejects_bad_lines) {
    PRTS_CHECK(!prts::driver::parseHttpRequestHead("").ok);
    PRTS_CHECK(!prts::driver::parseHttpRequestHead("GET\r\n").ok);
    PRTS_CHECK(!prts::driver::parseHttpRequestHead("GET /path\r\n").ok);
    PRTS_CHECK(!prts::driver::parseHttpRequestHead("POST /x HTTP/1.1\r\n").ok);
    PRTS_CHECK(!prts::driver::parseHttpRequestHead("GET x HTTP/1.1\r\n").ok);
}

PRTS_TEST(http_request_keeps_query_string) {
    const auto request = prts::driver::parseHttpRequestHead("GET /status?t=1 HTTP/1.1\r\n\r\n");
    PRTS_CHECK(request.ok);
    PRTS_CHECK_EQ(request.path, std::string("/status?t=1"));
}
