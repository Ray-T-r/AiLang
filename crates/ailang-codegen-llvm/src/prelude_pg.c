/* -------- Postgres client (libpq) --------
 *
 * libpq returns opaque `PGconn*` / `PGresult*` pointers. AiLang has no
 * pointer type, so we cast through `intptr_t` and expose handles as
 * `i64`. The user MUST call `pg_close(conn)` and `pg_clear(res)` to free.
 *
 * Strings returned from libpq (e.g. `PQgetvalue`) are owned by the
 * `PGresult` and stay valid until `pg_clear`. We hand them out without
 * copying — copy yourself if you need to keep them past `pg_clear`. */
#include <libpq-fe.h>

static int64_t pg_connect(const char* conninfo) {
    PGconn* c = PQconnectdb(conninfo ? conninfo : "");
    /* Even a failed connection returns a non-null PGconn we can query
     * for the error message; the caller checks pg_status / pg_error. */
    return (int64_t) (intptr_t) c;
}

/* 0 = CONNECTION_OK, anything else = bad. */
static int64_t pg_status(int64_t conn) {
    if (conn == 0) return -1;
    return (int64_t) PQstatus((PGconn*) (intptr_t) conn);
}

static const char* pg_error(int64_t conn) {
    if (conn == 0) return "(null connection)";
    const char* msg = PQerrorMessage((PGconn*) (intptr_t) conn);
    return msg ? msg : "";
}

static void pg_close(int64_t conn) {
    if (conn != 0) PQfinish((PGconn*) (intptr_t) conn);
}

/* Execute a SQL statement. Returns a result handle (0 on null). The user
 * checks `pg_ok(res)` before reading rows / `pg_result_error(res)` for
 * the error message, then calls `pg_clear(res)`. */
static int64_t pg_exec(int64_t conn, const char* sql) {
    if (conn == 0 || !sql) return 0;
    PGresult* r = PQexec((PGconn*) (intptr_t) conn, sql);
    return (int64_t) (intptr_t) r;
}

static bool pg_ok(int64_t res) {
    if (res == 0) return false;
    int s = PQresultStatus((PGresult*) (intptr_t) res);
    return s == PGRES_COMMAND_OK || s == PGRES_TUPLES_OK;
}

static const char* pg_result_error(int64_t res) {
    if (res == 0) return "(null result)";
    const char* m = PQresultErrorMessage((PGresult*) (intptr_t) res);
    return m ? m : "";
}

static void pg_clear(int64_t res) {
    if (res != 0) PQclear((PGresult*) (intptr_t) res);
}

static int64_t pg_nrows(int64_t res) {
    if (res == 0) return 0;
    return (int64_t) PQntuples((PGresult*) (intptr_t) res);
}

static int64_t pg_ncols(int64_t res) {
    if (res == 0) return 0;
    return (int64_t) PQnfields((PGresult*) (intptr_t) res);
}

/* Copy the cell to GC heap so it outlives `pg_clear(res)`. Without this,
 * the common pattern `v := pg_value(r, 0, 0); pg_clear(r); use(v)` is a
 * use-after-free — libpq frees the value buffer in PQclear. */
static const char* pg_value(int64_t res, int64_t row, int64_t col) {
    if (res == 0) return "";
    const char* v = PQgetvalue((PGresult*) (intptr_t) res, (int) row, (int) col);
    if (!v) return "";
    size_t n = strlen(v);
    char* out = (char*) GC_malloc_atomic(n + 1);
    memcpy(out, v, n + 1);
    return out;
}

static bool pg_isnull(int64_t res, int64_t row, int64_t col) {
    if (res == 0) return false;
    return PQgetisnull((PGresult*) (intptr_t) res, (int) row, (int) col) != 0;
}

static const char* pg_col_name(int64_t res, int64_t col) {
    if (res == 0) return "";
    const char* n = PQfname((PGresult*) (intptr_t) res, (int) col);
    if (!n) return "";
    /* Same lifetime concern as pg_value — copy to GC heap. */
    size_t l = strlen(n);
    char* out = (char*) GC_malloc_atomic(l + 1);
    memcpy(out, n, l + 1);
    return out;
}

static int64_t pg_affected(int64_t res) {
    if (res == 0) return 0;
    const char* s = PQcmdTuples((PGresult*) (intptr_t) res);
    if (!s || !*s) return 0;
    return (int64_t) atoll(s);
}

/* SQL-safe literal quoting. Always use this for user input — concatenating
 * raw strings into SQL is a vulnerability. Returns "''" on failure. */
static const char* pg_escape(int64_t conn, const char* s) {
    if (conn == 0 || !s) return "''";
    char* esc = PQescapeLiteral((PGconn*) (intptr_t) conn, s, strlen(s));
    if (!esc) return "''";
    /* PQescapeLiteral returns malloc'd memory; copy to GC heap. */
    size_t n = strlen(esc);
    char* out = (char*) GC_malloc_atomic(n + 1);
    memcpy(out, esc, n + 1);
    PQfreemem(esc);
    return out;
}

