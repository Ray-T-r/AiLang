/* -------- TLS (OpenSSL) --------
 *
 * Thin layer over libssl. The handshake (SSL_accept / SSL_connect) is
 * done synchronously on the underlying TCP fd; once it succeeds the
 * caller uses `tls_send` / `tls_recv` instead of `sock_send` / `sock_recv`.
 *
 * Handles (`SSL_CTX*`, `SSL*`) are exposed to AiLang as `i64`. The
 * library is auto-initialised on first ctx creation. */
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>

static int ailang_tls_init_done_ = 0;
static void ailang_tls_init_(void) {
    if (ailang_tls_init_done_) return;
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    ailang_tls_init_done_ = 1;
}

static int64_t tls_server_ctx(const char* cert, const char* key) {
    ailang_tls_init_();
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) return -1;
    if (!cert || SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(ctx);
        return -1;
    }
    if (!key || SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0) {
        SSL_CTX_free(ctx);
        return -1;
    }
    return (int64_t) (intptr_t) ctx;
}

static int64_t tls_client_ctx(void) {
    ailang_tls_init_();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return -1;
    return (int64_t) (intptr_t) ctx;
}

static void tls_free_ctx(int64_t ctx) {
    if (ctx > 0) SSL_CTX_free((SSL_CTX*) (intptr_t) ctx);
}

/* Wrap an accepted TCP fd in TLS and complete the handshake. */
static int64_t tls_accept(int64_t ctx, int64_t fd) {
    if (ctx <= 0 || fd < 0) return -1;
    SSL* ssl = SSL_new((SSL_CTX*) (intptr_t) ctx);
    if (!ssl) return -1;
    SSL_set_fd(ssl, (int) fd);
    if (SSL_accept(ssl) <= 0) {
        SSL_free(ssl);
        return -1;
    }
    return (int64_t) (intptr_t) ssl;
}

/* Wrap a connected TCP fd in TLS and complete the client handshake. */
static int64_t tls_connect_fd(int64_t ctx, int64_t fd) {
    if (ctx <= 0 || fd < 0) return -1;
    SSL* ssl = SSL_new((SSL_CTX*) (intptr_t) ctx);
    if (!ssl) return -1;
    SSL_set_fd(ssl, (int) fd);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        return -1;
    }
    return (int64_t) (intptr_t) ssl;
}

static int64_t tls_send(int64_t ssl, ailang_bytes b) {
    if (ssl <= 0) return -1;
    if (b.len == 0) return 0;
    int n = SSL_write((SSL*) (intptr_t) ssl, b.data, (int) b.len);
    return (int64_t) n;
}

static int64_t tls_send_str(int64_t ssl, const char* s) {
    if (ssl <= 0 || !s) return -1;
    size_t len = strlen(s);
    if (len == 0) return 0;
    int n = SSL_write((SSL*) (intptr_t) ssl, s, (int) len);
    return (int64_t) n;
}

static ailang_bytes tls_recv(int64_t ssl, int64_t max) {
    ailang_bytes r;
    r.len = 0;
    r.data = (const uint8_t*) "";
    if (ssl <= 0 || max <= 0) return r;
    uint8_t* buf = (uint8_t*) GC_malloc_atomic((size_t) max);
    int n = SSL_read((SSL*) (intptr_t) ssl, buf, (int) max);
    if (n <= 0) return r;
    r.len = (int64_t) n;
    r.data = buf;
    return r;
}

static void tls_close(int64_t ssl) {
    if (ssl > 0) {
        SSL_shutdown((SSL*) (intptr_t) ssl);
        SSL_free((SSL*) (intptr_t) ssl);
    }
}

/* Last queued OpenSSL error (peek; caller can call again to drain). */
static const char* tls_error(void) {
    unsigned long e = ERR_peek_error();
    if (e == 0) return "";
    char* buf = (char*) GC_malloc_atomic(256);
    ERR_error_string_n(e, buf, 256);
    return buf;
}

/* sha1(s) — 20-byte SHA-1 digest of `s` (str, len = strlen). */
static ailang_bytes sha1(const char* s) {
    ailang_bytes r;
    uint8_t* buf = (uint8_t*) GC_malloc_atomic(20);
    SHA1((const unsigned char*) (s ? s : ""), s ? strlen(s) : 0, buf);
    r.len = 20;
    r.data = buf;
    return r;
}

