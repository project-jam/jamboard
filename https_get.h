#pragma once
// ============================================================
// Minimal HTTPS client
//
// Some providers (Google in particular) reject clients that use the
// OpenSSL default cipher configuration with HTTP 429. Sending the
// same cipher list, TLS 1.3 suites and curve list that curl uses
// makes the requests go through, so that is what this client sends.
//
// It also resolves the host with every returned address and falls
// back from IPv6 to IPv4, which matters on machines without IPv6
// connectivity.
// ============================================================

#include <string>
#include <vector>
#include <utility>

#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define JAMBOARD_CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#define JAMBOARD_CLOSE_SOCKET close
#endif

struct HttpResponse {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;
};

namespace https_detail {

// The cipher configuration curl uses. Google blocks the OpenSSL default.
static const char* CURL_CIPHERS =
    "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
    "ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:"
    "ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:"
    "ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:"
    "AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:"
    "AES128-SHA:AES256-SHA";

static const char* CURL_TLS13_CIPHERS =
    "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256";

static const char* CURL_GROUPS = "X25519:secp256r1:secp384r1:secp521r1";

} // namespace https_detail

// Performs an HTTP/1.1 GET request over TLS.
inline HttpResponse https_get(const std::string& host, const std::string& path,
                              const std::vector<std::pair<std::string, std::string>>& headers = {}) {
    HttpResponse out;

    struct addrinfo hints = {}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0) {
        out.error = "could not resolve host";
        return out;
    }

    // Try every address, IPv6 first. Hosts that advertise an AAAA record
    // on a machine without IPv6 connectivity need the IPv4 fallback.
    int sock = -1;
    for (struct addrinfo* p = res; p != nullptr; p = p->ai_next) {
        int s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s < 0) continue;
        if (connect(s, p->ai_addr, p->ai_addrlen) == 0) { sock = s; break; }
        JAMBOARD_CLOSE_SOCKET(s);
    }
    freeaddrinfo(res);
    if (sock < 0) {
        out.error = "could not connect";
        return out;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        JAMBOARD_CLOSE_SOCKET(sock);
        out.error = "ssl context failed";
        return out;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_cipher_list(ctx, https_detail::CURL_CIPHERS);
    SSL_CTX_set_ciphersuites(ctx, https_detail::CURL_TLS13_CIPHERS);
    SSL_CTX_set1_groups_list(ctx, https_detail::CURL_GROUPS);

    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        JAMBOARD_CLOSE_SOCKET(sock);
        out.error = "ssl failed";
        return out;
    }
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host.c_str());
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        JAMBOARD_CLOSE_SOCKET(sock);
        out.error = "tls handshake failed";
        return out;
    }

    std::string req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
           " (KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36\r\n";
    req += "Accept: */*\r\n";
    req += "Accept-Language: en-US,en;q=0.9\r\n";
    for (const auto& h : headers) req += h.first + ": " + h.second + "\r\n";
    req += "Connection: close\r\n\r\n";

    if (SSL_write(ssl, req.data(), (int)req.size()) <= 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        JAMBOARD_CLOSE_SOCKET(sock);
        out.error = "send failed";
        return out;
    }

    std::string response;
    char buf[8192];
    int n;
    while ((n = SSL_read(ssl, buf, (int)sizeof(buf))) > 0) response.append(buf, n);

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    JAMBOARD_CLOSE_SOCKET(sock);

    // Split the head from the body
    size_t head_end = response.find("\r\n\r\n");
    if (head_end == std::string::npos) {
        out.error = "malformed response";
        return out;
    }
    std::string head = response.substr(0, head_end);
    std::string body = response.substr(head_end + 4);

    // Status line
    size_t sp = head.find(' ');
    if (sp == std::string::npos) {
        out.error = "malformed status line";
        return out;
    }
    out.status = atoi(head.c_str() + sp + 1);

    // Chunked transfer encoding
    std::string lower_head = head;
    for (auto& c : lower_head) c = (char)tolower((unsigned char)c);
    if (lower_head.find("transfer-encoding: chunked") != std::string::npos) {
        std::string decoded;
        size_t pos = 0;
        while (pos < body.size()) {
            size_t eol = body.find("\r\n", pos);
            if (eol == std::string::npos) break;
            size_t len = strtoul(body.substr(pos, eol - pos).c_str(), nullptr, 16);
            if (len == 0) break;
            decoded += body.substr(eol + 2, len);
            pos = eol + 2 + len + 2;
        }
        body = decoded;
    }

    out.body = body;
    out.ok = (out.status >= 200 && out.status < 300);
    return out;
}
