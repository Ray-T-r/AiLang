#ifndef _WIN32
#define GC_THREADS
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gc.h>
#include <time.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif
#ifndef _WIN32
#include <regex.h>
#endif
#ifndef _WIN32
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#endif
#ifndef _WIN32
#include <libpq-fe.h>
#endif
#ifdef _WIN32
unsigned long GetModuleFileNameA(void*, char*, unsigned long);
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
static const char* scat(const char* a, const char* b){ size_t la=strlen(a), lb=strlen(b); char* r=(char*)GC_MALLOC(la+lb+1); memcpy(r,a,la); memcpy(r+la,b,lb); r[la+lb]=0; return r; }
static const char* i2s(long long v){ char* r=(char*)GC_MALLOC(24); snprintf(r,24,"%lld",v); return r; }
static const char* substr(const char* s, int64_t a, int64_t b){ int64_t n=(int64_t)strlen(s); if(a<0)a=0; if(b>n)b=n; if(b<a)b=a; int64_t L=b-a; char* r=(char*)GC_MALLOC(L+1); memcpy(r,s+a,L); r[L]=0; return r; }
static int64_t s2i(const char* s){ return (int64_t)strtoll(s,0,10); }
static const char* f2s(double v){ char* r=(char*)GC_MALLOC(32); snprintf(r,32,"%g",v); return r; }
static double s2f(const char* s){ return strtod(s,0); }
static const char* ail_u16to8(const unsigned char* p, long n, int be){ char* o=(char*)GC_MALLOC((size_t)(n*2+4)); size_t k=0; long i=0; while(i+1<n){ unsigned cu=be?((unsigned)p[i]<<8|p[i+1]):((unsigned)p[i]|(unsigned)p[i+1]<<8); i+=2; unsigned cp=cu; if(cu>=0xD800&&cu<=0xDBFF&&i+1<n){ unsigned lo=be?((unsigned)p[i]<<8|p[i+1]):((unsigned)p[i]|(unsigned)p[i+1]<<8); if(lo>=0xDC00&&lo<=0xDFFF){ cp=0x10000u+((cu-0xD800u)<<10)+(lo-0xDC00u); i+=2; } } if(cp<0x80){ o[k++]=(char)cp; } else if(cp<0x800){ o[k++]=(char)(0xC0|(cp>>6)); o[k++]=(char)(0x80|(cp&0x3F)); } else if(cp<0x10000){ o[k++]=(char)(0xE0|(cp>>12)); o[k++]=(char)(0x80|((cp>>6)&0x3F)); o[k++]=(char)(0x80|(cp&0x3F)); } else { o[k++]=(char)(0xF0|(cp>>18)); o[k++]=(char)(0x80|((cp>>12)&0x3F)); o[k++]=(char)(0x80|((cp>>6)&0x3F)); o[k++]=(char)(0x80|(cp&0x3F)); } } o[k]=0; return o; }
static const char* read_file_c(const char* path){ FILE* f=fopen(path,"rb"); if(!f) return ""; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); unsigned char* b=(unsigned char*)GC_MALLOC(n+1); if(n>0) fread(b,1,n,f); b[n]=0; fclose(f); if(n>=3&&b[0]==0xEF&&b[1]==0xBB&&b[2]==0xBF){ memmove(b,b+3,(size_t)(n-3)+1); return (const char*)b; } if(n>=2&&b[0]==0xFF&&b[1]==0xFE){ return ail_u16to8(b+2,n-2,0); } if(n>=2&&b[0]==0xFE&&b[1]==0xFF){ return ail_u16to8(b+2,n-2,1); } return (const char*)b; }
static int64_t write_file_c(const char* path, const char* content){ FILE* f=fopen(path,"wb"); if(!f) return 0; fwrite(content,1,strlen(content),f); fclose(f); return 1; }
typedef struct { int64_t len; const uint8_t* data; } ailang_bytes;
static ailang_bytes str_to_bytes(const char* s){ ailang_bytes r; if(!s){ r.len=0; r.data=(const uint8_t*)""; return r; } size_t n=strlen(s); r.len=(int64_t)n; r.data=(const uint8_t*)s; return r; }
static const char* bytes_to_str(ailang_bytes b){ char* buf=(char*)GC_MALLOC((size_t)b.len+1); if(b.len>0) memcpy(buf,b.data,(size_t)b.len); buf[b.len]=0; return buf; }
static int64_t bytes_at(ailang_bytes b, int64_t i){ if(i<0||i>=b.len) return 0; return (int64_t)b.data[i]; }
static ailang_bytes bytes_slice(ailang_bytes b, int64_t lo, int64_t hi){ if(lo<0) lo=0; if(hi>b.len) hi=b.len; if(lo>hi) lo=hi; int64_t n=hi-lo; ailang_bytes r; r.len=n; uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)(n>0?n:1)); if(n>0) memcpy(buf,b.data+lo,(size_t)n); r.data=buf; return r; }
static ailang_bytes read_file_bytes(const char* path){ ailang_bytes r; r.len=0; r.data=(const uint8_t*)""; FILE* f=fopen(path,"rb"); if(!f) return r; if(fseek(f,0,SEEK_END)!=0){ fclose(f); return r; } long sz=ftell(f); if(sz<0){ fclose(f); return r; } fseek(f,0,SEEK_SET); uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)(sz>0?sz:1)); size_t got=fread(buf,1,(size_t)sz,f); fclose(f); r.len=(int64_t)got; r.data=buf; return r; }
static int64_t write_file_bytes(const char* path, ailang_bytes b){ FILE* f=fopen(path,"wb"); if(!f) return 0; size_t want=(size_t)b.len; size_t got=b.len>0?fwrite(b.data,1,want,f):0; int closed=fclose(f); return (closed==0 && got==want)?1:0; }
static void print_bytes(ailang_bytes b){ putchar(98); putchar(34); for(int64_t i=0;i<b.len;i++){ unsigned c=b.data[i]; if(c==34){ putchar(92); putchar(34); } else if(c==92){ putchar(92); putchar(92); } else if(c==10){ putchar(92); putchar(110); } else if(c==13){ putchar(92); putchar(114); } else if(c==9){ putchar(92); putchar(116); } else if(c>=32 && c<127){ putchar((int)c); } else { putchar(92); putchar(120); printf("%02x", c); } } putchar(34); }
static int64_t str_contains(const char* h, const char* n){ if(!h||!n) return 0; return strstr(h,n)!=0; }
static int64_t starts_with(const char* s, const char* p){ if(!s||!p) return 0; size_t lp=strlen(p); return strncmp(s,p,lp)==0; }
static int64_t ends_with(const char* s, const char* su){ if(!s||!su) return 0; size_t ls=strlen(s),lu=strlen(su); if(lu>ls) return 0; return memcmp(s+ls-lu,su,lu)==0; }
static int64_t str_index_of(const char* h, const char* n){ if(!h||!n) return -1; const char* p=strstr(h,n); return p?(int64_t)(p-h):(int64_t)-1; }
static const char* to_upper(const char* s){ if(!s) return ""; size_t n=strlen(s); char* o=(char*)GC_MALLOC(n+1); for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)s[i]; o[i]=(c>=97&&c<=122)?(char)(c-32):(char)c; } o[n]=0; return o; }
static const char* to_lower(const char* s){ if(!s) return ""; size_t n=strlen(s); char* o=(char*)GC_MALLOC(n+1); for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)s[i]; o[i]=(c>=65&&c<=90)?(char)(c+32):(char)c; } o[n]=0; return o; }
static const char* trim(const char* s){ if(!s) return ""; const char* p=s; while(*p==32||*p==9||*p==10||*p==13) p++; const char* e=s+strlen(s); while(e>p){ char c=e[-1]; if(c!=32&&c!=9&&c!=10&&c!=13) break; e--; } size_t n=(size_t)(e-p); char* o=(char*)GC_MALLOC(n+1); memcpy(o,p,n); o[n]=0; return o; }
static const char* str_replace(const char* s, const char* a, const char* b){ if(!s||!a||!*a) return s?s:""; if(!b) b=""; size_t la=strlen(a),lb=strlen(b); size_t cnt=0; const char* p=s; while((p=strstr(p,a))!=0){ cnt++; p+=la; } size_t ls=strlen(s); size_t ol=ls+cnt*(lb>=la?lb-la:0)-cnt*(la>lb?la-lb:0); char* o=(char*)GC_MALLOC(ol+1); char* w=o; const char* r=s; while((p=strstr(r,a))!=0){ size_t bf=(size_t)(p-r); memcpy(w,r,bf); w+=bf; memcpy(w,b,lb); w+=lb; r=p+la; } size_t tl=strlen(r); memcpy(w,r,tl); w+=tl; *w=0; return o; }
static const char* repeat(const char* s, int64_t n){ if(!s||n<=0) return ""; size_t one=strlen(s); size_t tot=one*(size_t)n; char* o=(char*)GC_MALLOC(tot+1); for(int64_t i=0;i<n;i++) memcpy(o+(size_t)i*one,s,one); o[tot]=0; return o; }
static const char* pad_left(const char* s, int64_t w, const char* pad){ if(!s) s=""; if(!pad||!*pad) pad=" "; size_t sl=strlen(s); if((int64_t)sl>=w){ char* o=(char*)GC_MALLOC(sl+1); memcpy(o,s,sl+1); return o; } size_t pl=strlen(pad); size_t need=(size_t)w-sl; char* o=(char*)GC_MALLOC((size_t)w+1); size_t i=0; while(i<need){ o[i]=pad[i%pl]; i++; } memcpy(o+need,s,sl); o[w]=0; return o; }
static const char* pad_right(const char* s, int64_t w, const char* pad){ if(!s) s=""; if(!pad||!*pad) pad=" "; size_t sl=strlen(s); if((int64_t)sl>=w){ char* o=(char*)GC_MALLOC(sl+1); memcpy(o,s,sl+1); return o; } size_t pl=strlen(pad); char* o=(char*)GC_MALLOC((size_t)w+1); memcpy(o,s,sl); size_t i=sl; while(i<(size_t)w){ o[i]=pad[(i-sl)%pl]; i++; } o[w]=0; return o; }
static const char* chr(int64_t i){ char* o=(char*)GC_MALLOC(2); o[0]=(char)(i&255); o[1]=0; return o; }
static int64_t ord(const char* s){ return (s&&*s)?(int64_t)(unsigned char)s[0]:0; }
static int64_t str_to_bool(const char* s){ if(!s) return 0; if(strcmp(s,"true")==0) return 1; if(strcmp(s,"True")==0) return 1; if(strcmp(s,"TRUE")==0) return 1; if(strcmp(s,"1")==0) return 1; if(strcmp(s,"yes")==0) return 1; return 0; }
static int64_t abs_i64(int64_t n){ return n<0?-n:n; }
static double abs_f64(double x){ return x<0?-x:x; }
static int64_t sign(int64_t n){ return (n>0)-(n<0); }
static int64_t clamp(int64_t n, int64_t lo, int64_t hi){ if(n<lo) return lo; if(n>hi) return hi; return n; }
static const char* read_line(void){ size_t cap=128,len=0; char* b=(char*)GC_MALLOC(cap); int c; while((c=fgetc(stdin))!=EOF&&c!=10){ if(len+1>=cap){ size_t nc=cap*2; char* nb=(char*)GC_MALLOC(nc); memcpy(nb,b,len); b=nb; cap=nc; } b[len++]=(char)c; } b[len]=0; if(len==0&&c==EOF) return ""; return b; }
static const char* get_env(const char* name){ const char* v=name?getenv(name):0; return v?v:""; }
static const char* format(const char* fmt, ...){ char b[1024]; va_list ap; va_start(ap,fmt); int n=vsnprintf(b,sizeof b,fmt?fmt:"",ap); va_end(ap); if(n<0) n=0; if(n>=(int)sizeof b) n=(int)sizeof b-1; char* o=(char*)GC_MALLOC((size_t)n+1); memcpy(o,b,(size_t)n+1); return o; }
static const char* exe_dir(void){ char buf[4096]; buf[0]=0;
#ifdef _WIN32
 unsigned long wn=GetModuleFileNameA(0,buf,(unsigned long)sizeof buf); if(wn==0||wn>=sizeof buf) return "";
#elif defined(__APPLE__)
 unsigned int sz=(unsigned int)sizeof buf; if(_NSGetExecutablePath(buf,&sz)!=0) return "";
#else
 long rn=readlink("/proc/self/exe",buf,sizeof buf-1); if(rn<=0) return ""; buf[(size_t)rn]=0;
#endif
 int i=(int)strlen(buf)-1; while(i>=0 && buf[i]!='/' && buf[i]!=92) i--; if(i<0) return ""; char* d=(char*)GC_MALLOC((size_t)i+1); memcpy(d,buf,(size_t)i); d[i]=0; return d; }
static int64_t now_ms(void){ struct timespec ts; if(clock_gettime(CLOCK_REALTIME,&ts)!=0) return 0; return (int64_t)ts.tv_sec*1000+(int64_t)(ts.tv_nsec/1000000); }
static int64_t now_us(void){ struct timespec ts; if(clock_gettime(CLOCK_REALTIME,&ts)!=0) return 0; return (int64_t)ts.tv_sec*1000000+(int64_t)(ts.tv_nsec/1000); }
static int64_t mono_ms(void){ struct timespec ts; if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0) return 0; return (int64_t)ts.tv_sec*1000+(int64_t)(ts.tv_nsec/1000000); }
static void sleep_ms(int64_t ms){ if(ms<=0) return; struct timespec ts; ts.tv_sec=(time_t)(ms/1000); ts.tv_nsec=(long)((ms%1000)*1000000); while(nanosleep(&ts,&ts)==-1){} }
static const char* time_iso(int64_t ms){ time_t sec=(time_t)(ms/1000); struct tm tmv;
#ifdef _WIN32
 if(gmtime_s(&tmv,&sec)!=0) return "";
#else
 if(!gmtime_r(&sec,&tmv)) return "";
#endif
 char* buf=(char*)GC_MALLOC(32); size_t n=strftime(buf,32,"%Y-%m-%dT%H:%M:%SZ",&tmv); if(n==0){ buf[0]=0; } return buf; }
#ifndef _WIN32
static int64_t tcp_listen(const char* host, int64_t port){ if(!host||port<=0||port>65535) return -1; int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1; int yes=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)); struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)port); if(host[0]==0||strcmp(host,"0.0.0.0")==0){ addr.sin_addr.s_addr=htonl(INADDR_ANY); } else if(inet_pton(AF_INET,host,&addr.sin_addr)!=1){ close(fd); return -1; } if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0){ close(fd); return -1; } if(listen(fd,128)<0){ close(fd); return -1; } return (int64_t)fd; }
static int64_t tcp_accept(int64_t fd){ struct sockaddr_in addr; socklen_t len=sizeof(addr); int client=accept((int)fd,(struct sockaddr*)&addr,&len); if(client<0) return -1; return (int64_t)client; }
static int64_t tcp_connect(const char* host, int64_t port){ if(!host||port<=0||port>65535) return -1; int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1; struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)port); if(inet_pton(AF_INET,host,&addr.sin_addr)!=1){ struct addrinfo hints; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; struct addrinfo* res=0; if(getaddrinfo(host,0,&hints,&res)!=0||!res){ close(fd); return -1; } addr.sin_addr=((struct sockaddr_in*)res->ai_addr)->sin_addr; freeaddrinfo(res); } if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0){ close(fd); return -1; } return (int64_t)fd; }
static int64_t sock_send(int64_t fd, ailang_bytes b){ if(fd<0) return -1; if(b.len==0) return 0; ssize_t n=send((int)fd,b.data,(size_t)b.len,0); return (int64_t)n; }
static int64_t sock_send_str(int64_t fd, const char* s){ if(fd<0||!s) return -1; size_t len=strlen(s); if(len==0) return 0; ssize_t n=send((int)fd,s,len,0); return (int64_t)n; }
static ailang_bytes sock_recv(int64_t fd, int64_t max){ ailang_bytes r; r.len=0; r.data=(const uint8_t*)""; if(fd<0||max<=0) return r; uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)max); ssize_t n=recv((int)fd,buf,(size_t)max,0); if(n<=0) return r; r.len=(int64_t)n; r.data=buf; return r; }
static int64_t sock_close(int64_t fd){ if(fd<0) return 0; return (int64_t)close((int)fd); }
#endif
#ifndef _WIN32
static int64_t proc_fork(void){ pid_t p=fork(); return (int64_t)p; }
static int64_t proc_getpid(void){ return (int64_t)getpid(); }
static void proc_no_zombies(void){ struct sigaction sa; memset(&sa,0,sizeof(sa)); sa.sa_handler=SIG_IGN; sigemptyset(&sa.sa_mask); sa.sa_flags=SA_NOCLDWAIT; sigaction(SIGCHLD,&sa,0); }
static int64_t proc_reap(void){ int64_t n=0; while(waitpid(-1,0,WNOHANG)>0) n++; return n; }
#endif
#ifndef _WIN32
static int64_t regex_match(const char* pat, const char* text){ if(!pat||!text) return 0; regex_t re; if(regcomp(&re,pat,REG_EXTENDED|REG_NOSUB)!=0) return 0; int rc=regexec(&re,text,0,0,0); regfree(&re); return rc==0; }
static const char* regex_find(const char* pat, const char* text){ if(!pat||!text) return ""; regex_t re; if(regcomp(&re,pat,REG_EXTENDED)!=0) return ""; regmatch_t m; int rc=regexec(&re,text,1,&m,0); regfree(&re); if(rc!=0||m.rm_so<0) return ""; size_t len=(size_t)(m.rm_eo-m.rm_so); char* o=(char*)GC_MALLOC(len+1); memcpy(o,text+m.rm_so,len); o[len]=0; return o; }
#endif
#ifndef _WIN32
static int ailang_tls_init_done_=0;
static void ailang_tls_init_(void){ if(ailang_tls_init_done_) return; SSL_library_init(); SSL_load_error_strings(); OpenSSL_add_ssl_algorithms(); ailang_tls_init_done_=1; }
static int64_t tls_server_ctx(const char* cert, const char* key){ ailang_tls_init_(); SSL_CTX* ctx=SSL_CTX_new(TLS_server_method()); if(!ctx) return -1; if(!cert||SSL_CTX_use_certificate_file(ctx,cert,SSL_FILETYPE_PEM)<=0){ SSL_CTX_free(ctx); return -1; } if(!key||SSL_CTX_use_PrivateKey_file(ctx,key,SSL_FILETYPE_PEM)<=0){ SSL_CTX_free(ctx); return -1; } return (int64_t)(intptr_t)ctx; }
static int64_t tls_client_ctx(void){ ailang_tls_init_(); SSL_CTX* ctx=SSL_CTX_new(TLS_client_method()); if(!ctx) return -1; return (int64_t)(intptr_t)ctx; }
static void tls_free_ctx(int64_t ctx){ if(ctx>0) SSL_CTX_free((SSL_CTX*)(intptr_t)ctx); }
static int64_t tls_accept(int64_t ctx, int64_t fd){ if(ctx<=0||fd<0) return -1; SSL* ssl=SSL_new((SSL_CTX*)(intptr_t)ctx); if(!ssl) return -1; SSL_set_fd(ssl,(int)fd); if(SSL_accept(ssl)<=0){ SSL_free(ssl); return -1; } return (int64_t)(intptr_t)ssl; }
static int64_t tls_connect_fd(int64_t ctx, int64_t fd){ if(ctx<=0||fd<0) return -1; SSL* ssl=SSL_new((SSL_CTX*)(intptr_t)ctx); if(!ssl) return -1; SSL_set_fd(ssl,(int)fd); if(SSL_connect(ssl)<=0){ SSL_free(ssl); return -1; } return (int64_t)(intptr_t)ssl; }
static int64_t tls_send(int64_t ssl, ailang_bytes b){ if(ssl<=0) return -1; if(b.len==0) return 0; int n=SSL_write((SSL*)(intptr_t)ssl,b.data,(int)b.len); return (int64_t)n; }
static int64_t tls_send_str(int64_t ssl, const char* s){ if(ssl<=0||!s) return -1; size_t len=strlen(s); if(len==0) return 0; int n=SSL_write((SSL*)(intptr_t)ssl,s,(int)len); return (int64_t)n; }
static ailang_bytes tls_recv(int64_t ssl, int64_t max){ ailang_bytes r; r.len=0; r.data=(const uint8_t*)""; if(ssl<=0||max<=0) return r; uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)max); int n=SSL_read((SSL*)(intptr_t)ssl,buf,(int)max); if(n<=0) return r; r.len=(int64_t)n; r.data=buf; return r; }
static void tls_close(int64_t ssl){ if(ssl>0){ SSL_shutdown((SSL*)(intptr_t)ssl); SSL_free((SSL*)(intptr_t)ssl); } }
static const char* tls_error(void){ unsigned long e=ERR_peek_error(); if(e==0) return ""; char* buf=(char*)GC_MALLOC(256); ERR_error_string_n(e,buf,256); return buf; }
static ailang_bytes sha1(const char* s){ ailang_bytes r; uint8_t* buf=(uint8_t*)GC_MALLOC(20); SHA1((const unsigned char*)(s?s:""), s?strlen(s):0, buf); r.len=20; r.data=buf; return r; }
#endif
#ifndef _WIN32
static int64_t pg_connect(const char* conninfo){ PGconn* c=PQconnectdb(conninfo?conninfo:""); return (int64_t)(intptr_t)c; }
static int64_t pg_status(int64_t conn){ if(conn==0) return -1; return (int64_t)PQstatus((PGconn*)(intptr_t)conn); }
static const char* pg_error(int64_t conn){ if(conn==0) return "(null connection)"; const char* msg=PQerrorMessage((PGconn*)(intptr_t)conn); return msg?msg:""; }
static void pg_close(int64_t conn){ if(conn!=0) PQfinish((PGconn*)(intptr_t)conn); }
static int64_t pg_exec(int64_t conn, const char* sql){ if(conn==0||!sql) return 0; PGresult* r=PQexec((PGconn*)(intptr_t)conn,sql); return (int64_t)(intptr_t)r; }
static int64_t pg_ok(int64_t res){ if(res==0) return 0; int s=PQresultStatus((PGresult*)(intptr_t)res); return s==PGRES_COMMAND_OK||s==PGRES_TUPLES_OK; }
static const char* pg_result_error(int64_t res){ if(res==0) return "(null result)"; const char* m=PQresultErrorMessage((PGresult*)(intptr_t)res); return m?m:""; }
static void pg_clear(int64_t res){ if(res!=0) PQclear((PGresult*)(intptr_t)res); }
static int64_t pg_nrows(int64_t res){ if(res==0) return 0; return (int64_t)PQntuples((PGresult*)(intptr_t)res); }
static int64_t pg_ncols(int64_t res){ if(res==0) return 0; return (int64_t)PQnfields((PGresult*)(intptr_t)res); }
static const char* pg_value(int64_t res, int64_t row, int64_t col){ if(res==0) return ""; const char* v=PQgetvalue((PGresult*)(intptr_t)res,(int)row,(int)col); if(!v) return ""; size_t n=strlen(v); char* o=(char*)GC_MALLOC(n+1); memcpy(o,v,n+1); return o; }
static int64_t pg_isnull(int64_t res, int64_t row, int64_t col){ if(res==0) return 0; return PQgetisnull((PGresult*)(intptr_t)res,(int)row,(int)col)!=0; }
static const char* pg_col_name(int64_t res, int64_t col){ if(res==0) return ""; const char* nm=PQfname((PGresult*)(intptr_t)res,(int)col); if(!nm) return ""; size_t l=strlen(nm); char* o=(char*)GC_MALLOC(l+1); memcpy(o,nm,l+1); return o; }
static int64_t pg_affected(int64_t res){ if(res==0) return 0; const char* s=PQcmdTuples((PGresult*)(intptr_t)res); if(!s||!*s) return 0; return (int64_t)atoll(s); }
static const char* pg_escape(int64_t conn, const char* s){ if(conn==0||!s) return "''"; char* esc=PQescapeLiteral((PGconn*)(intptr_t)conn,s,strlen(s)); if(!esc) return "''"; size_t n=strlen(esc); char* o=(char*)GC_MALLOC(n+1); memcpy(o,esc,n+1); PQfreemem(esc); return o; }
#endif
static int g_argc=0; static char** g_argv=0;
typedef struct { void* fn; void* env; } closure_t;
#ifndef _WIN32
typedef struct { pthread_t th; closure_t clo; } ail_thread_t;
static void* ail_thread_tramp(void* p){ closure_t* c=(closure_t*)p; return (void*)(intptr_t)((int64_t(*)(void*))c->fn)(c->env); }
static int64_t thread_spawn(closure_t clo){ ail_thread_t* t=(ail_thread_t*)GC_MALLOC(sizeof(ail_thread_t)); t->clo=clo; if(pthread_create(&t->th,0,ail_thread_tramp,&t->clo)!=0) return 0; return (int64_t)(intptr_t)t; }
static int64_t thread_join(int64_t h){ if(h==0) return 0; ail_thread_t* t=(ail_thread_t*)(intptr_t)h; void* rv=0; pthread_join(t->th,&rv); return (int64_t)(intptr_t)rv; }
static int64_t mutex_new(void){ pthread_mutex_t* m=(pthread_mutex_t*)GC_MALLOC(sizeof(pthread_mutex_t)); pthread_mutex_init(m,0); return (int64_t)(intptr_t)m; }
static int64_t mutex_lock(int64_t h){ if(h) pthread_mutex_lock((pthread_mutex_t*)(intptr_t)h); return 0; }
static int64_t mutex_unlock(int64_t h){ if(h) pthread_mutex_unlock((pthread_mutex_t*)(intptr_t)h); return 0; }
typedef struct { pthread_mutex_t m; pthread_cond_t ne, nf; int64_t* buf; int64_t cap, head, tail, cnt; int closed; } ail_chan_t;
static int64_t chan_new(int64_t cap){ if(cap<1) cap=1; ail_chan_t* c=(ail_chan_t*)GC_MALLOC(sizeof(ail_chan_t)); pthread_mutex_init(&c->m,0); pthread_cond_init(&c->ne,0); pthread_cond_init(&c->nf,0); c->buf=(int64_t*)GC_MALLOC(sizeof(int64_t)*(size_t)cap); c->cap=cap; c->head=0; c->tail=0; c->cnt=0; c->closed=0; return (int64_t)(intptr_t)c; }
static int64_t chan_send(int64_t h, int64_t v){ ail_chan_t* c=(ail_chan_t*)(intptr_t)h; if(!c) return 0; pthread_mutex_lock(&c->m); while(c->cnt==c->cap && !c->closed) pthread_cond_wait(&c->nf,&c->m); if(c->closed){ pthread_mutex_unlock(&c->m); return 0; } c->buf[c->tail]=v; c->tail=(c->tail+1)%c->cap; c->cnt++; pthread_cond_signal(&c->ne); pthread_mutex_unlock(&c->m); return 1; }
static int64_t chan_recv(int64_t h){ ail_chan_t* c=(ail_chan_t*)(intptr_t)h; if(!c) return 0; pthread_mutex_lock(&c->m); while(c->cnt==0 && !c->closed) pthread_cond_wait(&c->ne,&c->m); if(c->cnt==0 && c->closed){ pthread_mutex_unlock(&c->m); return 0; } int64_t v=c->buf[c->head]; c->head=(c->head+1)%c->cap; c->cnt--; pthread_cond_signal(&c->nf); pthread_mutex_unlock(&c->m); return v; }
static int64_t chan_close(int64_t h){ ail_chan_t* c=(ail_chan_t*)(intptr_t)h; if(!c) return 0; pthread_mutex_lock(&c->m); c->closed=1; pthread_cond_broadcast(&c->ne); pthread_cond_broadcast(&c->nf); pthread_mutex_unlock(&c->m); return 0; }
#endif

typedef struct s_Token s_Token;
typedef struct s_StructDef s_StructDef;
typedef struct s_EnumDef s_EnumDef;
typedef struct s_Func s_Func;
typedef struct s_ClassDef s_ClassDef;
typedef struct s_P s_P;
typedef struct s_Binds s_Binds;
typedef struct s_Syms s_Syms;
typedef struct s_Expr s_Expr;
typedef struct s_Stmt s_Stmt;

typedef struct { int64_t len; int64_t cap; int64_t* data; } arr_i64;
typedef struct { int64_t len; int64_t cap; const char** data; } arr_str;
typedef struct { int64_t len; int64_t cap; double* data; } arr_f64;
typedef struct { int64_t len; int64_t cap; s_Token* data; } arr_Token;
typedef struct { int64_t len; int64_t cap; s_StructDef* data; } arr_StructDef;
typedef struct { int64_t len; int64_t cap; s_EnumDef* data; } arr_EnumDef;
typedef struct { int64_t len; int64_t cap; s_Func* data; } arr_Func;
typedef struct { int64_t len; int64_t cap; s_ClassDef* data; } arr_ClassDef;
typedef struct { int64_t len; int64_t cap; s_P* data; } arr_P;
typedef struct { int64_t len; int64_t cap; s_Binds* data; } arr_Binds;
typedef struct { int64_t len; int64_t cap; s_Syms* data; } arr_Syms;
typedef struct { int64_t len; int64_t cap; s_Expr* data; } arr_Expr;
typedef struct { int64_t len; int64_t cap; s_Stmt* data; } arr_Stmt;
typedef struct map_str_i64_s map_str_i64_s;
typedef map_str_i64_s* map_str_i64;
typedef struct map_str_str_s map_str_str_s;
typedef map_str_str_s* map_str_str;
typedef struct map_str_f64_s map_str_f64_s;
typedef map_str_f64_s* map_str_f64;
typedef struct map_str_Token_s map_str_Token_s;
typedef map_str_Token_s* map_str_Token;
typedef struct map_str_StructDef_s map_str_StructDef_s;
typedef map_str_StructDef_s* map_str_StructDef;
typedef struct map_str_EnumDef_s map_str_EnumDef_s;
typedef map_str_EnumDef_s* map_str_EnumDef;
typedef struct map_str_Func_s map_str_Func_s;
typedef map_str_Func_s* map_str_Func;
typedef struct map_str_ClassDef_s map_str_ClassDef_s;
typedef map_str_ClassDef_s* map_str_ClassDef;
typedef struct map_str_P_s map_str_P_s;
typedef map_str_P_s* map_str_P;
typedef struct map_str_Binds_s map_str_Binds_s;
typedef map_str_Binds_s* map_str_Binds;
typedef struct map_str_Syms_s map_str_Syms_s;
typedef map_str_Syms_s* map_str_Syms;
typedef struct map_str_Expr_s map_str_Expr_s;
typedef map_str_Expr_s* map_str_Expr;
typedef struct map_str_Stmt_s map_str_Stmt_s;
typedef map_str_Stmt_s* map_str_Stmt;
typedef struct map_i64_i64_s map_i64_i64_s;
typedef map_i64_i64_s* map_i64_i64;
typedef struct map_i64_str_s map_i64_str_s;
typedef map_i64_str_s* map_i64_str;
typedef struct map_i64_f64_s map_i64_f64_s;
typedef map_i64_f64_s* map_i64_f64;
typedef struct map_i64_Token_s map_i64_Token_s;
typedef map_i64_Token_s* map_i64_Token;
typedef struct map_i64_StructDef_s map_i64_StructDef_s;
typedef map_i64_StructDef_s* map_i64_StructDef;
typedef struct map_i64_EnumDef_s map_i64_EnumDef_s;
typedef map_i64_EnumDef_s* map_i64_EnumDef;
typedef struct map_i64_Func_s map_i64_Func_s;
typedef map_i64_Func_s* map_i64_Func;
typedef struct map_i64_ClassDef_s map_i64_ClassDef_s;
typedef map_i64_ClassDef_s* map_i64_ClassDef;
typedef struct map_i64_P_s map_i64_P_s;
typedef map_i64_P_s* map_i64_P;
typedef struct map_i64_Binds_s map_i64_Binds_s;
typedef map_i64_Binds_s* map_i64_Binds;
typedef struct map_i64_Syms_s map_i64_Syms_s;
typedef map_i64_Syms_s* map_i64_Syms;
typedef struct map_i64_Expr_s map_i64_Expr_s;
typedef map_i64_Expr_s* map_i64_Expr;
typedef struct map_i64_Stmt_s map_i64_Stmt_s;
typedef map_i64_Stmt_s* map_i64_Stmt;
typedef struct map_f64_i64_s map_f64_i64_s;
typedef map_f64_i64_s* map_f64_i64;
typedef struct map_f64_str_s map_f64_str_s;
typedef map_f64_str_s* map_f64_str;
typedef struct map_f64_f64_s map_f64_f64_s;
typedef map_f64_f64_s* map_f64_f64;
typedef struct map_f64_Token_s map_f64_Token_s;
typedef map_f64_Token_s* map_f64_Token;
typedef struct map_f64_StructDef_s map_f64_StructDef_s;
typedef map_f64_StructDef_s* map_f64_StructDef;
typedef struct map_f64_EnumDef_s map_f64_EnumDef_s;
typedef map_f64_EnumDef_s* map_f64_EnumDef;
typedef struct map_f64_Func_s map_f64_Func_s;
typedef map_f64_Func_s* map_f64_Func;
typedef struct map_f64_ClassDef_s map_f64_ClassDef_s;
typedef map_f64_ClassDef_s* map_f64_ClassDef;
typedef struct map_f64_P_s map_f64_P_s;
typedef map_f64_P_s* map_f64_P;
typedef struct map_f64_Binds_s map_f64_Binds_s;
typedef map_f64_Binds_s* map_f64_Binds;
typedef struct map_f64_Syms_s map_f64_Syms_s;
typedef map_f64_Syms_s* map_f64_Syms;
typedef struct map_f64_Expr_s map_f64_Expr_s;
typedef map_f64_Expr_s* map_f64_Expr;
typedef struct map_f64_Stmt_s map_f64_Stmt_s;
typedef map_f64_Stmt_s* map_f64_Stmt;

struct s_Token { int64_t kind; const char* text; int64_t pos; };
struct s_StructDef { const char* name; arr_str fnames; arr_str ftypes; };
struct s_EnumDef { const char* name; arr_str vnames; arr_str vftypes; };
struct s_Func { const char* name; arr_str params; arr_str ptypes; const char* ret; const char* lib; arr_str tparams; arr_Stmt body; };
struct s_ClassDef { const char* name; const char* parent; arr_str fnames; arr_str ftypes; arr_Func methods; arr_str vflags; };
struct s_P { arr_Token toks; int64_t pos; const char* src; };
struct s_Binds { arr_str names; arr_Expr vals; };
struct s_Syms { map_str_str vty; map_str_str fld; map_str_str ctors; map_str_str frets; map_str_str evar; map_str_str vft; arr_Func gfns; arr_Func lams; };
struct s_Expr { int tag; union {
  struct { int64_t f0; } Num;
  struct { const char* f0; } Flt;
  struct { const char* f0; } Str;
  struct { const char* f0; } Var;
  struct { int64_t f0; s_Expr* f1; s_Expr* f2; } Bin;
  struct { int64_t f0; s_Expr* f1; } Unary;
  struct { const char* f0; arr_Expr f1; } Call;
  struct { s_Expr* f0; const char* f1; } Field;
  struct { s_Expr* f0; s_Expr* f1; } Index;
  struct { arr_Expr f0; const char* f1; } Array;
  struct { const char* f0; arr_Expr f1; arr_Expr f2; } MapLit;
  struct { s_Expr* f0; } Addr;
  struct { s_Expr* f0; arr_str f1; arr_str f2; arr_Expr f3; } Match;
  struct { s_Expr* f0; s_Expr* f1; s_Expr* f2; } IfE;
  struct { s_Expr* f0; } Try;
  struct { arr_str f0; arr_str f1; s_Expr* f2; int64_t f3; } Lambda;
  struct { arr_Expr f0; } Tuple;
  struct { arr_Stmt f0; } BlockE;
  struct { } Bad;
} u; };
struct s_Stmt { int tag; union {
  struct { const char* f0; s_Expr* f1; int64_t f2; } SDecl;
  struct { arr_str f0; s_Expr* f1; } SDestructure;
  struct { const char* f0; s_Expr* f1; int64_t f2; } SAssign;
  struct { s_Expr* f0; s_Expr* f1; s_Expr* f2; int64_t f3; } SIdxAssign;
  struct { s_Expr* f0; const char* f1; s_Expr* f2; int64_t f3; } SFieldAssign;
  struct { s_Expr* f0; int64_t f1; } SReturn;
  struct { s_Expr* f0; int64_t f1; } SPrint;
  struct { s_Expr* f0; arr_Stmt f1; arr_Stmt f2; } SIf;
  struct { s_Expr* f0; arr_Stmt f1; } SLoop;
  struct { const char* f0; s_Expr* f1; arr_Stmt f2; } SLoopIn;
  struct { const char* f0; const char* f1; s_Expr* f2; arr_Stmt f3; } SLoopKV;
  struct { const char* f0; s_Expr* f1; s_Expr* f2; arr_Stmt f3; } SLoopRange;
  struct { } SBreak;
  struct { } SContinue;
  struct { s_Expr* f0; int64_t f1; } SExpr;
} u; };
struct map_str_i64_s { int64_t cap; int64_t len; const char** keys; int64_t* values; unsigned char* occupied; };
struct map_str_str_s { int64_t cap; int64_t len; const char** keys; const char** values; unsigned char* occupied; };
struct map_str_f64_s { int64_t cap; int64_t len; const char** keys; double* values; unsigned char* occupied; };
struct map_str_Token_s { int64_t cap; int64_t len; const char** keys; s_Token* values; unsigned char* occupied; };
struct map_str_StructDef_s { int64_t cap; int64_t len; const char** keys; s_StructDef* values; unsigned char* occupied; };
struct map_str_EnumDef_s { int64_t cap; int64_t len; const char** keys; s_EnumDef* values; unsigned char* occupied; };
struct map_str_Func_s { int64_t cap; int64_t len; const char** keys; s_Func* values; unsigned char* occupied; };
struct map_str_ClassDef_s { int64_t cap; int64_t len; const char** keys; s_ClassDef* values; unsigned char* occupied; };
struct map_str_P_s { int64_t cap; int64_t len; const char** keys; s_P* values; unsigned char* occupied; };
struct map_str_Binds_s { int64_t cap; int64_t len; const char** keys; s_Binds* values; unsigned char* occupied; };
struct map_str_Syms_s { int64_t cap; int64_t len; const char** keys; s_Syms* values; unsigned char* occupied; };
struct map_str_Expr_s { int64_t cap; int64_t len; const char** keys; s_Expr* values; unsigned char* occupied; };
struct map_str_Stmt_s { int64_t cap; int64_t len; const char** keys; s_Stmt* values; unsigned char* occupied; };
struct map_i64_i64_s { int64_t cap; int64_t len; int64_t* keys; int64_t* values; unsigned char* occupied; };
struct map_i64_str_s { int64_t cap; int64_t len; int64_t* keys; const char** values; unsigned char* occupied; };
struct map_i64_f64_s { int64_t cap; int64_t len; int64_t* keys; double* values; unsigned char* occupied; };
struct map_i64_Token_s { int64_t cap; int64_t len; int64_t* keys; s_Token* values; unsigned char* occupied; };
struct map_i64_StructDef_s { int64_t cap; int64_t len; int64_t* keys; s_StructDef* values; unsigned char* occupied; };
struct map_i64_EnumDef_s { int64_t cap; int64_t len; int64_t* keys; s_EnumDef* values; unsigned char* occupied; };
struct map_i64_Func_s { int64_t cap; int64_t len; int64_t* keys; s_Func* values; unsigned char* occupied; };
struct map_i64_ClassDef_s { int64_t cap; int64_t len; int64_t* keys; s_ClassDef* values; unsigned char* occupied; };
struct map_i64_P_s { int64_t cap; int64_t len; int64_t* keys; s_P* values; unsigned char* occupied; };
struct map_i64_Binds_s { int64_t cap; int64_t len; int64_t* keys; s_Binds* values; unsigned char* occupied; };
struct map_i64_Syms_s { int64_t cap; int64_t len; int64_t* keys; s_Syms* values; unsigned char* occupied; };
struct map_i64_Expr_s { int64_t cap; int64_t len; int64_t* keys; s_Expr* values; unsigned char* occupied; };
struct map_i64_Stmt_s { int64_t cap; int64_t len; int64_t* keys; s_Stmt* values; unsigned char* occupied; };
struct map_f64_i64_s { int64_t cap; int64_t len; double* keys; int64_t* values; unsigned char* occupied; };
struct map_f64_str_s { int64_t cap; int64_t len; double* keys; const char** values; unsigned char* occupied; };
struct map_f64_f64_s { int64_t cap; int64_t len; double* keys; double* values; unsigned char* occupied; };
struct map_f64_Token_s { int64_t cap; int64_t len; double* keys; s_Token* values; unsigned char* occupied; };
struct map_f64_StructDef_s { int64_t cap; int64_t len; double* keys; s_StructDef* values; unsigned char* occupied; };
struct map_f64_EnumDef_s { int64_t cap; int64_t len; double* keys; s_EnumDef* values; unsigned char* occupied; };
struct map_f64_Func_s { int64_t cap; int64_t len; double* keys; s_Func* values; unsigned char* occupied; };
struct map_f64_ClassDef_s { int64_t cap; int64_t len; double* keys; s_ClassDef* values; unsigned char* occupied; };
struct map_f64_P_s { int64_t cap; int64_t len; double* keys; s_P* values; unsigned char* occupied; };
struct map_f64_Binds_s { int64_t cap; int64_t len; double* keys; s_Binds* values; unsigned char* occupied; };
struct map_f64_Syms_s { int64_t cap; int64_t len; double* keys; s_Syms* values; unsigned char* occupied; };
struct map_f64_Expr_s { int64_t cap; int64_t len; double* keys; s_Expr* values; unsigned char* occupied; };
struct map_f64_Stmt_s { int64_t cap; int64_t len; double* keys; s_Stmt* values; unsigned char* occupied; };

static arr_i64 arr_i64_new(void){ arr_i64 a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_i64 arr_i64_push(arr_i64 a, int64_t x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; int64_t* nd=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); if(a.len) memcpy(nd,a.data,a.len*sizeof(int64_t)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static int64_t arr_i64_get(arr_i64 a, int64_t i){ return a.data[i]; }
static int64_t arr_i64_len(arr_i64 a){ return a.len; }
static arr_i64 arr_i64_pop(arr_i64 a){ if(a.len>0) a.len-=1; return a; }
static arr_i64 arr_i64_slice(arr_i64 a, int64_t lo, int64_t hi){ arr_i64 r=arr_i64_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_i64_push(r,a.data[i]); return r; }
static arr_i64 arr_i64_reverse(arr_i64 a){ arr_i64 r=arr_i64_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_i64_push(r,a.data[i]); return r; }
static int __cmp_i64(const void* a, const void* b){ int64_t x=*(const int64_t*)a, y=*(const int64_t*)b; return (x>y)-(x<y); }
static arr_i64 arr_i64_sort(arr_i64 a){ arr_i64 r=arr_i64_reverse(arr_i64_reverse(a)); if(r.len>1) qsort(r.data,r.len,sizeof(int64_t),__cmp_i64); return r; }
static int64_t arr_i64_contains(arr_i64 a, int64_t x){ for(int64_t i=0;i<a.len;i++) if(a.data[i]==x) return 1; return 0; }
static int64_t arr_i64_index_of(arr_i64 a, int64_t x){ for(int64_t i=0;i<a.len;i++) if(a.data[i]==x) return i; return -1; }
static const char* arr_i64_join(arr_i64 a, const char* sep){ if(!sep) sep=""; size_t sl=strlen(sep); size_t cap=(size_t)a.len*22+(a.len>1?sl*(size_t)(a.len-1):0)+1; if(cap<16) cap=16; char* o=(char*)GC_MALLOC(cap); char* w=o; for(int64_t i=0;i<a.len;i++){ if(i>0&&sl){ memcpy(w,sep,sl); w+=sl; } int n=snprintf(w,cap-(size_t)(w-o),"%lld",(long long)a.data[i]); if(n>0) w+=n; } *w=0; return o; }
static arr_str arr_str_new(void){ arr_str a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_str arr_str_push(arr_str a, const char* x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; const char** nd=(const char**)GC_MALLOC(nc*sizeof(const char*)); if(a.len) memcpy(nd,a.data,a.len*sizeof(const char*)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static const char* arr_str_get(arr_str a, int64_t i){ return a.data[i]; }
static int64_t arr_str_len(arr_str a){ return a.len; }
static arr_str arr_str_pop(arr_str a){ if(a.len>0) a.len-=1; return a; }
static arr_str arr_str_slice(arr_str a, int64_t lo, int64_t hi){ arr_str r=arr_str_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_str_push(r,a.data[i]); return r; }
static arr_str arr_str_reverse(arr_str a){ arr_str r=arr_str_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_str_push(r,a.data[i]); return r; }
static int __cmp_str(const void* a, const void* b){ return strcmp(*(const char* const*)a, *(const char* const*)b); }
static arr_str arr_str_sort(arr_str a){ arr_str r=arr_str_reverse(arr_str_reverse(a)); if(r.len>1) qsort(r.data,r.len,sizeof(const char*),__cmp_str); return r; }
static int64_t arr_str_contains(arr_str a, const char* x){ for(int64_t i=0;i<a.len;i++) if(strcmp(a.data[i],x)==0) return 1; return 0; }
static int64_t arr_str_index_of(arr_str a, const char* x){ for(int64_t i=0;i<a.len;i++) if(strcmp(a.data[i],x)==0) return i; return -1; }
static const char* arr_str_join(arr_str a, const char* sep){ if(!sep) sep=""; size_t sl=strlen(sep); size_t tot=0; for(int64_t i=0;i<a.len;i++) tot+=a.data[i]?strlen(a.data[i]):0; if(a.len>1) tot+=sl*(size_t)(a.len-1); char* o=(char*)GC_MALLOC(tot+1); char* w=o; for(int64_t i=0;i<a.len;i++){ if(i>0&&sl){ memcpy(w,sep,sl); w+=sl; } const char* s=a.data[i]?a.data[i]:""; size_t n=strlen(s); if(n){ memcpy(w,s,n); w+=n; } } *w=0; return o; }
static arr_f64 arr_f64_new(void){ arr_f64 a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_f64 arr_f64_push(arr_f64 a, double x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; double* nd=(double*)GC_MALLOC(nc*sizeof(double)); if(a.len) memcpy(nd,a.data,a.len*sizeof(double)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static double arr_f64_get(arr_f64 a, int64_t i){ return a.data[i]; }
static int64_t arr_f64_len(arr_f64 a){ return a.len; }
static arr_f64 arr_f64_pop(arr_f64 a){ if(a.len>0) a.len-=1; return a; }
static arr_f64 arr_f64_slice(arr_f64 a, int64_t lo, int64_t hi){ arr_f64 r=arr_f64_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_f64_push(r,a.data[i]); return r; }
static arr_f64 arr_f64_reverse(arr_f64 a){ arr_f64 r=arr_f64_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_f64_push(r,a.data[i]); return r; }
static void print_arr_i64(arr_i64 a){ printf("["); for(int64_t i=0;i<a.len;i++){ if(i>0) printf(", "); printf("%lld",(long long)a.data[i]); } printf("]"); }
static void print_arr_str(arr_str a){ printf("["); for(int64_t i=0;i<a.len;i++){ if(i>0) printf(", "); putchar(34); printf("%s", a.data[i] ? a.data[i] : ""); putchar(34); } printf("]"); }
static arr_str ailang_args(void){ arr_str a = arr_str_new(); for(int i=1;i<g_argc;i++) a = arr_str_push(a, g_argv[i]); return a; }
static arr_str split(const char* s, const char* sep){ arr_str a=arr_str_new(); if(!s) s=""; if(!sep||!*sep){ return arr_str_push(a,s); } size_t ls=strlen(sep); const char* r=s; const char* p; while((p=strstr(r,sep))!=0){ size_t n=(size_t)(p-r); char* pc=(char*)GC_MALLOC(n+1); memcpy(pc,r,n); pc[n]=0; a=arr_str_push(a,pc); r=p+ls; } size_t n=strlen(r); char* pc=(char*)GC_MALLOC(n+1); memcpy(pc,r,n+1); a=arr_str_push(a,pc); return a; }
static arr_Token arr_Token_new(void){ arr_Token a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_Token arr_Token_push(arr_Token a, s_Token x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_Token* nd=(s_Token*)GC_MALLOC(nc*sizeof(s_Token)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_Token)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_Token arr_Token_get(arr_Token a, int64_t i){ return a.data[i]; }
static int64_t arr_Token_len(arr_Token a){ return a.len; }
static arr_Token arr_Token_pop(arr_Token a){ if(a.len>0) a.len-=1; return a; }
static arr_Token arr_Token_slice(arr_Token a, int64_t lo, int64_t hi){ arr_Token r=arr_Token_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_Token_push(r,a.data[i]); return r; }
static arr_Token arr_Token_reverse(arr_Token a){ arr_Token r=arr_Token_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_Token_push(r,a.data[i]); return r; }
static arr_StructDef arr_StructDef_new(void){ arr_StructDef a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_StructDef arr_StructDef_push(arr_StructDef a, s_StructDef x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_StructDef* nd=(s_StructDef*)GC_MALLOC(nc*sizeof(s_StructDef)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_StructDef)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_StructDef arr_StructDef_get(arr_StructDef a, int64_t i){ return a.data[i]; }
static int64_t arr_StructDef_len(arr_StructDef a){ return a.len; }
static arr_StructDef arr_StructDef_pop(arr_StructDef a){ if(a.len>0) a.len-=1; return a; }
static arr_StructDef arr_StructDef_slice(arr_StructDef a, int64_t lo, int64_t hi){ arr_StructDef r=arr_StructDef_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_StructDef_push(r,a.data[i]); return r; }
static arr_StructDef arr_StructDef_reverse(arr_StructDef a){ arr_StructDef r=arr_StructDef_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_StructDef_push(r,a.data[i]); return r; }
static arr_EnumDef arr_EnumDef_new(void){ arr_EnumDef a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_EnumDef arr_EnumDef_push(arr_EnumDef a, s_EnumDef x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_EnumDef* nd=(s_EnumDef*)GC_MALLOC(nc*sizeof(s_EnumDef)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_EnumDef)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_EnumDef arr_EnumDef_get(arr_EnumDef a, int64_t i){ return a.data[i]; }
static int64_t arr_EnumDef_len(arr_EnumDef a){ return a.len; }
static arr_EnumDef arr_EnumDef_pop(arr_EnumDef a){ if(a.len>0) a.len-=1; return a; }
static arr_EnumDef arr_EnumDef_slice(arr_EnumDef a, int64_t lo, int64_t hi){ arr_EnumDef r=arr_EnumDef_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_EnumDef_push(r,a.data[i]); return r; }
static arr_EnumDef arr_EnumDef_reverse(arr_EnumDef a){ arr_EnumDef r=arr_EnumDef_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_EnumDef_push(r,a.data[i]); return r; }
static arr_Func arr_Func_new(void){ arr_Func a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_Func arr_Func_push(arr_Func a, s_Func x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_Func* nd=(s_Func*)GC_MALLOC(nc*sizeof(s_Func)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_Func)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_Func arr_Func_get(arr_Func a, int64_t i){ return a.data[i]; }
static int64_t arr_Func_len(arr_Func a){ return a.len; }
static arr_Func arr_Func_pop(arr_Func a){ if(a.len>0) a.len-=1; return a; }
static arr_Func arr_Func_slice(arr_Func a, int64_t lo, int64_t hi){ arr_Func r=arr_Func_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_Func_push(r,a.data[i]); return r; }
static arr_Func arr_Func_reverse(arr_Func a){ arr_Func r=arr_Func_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_Func_push(r,a.data[i]); return r; }
static arr_ClassDef arr_ClassDef_new(void){ arr_ClassDef a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_ClassDef arr_ClassDef_push(arr_ClassDef a, s_ClassDef x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_ClassDef* nd=(s_ClassDef*)GC_MALLOC(nc*sizeof(s_ClassDef)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_ClassDef)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_ClassDef arr_ClassDef_get(arr_ClassDef a, int64_t i){ return a.data[i]; }
static int64_t arr_ClassDef_len(arr_ClassDef a){ return a.len; }
static arr_ClassDef arr_ClassDef_pop(arr_ClassDef a){ if(a.len>0) a.len-=1; return a; }
static arr_ClassDef arr_ClassDef_slice(arr_ClassDef a, int64_t lo, int64_t hi){ arr_ClassDef r=arr_ClassDef_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_ClassDef_push(r,a.data[i]); return r; }
static arr_ClassDef arr_ClassDef_reverse(arr_ClassDef a){ arr_ClassDef r=arr_ClassDef_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_ClassDef_push(r,a.data[i]); return r; }
static arr_P arr_P_new(void){ arr_P a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_P arr_P_push(arr_P a, s_P x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_P* nd=(s_P*)GC_MALLOC(nc*sizeof(s_P)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_P)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_P arr_P_get(arr_P a, int64_t i){ return a.data[i]; }
static int64_t arr_P_len(arr_P a){ return a.len; }
static arr_P arr_P_pop(arr_P a){ if(a.len>0) a.len-=1; return a; }
static arr_P arr_P_slice(arr_P a, int64_t lo, int64_t hi){ arr_P r=arr_P_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_P_push(r,a.data[i]); return r; }
static arr_P arr_P_reverse(arr_P a){ arr_P r=arr_P_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_P_push(r,a.data[i]); return r; }
static arr_Binds arr_Binds_new(void){ arr_Binds a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_Binds arr_Binds_push(arr_Binds a, s_Binds x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_Binds* nd=(s_Binds*)GC_MALLOC(nc*sizeof(s_Binds)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_Binds)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_Binds arr_Binds_get(arr_Binds a, int64_t i){ return a.data[i]; }
static int64_t arr_Binds_len(arr_Binds a){ return a.len; }
static arr_Binds arr_Binds_pop(arr_Binds a){ if(a.len>0) a.len-=1; return a; }
static arr_Binds arr_Binds_slice(arr_Binds a, int64_t lo, int64_t hi){ arr_Binds r=arr_Binds_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_Binds_push(r,a.data[i]); return r; }
static arr_Binds arr_Binds_reverse(arr_Binds a){ arr_Binds r=arr_Binds_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_Binds_push(r,a.data[i]); return r; }
static arr_Syms arr_Syms_new(void){ arr_Syms a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_Syms arr_Syms_push(arr_Syms a, s_Syms x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_Syms* nd=(s_Syms*)GC_MALLOC(nc*sizeof(s_Syms)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_Syms)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_Syms arr_Syms_get(arr_Syms a, int64_t i){ return a.data[i]; }
static int64_t arr_Syms_len(arr_Syms a){ return a.len; }
static arr_Syms arr_Syms_pop(arr_Syms a){ if(a.len>0) a.len-=1; return a; }
static arr_Syms arr_Syms_slice(arr_Syms a, int64_t lo, int64_t hi){ arr_Syms r=arr_Syms_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_Syms_push(r,a.data[i]); return r; }
static arr_Syms arr_Syms_reverse(arr_Syms a){ arr_Syms r=arr_Syms_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_Syms_push(r,a.data[i]); return r; }
static arr_Expr arr_Expr_new(void){ arr_Expr a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_Expr arr_Expr_push(arr_Expr a, s_Expr x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_Expr* nd=(s_Expr*)GC_MALLOC(nc*sizeof(s_Expr)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_Expr)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_Expr arr_Expr_get(arr_Expr a, int64_t i){ return a.data[i]; }
static int64_t arr_Expr_len(arr_Expr a){ return a.len; }
static arr_Expr arr_Expr_pop(arr_Expr a){ if(a.len>0) a.len-=1; return a; }
static arr_Expr arr_Expr_slice(arr_Expr a, int64_t lo, int64_t hi){ arr_Expr r=arr_Expr_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_Expr_push(r,a.data[i]); return r; }
static arr_Expr arr_Expr_reverse(arr_Expr a){ arr_Expr r=arr_Expr_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_Expr_push(r,a.data[i]); return r; }
static arr_Stmt arr_Stmt_new(void){ arr_Stmt a; a.len=0; a.cap=0; a.data=0; return a; }
static arr_Stmt arr_Stmt_push(arr_Stmt a, s_Stmt x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; s_Stmt* nd=(s_Stmt*)GC_MALLOC(nc*sizeof(s_Stmt)); if(a.len) memcpy(nd,a.data,a.len*sizeof(s_Stmt)); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }
static s_Stmt arr_Stmt_get(arr_Stmt a, int64_t i){ return a.data[i]; }
static int64_t arr_Stmt_len(arr_Stmt a){ return a.len; }
static arr_Stmt arr_Stmt_pop(arr_Stmt a){ if(a.len>0) a.len-=1; return a; }
static arr_Stmt arr_Stmt_slice(arr_Stmt a, int64_t lo, int64_t hi){ arr_Stmt r=arr_Stmt_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_Stmt_push(r,a.data[i]); return r; }
static arr_Stmt arr_Stmt_reverse(arr_Stmt a){ arr_Stmt r=arr_Stmt_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_Stmt_push(r,a.data[i]); return r; }
static uint64_t map_str_i64_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_i64 map_str_i64_new(void){ map_str_i64 m=(map_str_i64)GC_MALLOC(sizeof(map_str_i64_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_i64_grow(map_str_i64 m){ int64_t oc=m->cap; const char** ok=m->keys; int64_t* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_i64_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_i64_set(map_str_i64 m, const char* k, int64_t val){ if(m->len*10>=m->cap*7) map_str_i64_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static int64_t map_str_i64_get(map_str_i64 m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return 0; }
static int64_t map_str_i64_has(map_str_i64 m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_i64_keys(map_str_i64 m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_i64 map_str_i64_values(map_str_i64 m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->values[i]); return r; }
static int64_t map_str_i64_len(map_str_i64 m){ return m->len; }
static void map_str_i64_print(map_str_i64 m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("\"%s\": %lld", (m->keys[i] ? m->keys[i] : ""), (long long)(m->values[i])); } printf("}"); }
static uint64_t map_str_str_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_str map_str_str_new(void){ map_str_str m=(map_str_str)GC_MALLOC(sizeof(map_str_str_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(const char**)GC_MALLOC(8*sizeof(const char*)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_str_grow(map_str_str m){ int64_t oc=m->cap; const char** ok=m->keys; const char** ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_str_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_str_set(map_str_str m, const char* k, const char* val){ if(m->len*10>=m->cap*7) map_str_str_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static const char* map_str_str_get(map_str_str m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return ""; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return ""; }
static int64_t map_str_str_has(map_str_str m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_str_keys(map_str_str m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_str map_str_str_values(map_str_str m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->values[i]); return r; }
static int64_t map_str_str_len(map_str_str m){ return m->len; }
static void map_str_str_print(map_str_str m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("\"%s\": \"%s\"", (m->keys[i] ? m->keys[i] : ""), (m->values[i] ? m->values[i] : "")); } printf("}"); }
static uint64_t map_str_f64_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_f64 map_str_f64_new(void){ map_str_f64 m=(map_str_f64)GC_MALLOC(sizeof(map_str_f64_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(double*)GC_MALLOC(8*sizeof(double)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_f64_grow(map_str_f64 m){ int64_t oc=m->cap; const char** ok=m->keys; double* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(double*)GC_MALLOC(nc*sizeof(double)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_f64_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_f64_set(map_str_f64 m, const char* k, double val){ if(m->len*10>=m->cap*7) map_str_f64_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static double map_str_f64_get(map_str_f64 m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return 0; }
static int64_t map_str_f64_has(map_str_f64 m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_f64_keys(map_str_f64 m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_f64 map_str_f64_values(map_str_f64 m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->values[i]); return r; }
static int64_t map_str_f64_len(map_str_f64 m){ return m->len; }
static void map_str_f64_print(map_str_f64 m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("\"%s\": %g", (m->keys[i] ? m->keys[i] : ""), (double)(m->values[i])); } printf("}"); }
static uint64_t map_str_Token_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_Token map_str_Token_new(void){ map_str_Token m=(map_str_Token)GC_MALLOC(sizeof(map_str_Token_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_Token*)GC_MALLOC(8*sizeof(s_Token)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_Token_grow(map_str_Token m){ int64_t oc=m->cap; const char** ok=m->keys; s_Token* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_Token*)GC_MALLOC(nc*sizeof(s_Token)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_Token_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_Token_set(map_str_Token m, const char* k, s_Token val){ if(m->len*10>=m->cap*7) map_str_Token_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_Token map_str_Token_get(map_str_Token m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Token){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_Token){0}; }
static int64_t map_str_Token_has(map_str_Token m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_Token_keys(map_str_Token m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_Token map_str_Token_values(map_str_Token m){ arr_Token r=arr_Token_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Token_push(r,m->values[i]); return r; }
static int64_t map_str_Token_len(map_str_Token m){ return m->len; }
static void map_str_Token_print(map_str_Token m){ (void)m; }
static uint64_t map_str_StructDef_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_StructDef map_str_StructDef_new(void){ map_str_StructDef m=(map_str_StructDef)GC_MALLOC(sizeof(map_str_StructDef_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_StructDef*)GC_MALLOC(8*sizeof(s_StructDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_StructDef_grow(map_str_StructDef m){ int64_t oc=m->cap; const char** ok=m->keys; s_StructDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_StructDef*)GC_MALLOC(nc*sizeof(s_StructDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_StructDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_StructDef_set(map_str_StructDef m, const char* k, s_StructDef val){ if(m->len*10>=m->cap*7) map_str_StructDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_StructDef map_str_StructDef_get(map_str_StructDef m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_StructDef){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_StructDef){0}; }
static int64_t map_str_StructDef_has(map_str_StructDef m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_StructDef_keys(map_str_StructDef m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_StructDef map_str_StructDef_values(map_str_StructDef m){ arr_StructDef r=arr_StructDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_StructDef_push(r,m->values[i]); return r; }
static int64_t map_str_StructDef_len(map_str_StructDef m){ return m->len; }
static void map_str_StructDef_print(map_str_StructDef m){ (void)m; }
static uint64_t map_str_EnumDef_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_EnumDef map_str_EnumDef_new(void){ map_str_EnumDef m=(map_str_EnumDef)GC_MALLOC(sizeof(map_str_EnumDef_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_EnumDef*)GC_MALLOC(8*sizeof(s_EnumDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_EnumDef_grow(map_str_EnumDef m){ int64_t oc=m->cap; const char** ok=m->keys; s_EnumDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_EnumDef*)GC_MALLOC(nc*sizeof(s_EnumDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_EnumDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_EnumDef_set(map_str_EnumDef m, const char* k, s_EnumDef val){ if(m->len*10>=m->cap*7) map_str_EnumDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_EnumDef map_str_EnumDef_get(map_str_EnumDef m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_EnumDef){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_EnumDef){0}; }
static int64_t map_str_EnumDef_has(map_str_EnumDef m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_EnumDef_keys(map_str_EnumDef m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_EnumDef map_str_EnumDef_values(map_str_EnumDef m){ arr_EnumDef r=arr_EnumDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_EnumDef_push(r,m->values[i]); return r; }
static int64_t map_str_EnumDef_len(map_str_EnumDef m){ return m->len; }
static void map_str_EnumDef_print(map_str_EnumDef m){ (void)m; }
static uint64_t map_str_Func_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_Func map_str_Func_new(void){ map_str_Func m=(map_str_Func)GC_MALLOC(sizeof(map_str_Func_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_Func*)GC_MALLOC(8*sizeof(s_Func)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_Func_grow(map_str_Func m){ int64_t oc=m->cap; const char** ok=m->keys; s_Func* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_Func*)GC_MALLOC(nc*sizeof(s_Func)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_Func_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_Func_set(map_str_Func m, const char* k, s_Func val){ if(m->len*10>=m->cap*7) map_str_Func_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_Func map_str_Func_get(map_str_Func m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Func){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_Func){0}; }
static int64_t map_str_Func_has(map_str_Func m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_Func_keys(map_str_Func m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_Func map_str_Func_values(map_str_Func m){ arr_Func r=arr_Func_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Func_push(r,m->values[i]); return r; }
static int64_t map_str_Func_len(map_str_Func m){ return m->len; }
static void map_str_Func_print(map_str_Func m){ (void)m; }
static uint64_t map_str_ClassDef_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_ClassDef map_str_ClassDef_new(void){ map_str_ClassDef m=(map_str_ClassDef)GC_MALLOC(sizeof(map_str_ClassDef_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_ClassDef*)GC_MALLOC(8*sizeof(s_ClassDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_ClassDef_grow(map_str_ClassDef m){ int64_t oc=m->cap; const char** ok=m->keys; s_ClassDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_ClassDef*)GC_MALLOC(nc*sizeof(s_ClassDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_ClassDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_ClassDef_set(map_str_ClassDef m, const char* k, s_ClassDef val){ if(m->len*10>=m->cap*7) map_str_ClassDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_ClassDef map_str_ClassDef_get(map_str_ClassDef m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_ClassDef){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_ClassDef){0}; }
static int64_t map_str_ClassDef_has(map_str_ClassDef m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_ClassDef_keys(map_str_ClassDef m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_ClassDef map_str_ClassDef_values(map_str_ClassDef m){ arr_ClassDef r=arr_ClassDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_ClassDef_push(r,m->values[i]); return r; }
static int64_t map_str_ClassDef_len(map_str_ClassDef m){ return m->len; }
static void map_str_ClassDef_print(map_str_ClassDef m){ (void)m; }
static uint64_t map_str_P_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_P map_str_P_new(void){ map_str_P m=(map_str_P)GC_MALLOC(sizeof(map_str_P_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_P*)GC_MALLOC(8*sizeof(s_P)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_P_grow(map_str_P m){ int64_t oc=m->cap; const char** ok=m->keys; s_P* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_P*)GC_MALLOC(nc*sizeof(s_P)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_P_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_P_set(map_str_P m, const char* k, s_P val){ if(m->len*10>=m->cap*7) map_str_P_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_P map_str_P_get(map_str_P m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_P){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_P){0}; }
static int64_t map_str_P_has(map_str_P m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_P_keys(map_str_P m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_P map_str_P_values(map_str_P m){ arr_P r=arr_P_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_P_push(r,m->values[i]); return r; }
static int64_t map_str_P_len(map_str_P m){ return m->len; }
static void map_str_P_print(map_str_P m){ (void)m; }
static uint64_t map_str_Binds_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_Binds map_str_Binds_new(void){ map_str_Binds m=(map_str_Binds)GC_MALLOC(sizeof(map_str_Binds_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_Binds*)GC_MALLOC(8*sizeof(s_Binds)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_Binds_grow(map_str_Binds m){ int64_t oc=m->cap; const char** ok=m->keys; s_Binds* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_Binds*)GC_MALLOC(nc*sizeof(s_Binds)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_Binds_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_Binds_set(map_str_Binds m, const char* k, s_Binds val){ if(m->len*10>=m->cap*7) map_str_Binds_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_Binds map_str_Binds_get(map_str_Binds m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Binds){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_Binds){0}; }
static int64_t map_str_Binds_has(map_str_Binds m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_Binds_keys(map_str_Binds m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_Binds map_str_Binds_values(map_str_Binds m){ arr_Binds r=arr_Binds_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Binds_push(r,m->values[i]); return r; }
static int64_t map_str_Binds_len(map_str_Binds m){ return m->len; }
static void map_str_Binds_print(map_str_Binds m){ (void)m; }
static uint64_t map_str_Syms_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_Syms map_str_Syms_new(void){ map_str_Syms m=(map_str_Syms)GC_MALLOC(sizeof(map_str_Syms_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_Syms*)GC_MALLOC(8*sizeof(s_Syms)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_Syms_grow(map_str_Syms m){ int64_t oc=m->cap; const char** ok=m->keys; s_Syms* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_Syms*)GC_MALLOC(nc*sizeof(s_Syms)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_Syms_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_Syms_set(map_str_Syms m, const char* k, s_Syms val){ if(m->len*10>=m->cap*7) map_str_Syms_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_Syms map_str_Syms_get(map_str_Syms m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Syms){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_Syms){0}; }
static int64_t map_str_Syms_has(map_str_Syms m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_Syms_keys(map_str_Syms m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_Syms map_str_Syms_values(map_str_Syms m){ arr_Syms r=arr_Syms_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Syms_push(r,m->values[i]); return r; }
static int64_t map_str_Syms_len(map_str_Syms m){ return m->len; }
static void map_str_Syms_print(map_str_Syms m){ (void)m; }
static uint64_t map_str_Expr_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_Expr map_str_Expr_new(void){ map_str_Expr m=(map_str_Expr)GC_MALLOC(sizeof(map_str_Expr_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_Expr*)GC_MALLOC(8*sizeof(s_Expr)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_Expr_grow(map_str_Expr m){ int64_t oc=m->cap; const char** ok=m->keys; s_Expr* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_Expr*)GC_MALLOC(nc*sizeof(s_Expr)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_Expr_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_Expr_set(map_str_Expr m, const char* k, s_Expr val){ if(m->len*10>=m->cap*7) map_str_Expr_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_Expr map_str_Expr_get(map_str_Expr m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Expr){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_Expr){0}; }
static int64_t map_str_Expr_has(map_str_Expr m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_Expr_keys(map_str_Expr m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_Expr map_str_Expr_values(map_str_Expr m){ arr_Expr r=arr_Expr_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Expr_push(r,m->values[i]); return r; }
static int64_t map_str_Expr_len(map_str_Expr m){ return m->len; }
static void map_str_Expr_print(map_str_Expr m){ (void)m; }
static uint64_t map_str_Stmt_hash(const char* s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }
static map_str_Stmt map_str_Stmt_new(void){ map_str_Stmt m=(map_str_Stmt)GC_MALLOC(sizeof(map_str_Stmt_s)); m->cap=8; m->len=0; m->keys=(const char**)GC_MALLOC(8*sizeof(const char*)); m->values=(s_Stmt*)GC_MALLOC(8*sizeof(s_Stmt)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_str_Stmt_grow(map_str_Stmt m){ int64_t oc=m->cap; const char** ok=m->keys; s_Stmt* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->values=(s_Stmt*)GC_MALLOC(nc*sizeof(s_Stmt)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_str_Stmt_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_str_Stmt_set(map_str_Stmt m, const char* k, s_Stmt val){ if(m->len*10>=m->cap*7) map_str_Stmt_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(strcmp(m->keys[i],k)==0){ m->values[i]=val; return; } } }
static s_Stmt map_str_Stmt_get(map_str_Stmt m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Stmt){0}; if(strcmp(m->keys[i],k)==0) return m->values[i]; } return (s_Stmt){0}; }
static int64_t map_str_Stmt_has(map_str_Stmt m, const char* k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_str_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(strcmp(m->keys[i],k)==0) return 1; } return 0; }
static arr_str map_str_Stmt_keys(map_str_Stmt m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->keys[i]); return r; }
static arr_Stmt map_str_Stmt_values(map_str_Stmt m){ arr_Stmt r=arr_Stmt_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Stmt_push(r,m->values[i]); return r; }
static int64_t map_str_Stmt_len(map_str_Stmt m){ return m->len; }
static void map_str_Stmt_print(map_str_Stmt m){ (void)m; }
static uint64_t map_i64_i64_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_i64 map_i64_i64_new(void){ map_i64_i64 m=(map_i64_i64)GC_MALLOC(sizeof(map_i64_i64_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_i64_grow(map_i64_i64 m){ int64_t oc=m->cap; int64_t* ok=m->keys; int64_t* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_i64_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_i64_set(map_i64_i64 m, int64_t k, int64_t val){ if(m->len*10>=m->cap*7) map_i64_i64_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static int64_t map_i64_i64_get(map_i64_i64 m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return m->values[i]; } return 0; }
static int64_t map_i64_i64_has(map_i64_i64 m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_i64_keys(map_i64_i64 m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_i64 map_i64_i64_values(map_i64_i64 m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->values[i]); return r; }
static int64_t map_i64_i64_len(map_i64_i64 m){ return m->len; }
static void map_i64_i64_print(map_i64_i64 m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("%lld: %lld", (long long)(m->keys[i]), (long long)(m->values[i])); } printf("}"); }
static uint64_t map_i64_str_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_str map_i64_str_new(void){ map_i64_str m=(map_i64_str)GC_MALLOC(sizeof(map_i64_str_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(const char**)GC_MALLOC(8*sizeof(const char*)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_str_grow(map_i64_str m){ int64_t oc=m->cap; int64_t* ok=m->keys; const char** ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_str_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_str_set(map_i64_str m, int64_t k, const char* val){ if(m->len*10>=m->cap*7) map_i64_str_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static const char* map_i64_str_get(map_i64_str m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return ""; if(m->keys[i]==k) return m->values[i]; } return ""; }
static int64_t map_i64_str_has(map_i64_str m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_str_keys(map_i64_str m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_str map_i64_str_values(map_i64_str m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->values[i]); return r; }
static int64_t map_i64_str_len(map_i64_str m){ return m->len; }
static void map_i64_str_print(map_i64_str m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("%lld: \"%s\"", (long long)(m->keys[i]), (m->values[i] ? m->values[i] : "")); } printf("}"); }
static uint64_t map_i64_f64_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_f64 map_i64_f64_new(void){ map_i64_f64 m=(map_i64_f64)GC_MALLOC(sizeof(map_i64_f64_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(double*)GC_MALLOC(8*sizeof(double)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_f64_grow(map_i64_f64 m){ int64_t oc=m->cap; int64_t* ok=m->keys; double* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(double*)GC_MALLOC(nc*sizeof(double)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_f64_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_f64_set(map_i64_f64 m, int64_t k, double val){ if(m->len*10>=m->cap*7) map_i64_f64_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static double map_i64_f64_get(map_i64_f64 m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return m->values[i]; } return 0; }
static int64_t map_i64_f64_has(map_i64_f64 m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_f64_keys(map_i64_f64 m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_f64 map_i64_f64_values(map_i64_f64 m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->values[i]); return r; }
static int64_t map_i64_f64_len(map_i64_f64 m){ return m->len; }
static void map_i64_f64_print(map_i64_f64 m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("%lld: %g", (long long)(m->keys[i]), (double)(m->values[i])); } printf("}"); }
static uint64_t map_i64_Token_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_Token map_i64_Token_new(void){ map_i64_Token m=(map_i64_Token)GC_MALLOC(sizeof(map_i64_Token_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_Token*)GC_MALLOC(8*sizeof(s_Token)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_Token_grow(map_i64_Token m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_Token* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_Token*)GC_MALLOC(nc*sizeof(s_Token)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_Token_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_Token_set(map_i64_Token m, int64_t k, s_Token val){ if(m->len*10>=m->cap*7) map_i64_Token_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Token map_i64_Token_get(map_i64_Token m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Token){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Token){0}; }
static int64_t map_i64_Token_has(map_i64_Token m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_Token_keys(map_i64_Token m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_Token map_i64_Token_values(map_i64_Token m){ arr_Token r=arr_Token_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Token_push(r,m->values[i]); return r; }
static int64_t map_i64_Token_len(map_i64_Token m){ return m->len; }
static void map_i64_Token_print(map_i64_Token m){ (void)m; }
static uint64_t map_i64_StructDef_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_StructDef map_i64_StructDef_new(void){ map_i64_StructDef m=(map_i64_StructDef)GC_MALLOC(sizeof(map_i64_StructDef_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_StructDef*)GC_MALLOC(8*sizeof(s_StructDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_StructDef_grow(map_i64_StructDef m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_StructDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_StructDef*)GC_MALLOC(nc*sizeof(s_StructDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_StructDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_StructDef_set(map_i64_StructDef m, int64_t k, s_StructDef val){ if(m->len*10>=m->cap*7) map_i64_StructDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_StructDef map_i64_StructDef_get(map_i64_StructDef m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_StructDef){0}; if(m->keys[i]==k) return m->values[i]; } return (s_StructDef){0}; }
static int64_t map_i64_StructDef_has(map_i64_StructDef m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_StructDef_keys(map_i64_StructDef m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_StructDef map_i64_StructDef_values(map_i64_StructDef m){ arr_StructDef r=arr_StructDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_StructDef_push(r,m->values[i]); return r; }
static int64_t map_i64_StructDef_len(map_i64_StructDef m){ return m->len; }
static void map_i64_StructDef_print(map_i64_StructDef m){ (void)m; }
static uint64_t map_i64_EnumDef_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_EnumDef map_i64_EnumDef_new(void){ map_i64_EnumDef m=(map_i64_EnumDef)GC_MALLOC(sizeof(map_i64_EnumDef_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_EnumDef*)GC_MALLOC(8*sizeof(s_EnumDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_EnumDef_grow(map_i64_EnumDef m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_EnumDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_EnumDef*)GC_MALLOC(nc*sizeof(s_EnumDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_EnumDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_EnumDef_set(map_i64_EnumDef m, int64_t k, s_EnumDef val){ if(m->len*10>=m->cap*7) map_i64_EnumDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_EnumDef map_i64_EnumDef_get(map_i64_EnumDef m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_EnumDef){0}; if(m->keys[i]==k) return m->values[i]; } return (s_EnumDef){0}; }
static int64_t map_i64_EnumDef_has(map_i64_EnumDef m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_EnumDef_keys(map_i64_EnumDef m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_EnumDef map_i64_EnumDef_values(map_i64_EnumDef m){ arr_EnumDef r=arr_EnumDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_EnumDef_push(r,m->values[i]); return r; }
static int64_t map_i64_EnumDef_len(map_i64_EnumDef m){ return m->len; }
static void map_i64_EnumDef_print(map_i64_EnumDef m){ (void)m; }
static uint64_t map_i64_Func_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_Func map_i64_Func_new(void){ map_i64_Func m=(map_i64_Func)GC_MALLOC(sizeof(map_i64_Func_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_Func*)GC_MALLOC(8*sizeof(s_Func)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_Func_grow(map_i64_Func m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_Func* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_Func*)GC_MALLOC(nc*sizeof(s_Func)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_Func_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_Func_set(map_i64_Func m, int64_t k, s_Func val){ if(m->len*10>=m->cap*7) map_i64_Func_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Func map_i64_Func_get(map_i64_Func m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Func){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Func){0}; }
static int64_t map_i64_Func_has(map_i64_Func m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_Func_keys(map_i64_Func m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_Func map_i64_Func_values(map_i64_Func m){ arr_Func r=arr_Func_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Func_push(r,m->values[i]); return r; }
static int64_t map_i64_Func_len(map_i64_Func m){ return m->len; }
static void map_i64_Func_print(map_i64_Func m){ (void)m; }
static uint64_t map_i64_ClassDef_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_ClassDef map_i64_ClassDef_new(void){ map_i64_ClassDef m=(map_i64_ClassDef)GC_MALLOC(sizeof(map_i64_ClassDef_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_ClassDef*)GC_MALLOC(8*sizeof(s_ClassDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_ClassDef_grow(map_i64_ClassDef m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_ClassDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_ClassDef*)GC_MALLOC(nc*sizeof(s_ClassDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_ClassDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_ClassDef_set(map_i64_ClassDef m, int64_t k, s_ClassDef val){ if(m->len*10>=m->cap*7) map_i64_ClassDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_ClassDef map_i64_ClassDef_get(map_i64_ClassDef m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_ClassDef){0}; if(m->keys[i]==k) return m->values[i]; } return (s_ClassDef){0}; }
static int64_t map_i64_ClassDef_has(map_i64_ClassDef m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_ClassDef_keys(map_i64_ClassDef m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_ClassDef map_i64_ClassDef_values(map_i64_ClassDef m){ arr_ClassDef r=arr_ClassDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_ClassDef_push(r,m->values[i]); return r; }
static int64_t map_i64_ClassDef_len(map_i64_ClassDef m){ return m->len; }
static void map_i64_ClassDef_print(map_i64_ClassDef m){ (void)m; }
static uint64_t map_i64_P_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_P map_i64_P_new(void){ map_i64_P m=(map_i64_P)GC_MALLOC(sizeof(map_i64_P_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_P*)GC_MALLOC(8*sizeof(s_P)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_P_grow(map_i64_P m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_P* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_P*)GC_MALLOC(nc*sizeof(s_P)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_P_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_P_set(map_i64_P m, int64_t k, s_P val){ if(m->len*10>=m->cap*7) map_i64_P_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_P map_i64_P_get(map_i64_P m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_P){0}; if(m->keys[i]==k) return m->values[i]; } return (s_P){0}; }
static int64_t map_i64_P_has(map_i64_P m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_P_keys(map_i64_P m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_P map_i64_P_values(map_i64_P m){ arr_P r=arr_P_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_P_push(r,m->values[i]); return r; }
static int64_t map_i64_P_len(map_i64_P m){ return m->len; }
static void map_i64_P_print(map_i64_P m){ (void)m; }
static uint64_t map_i64_Binds_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_Binds map_i64_Binds_new(void){ map_i64_Binds m=(map_i64_Binds)GC_MALLOC(sizeof(map_i64_Binds_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_Binds*)GC_MALLOC(8*sizeof(s_Binds)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_Binds_grow(map_i64_Binds m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_Binds* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_Binds*)GC_MALLOC(nc*sizeof(s_Binds)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_Binds_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_Binds_set(map_i64_Binds m, int64_t k, s_Binds val){ if(m->len*10>=m->cap*7) map_i64_Binds_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Binds map_i64_Binds_get(map_i64_Binds m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Binds){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Binds){0}; }
static int64_t map_i64_Binds_has(map_i64_Binds m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_Binds_keys(map_i64_Binds m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_Binds map_i64_Binds_values(map_i64_Binds m){ arr_Binds r=arr_Binds_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Binds_push(r,m->values[i]); return r; }
static int64_t map_i64_Binds_len(map_i64_Binds m){ return m->len; }
static void map_i64_Binds_print(map_i64_Binds m){ (void)m; }
static uint64_t map_i64_Syms_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_Syms map_i64_Syms_new(void){ map_i64_Syms m=(map_i64_Syms)GC_MALLOC(sizeof(map_i64_Syms_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_Syms*)GC_MALLOC(8*sizeof(s_Syms)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_Syms_grow(map_i64_Syms m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_Syms* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_Syms*)GC_MALLOC(nc*sizeof(s_Syms)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_Syms_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_Syms_set(map_i64_Syms m, int64_t k, s_Syms val){ if(m->len*10>=m->cap*7) map_i64_Syms_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Syms map_i64_Syms_get(map_i64_Syms m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Syms){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Syms){0}; }
static int64_t map_i64_Syms_has(map_i64_Syms m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_Syms_keys(map_i64_Syms m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_Syms map_i64_Syms_values(map_i64_Syms m){ arr_Syms r=arr_Syms_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Syms_push(r,m->values[i]); return r; }
static int64_t map_i64_Syms_len(map_i64_Syms m){ return m->len; }
static void map_i64_Syms_print(map_i64_Syms m){ (void)m; }
static uint64_t map_i64_Expr_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_Expr map_i64_Expr_new(void){ map_i64_Expr m=(map_i64_Expr)GC_MALLOC(sizeof(map_i64_Expr_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_Expr*)GC_MALLOC(8*sizeof(s_Expr)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_Expr_grow(map_i64_Expr m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_Expr* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_Expr*)GC_MALLOC(nc*sizeof(s_Expr)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_Expr_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_Expr_set(map_i64_Expr m, int64_t k, s_Expr val){ if(m->len*10>=m->cap*7) map_i64_Expr_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Expr map_i64_Expr_get(map_i64_Expr m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Expr){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Expr){0}; }
static int64_t map_i64_Expr_has(map_i64_Expr m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_Expr_keys(map_i64_Expr m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_Expr map_i64_Expr_values(map_i64_Expr m){ arr_Expr r=arr_Expr_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Expr_push(r,m->values[i]); return r; }
static int64_t map_i64_Expr_len(map_i64_Expr m){ return m->len; }
static void map_i64_Expr_print(map_i64_Expr m){ (void)m; }
static uint64_t map_i64_Stmt_hash(int64_t k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_i64_Stmt map_i64_Stmt_new(void){ map_i64_Stmt m=(map_i64_Stmt)GC_MALLOC(sizeof(map_i64_Stmt_s)); m->cap=8; m->len=0; m->keys=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->values=(s_Stmt*)GC_MALLOC(8*sizeof(s_Stmt)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_i64_Stmt_grow(map_i64_Stmt m){ int64_t oc=m->cap; int64_t* ok=m->keys; s_Stmt* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->values=(s_Stmt*)GC_MALLOC(nc*sizeof(s_Stmt)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_i64_Stmt_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_i64_Stmt_set(map_i64_Stmt m, int64_t k, s_Stmt val){ if(m->len*10>=m->cap*7) map_i64_Stmt_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Stmt map_i64_Stmt_get(map_i64_Stmt m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Stmt){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Stmt){0}; }
static int64_t map_i64_Stmt_has(map_i64_Stmt m, int64_t k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_i64_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_i64 map_i64_Stmt_keys(map_i64_Stmt m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->keys[i]); return r; }
static arr_Stmt map_i64_Stmt_values(map_i64_Stmt m){ arr_Stmt r=arr_Stmt_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Stmt_push(r,m->values[i]); return r; }
static int64_t map_i64_Stmt_len(map_i64_Stmt m){ return m->len; }
static void map_i64_Stmt_print(map_i64_Stmt m){ (void)m; }
static uint64_t map_f64_i64_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_i64 map_f64_i64_new(void){ map_f64_i64 m=(map_f64_i64)GC_MALLOC(sizeof(map_f64_i64_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(int64_t*)GC_MALLOC(8*sizeof(int64_t)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_i64_grow(map_f64_i64 m){ int64_t oc=m->cap; double* ok=m->keys; int64_t* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(int64_t*)GC_MALLOC(nc*sizeof(int64_t)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_i64_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_i64_set(map_f64_i64 m, double k, int64_t val){ if(m->len*10>=m->cap*7) map_f64_i64_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static int64_t map_f64_i64_get(map_f64_i64 m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return m->values[i]; } return 0; }
static int64_t map_f64_i64_has(map_f64_i64 m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_i64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_i64_keys(map_f64_i64 m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_i64 map_f64_i64_values(map_f64_i64 m){ arr_i64 r=arr_i64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_i64_push(r,m->values[i]); return r; }
static int64_t map_f64_i64_len(map_f64_i64 m){ return m->len; }
static void map_f64_i64_print(map_f64_i64 m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("%g: %lld", (double)(m->keys[i]), (long long)(m->values[i])); } printf("}"); }
static uint64_t map_f64_str_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_str map_f64_str_new(void){ map_f64_str m=(map_f64_str)GC_MALLOC(sizeof(map_f64_str_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(const char**)GC_MALLOC(8*sizeof(const char*)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_str_grow(map_f64_str m){ int64_t oc=m->cap; double* ok=m->keys; const char** ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(const char**)GC_MALLOC(nc*sizeof(const char*)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_str_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_str_set(map_f64_str m, double k, const char* val){ if(m->len*10>=m->cap*7) map_f64_str_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static const char* map_f64_str_get(map_f64_str m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return ""; if(m->keys[i]==k) return m->values[i]; } return ""; }
static int64_t map_f64_str_has(map_f64_str m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_str_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_str_keys(map_f64_str m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_str map_f64_str_values(map_f64_str m){ arr_str r=arr_str_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_str_push(r,m->values[i]); return r; }
static int64_t map_f64_str_len(map_f64_str m){ return m->len; }
static void map_f64_str_print(map_f64_str m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("%g: \"%s\"", (double)(m->keys[i]), (m->values[i] ? m->values[i] : "")); } printf("}"); }
static uint64_t map_f64_f64_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_f64 map_f64_f64_new(void){ map_f64_f64 m=(map_f64_f64)GC_MALLOC(sizeof(map_f64_f64_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(double*)GC_MALLOC(8*sizeof(double)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_f64_grow(map_f64_f64 m){ int64_t oc=m->cap; double* ok=m->keys; double* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(double*)GC_MALLOC(nc*sizeof(double)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_f64_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_f64_set(map_f64_f64 m, double k, double val){ if(m->len*10>=m->cap*7) map_f64_f64_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static double map_f64_f64_get(map_f64_f64 m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return m->values[i]; } return 0; }
static int64_t map_f64_f64_has(map_f64_f64 m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_f64_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_f64_keys(map_f64_f64 m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_f64 map_f64_f64_values(map_f64_f64 m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->values[i]); return r; }
static int64_t map_f64_f64_len(map_f64_f64 m){ return m->len; }
static void map_f64_f64_print(map_f64_f64 m){ printf("{"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(", "); __f=0; printf("%g: %g", (double)(m->keys[i]), (double)(m->values[i])); } printf("}"); }
static uint64_t map_f64_Token_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_Token map_f64_Token_new(void){ map_f64_Token m=(map_f64_Token)GC_MALLOC(sizeof(map_f64_Token_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_Token*)GC_MALLOC(8*sizeof(s_Token)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_Token_grow(map_f64_Token m){ int64_t oc=m->cap; double* ok=m->keys; s_Token* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_Token*)GC_MALLOC(nc*sizeof(s_Token)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_Token_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_Token_set(map_f64_Token m, double k, s_Token val){ if(m->len*10>=m->cap*7) map_f64_Token_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Token map_f64_Token_get(map_f64_Token m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Token){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Token){0}; }
static int64_t map_f64_Token_has(map_f64_Token m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Token_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_Token_keys(map_f64_Token m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_Token map_f64_Token_values(map_f64_Token m){ arr_Token r=arr_Token_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Token_push(r,m->values[i]); return r; }
static int64_t map_f64_Token_len(map_f64_Token m){ return m->len; }
static void map_f64_Token_print(map_f64_Token m){ (void)m; }
static uint64_t map_f64_StructDef_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_StructDef map_f64_StructDef_new(void){ map_f64_StructDef m=(map_f64_StructDef)GC_MALLOC(sizeof(map_f64_StructDef_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_StructDef*)GC_MALLOC(8*sizeof(s_StructDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_StructDef_grow(map_f64_StructDef m){ int64_t oc=m->cap; double* ok=m->keys; s_StructDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_StructDef*)GC_MALLOC(nc*sizeof(s_StructDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_StructDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_StructDef_set(map_f64_StructDef m, double k, s_StructDef val){ if(m->len*10>=m->cap*7) map_f64_StructDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_StructDef map_f64_StructDef_get(map_f64_StructDef m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_StructDef){0}; if(m->keys[i]==k) return m->values[i]; } return (s_StructDef){0}; }
static int64_t map_f64_StructDef_has(map_f64_StructDef m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_StructDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_StructDef_keys(map_f64_StructDef m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_StructDef map_f64_StructDef_values(map_f64_StructDef m){ arr_StructDef r=arr_StructDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_StructDef_push(r,m->values[i]); return r; }
static int64_t map_f64_StructDef_len(map_f64_StructDef m){ return m->len; }
static void map_f64_StructDef_print(map_f64_StructDef m){ (void)m; }
static uint64_t map_f64_EnumDef_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_EnumDef map_f64_EnumDef_new(void){ map_f64_EnumDef m=(map_f64_EnumDef)GC_MALLOC(sizeof(map_f64_EnumDef_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_EnumDef*)GC_MALLOC(8*sizeof(s_EnumDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_EnumDef_grow(map_f64_EnumDef m){ int64_t oc=m->cap; double* ok=m->keys; s_EnumDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_EnumDef*)GC_MALLOC(nc*sizeof(s_EnumDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_EnumDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_EnumDef_set(map_f64_EnumDef m, double k, s_EnumDef val){ if(m->len*10>=m->cap*7) map_f64_EnumDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_EnumDef map_f64_EnumDef_get(map_f64_EnumDef m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_EnumDef){0}; if(m->keys[i]==k) return m->values[i]; } return (s_EnumDef){0}; }
static int64_t map_f64_EnumDef_has(map_f64_EnumDef m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_EnumDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_EnumDef_keys(map_f64_EnumDef m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_EnumDef map_f64_EnumDef_values(map_f64_EnumDef m){ arr_EnumDef r=arr_EnumDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_EnumDef_push(r,m->values[i]); return r; }
static int64_t map_f64_EnumDef_len(map_f64_EnumDef m){ return m->len; }
static void map_f64_EnumDef_print(map_f64_EnumDef m){ (void)m; }
static uint64_t map_f64_Func_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_Func map_f64_Func_new(void){ map_f64_Func m=(map_f64_Func)GC_MALLOC(sizeof(map_f64_Func_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_Func*)GC_MALLOC(8*sizeof(s_Func)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_Func_grow(map_f64_Func m){ int64_t oc=m->cap; double* ok=m->keys; s_Func* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_Func*)GC_MALLOC(nc*sizeof(s_Func)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_Func_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_Func_set(map_f64_Func m, double k, s_Func val){ if(m->len*10>=m->cap*7) map_f64_Func_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Func map_f64_Func_get(map_f64_Func m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Func){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Func){0}; }
static int64_t map_f64_Func_has(map_f64_Func m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Func_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_Func_keys(map_f64_Func m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_Func map_f64_Func_values(map_f64_Func m){ arr_Func r=arr_Func_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Func_push(r,m->values[i]); return r; }
static int64_t map_f64_Func_len(map_f64_Func m){ return m->len; }
static void map_f64_Func_print(map_f64_Func m){ (void)m; }
static uint64_t map_f64_ClassDef_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_ClassDef map_f64_ClassDef_new(void){ map_f64_ClassDef m=(map_f64_ClassDef)GC_MALLOC(sizeof(map_f64_ClassDef_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_ClassDef*)GC_MALLOC(8*sizeof(s_ClassDef)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_ClassDef_grow(map_f64_ClassDef m){ int64_t oc=m->cap; double* ok=m->keys; s_ClassDef* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_ClassDef*)GC_MALLOC(nc*sizeof(s_ClassDef)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_ClassDef_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_ClassDef_set(map_f64_ClassDef m, double k, s_ClassDef val){ if(m->len*10>=m->cap*7) map_f64_ClassDef_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_ClassDef map_f64_ClassDef_get(map_f64_ClassDef m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_ClassDef){0}; if(m->keys[i]==k) return m->values[i]; } return (s_ClassDef){0}; }
static int64_t map_f64_ClassDef_has(map_f64_ClassDef m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_ClassDef_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_ClassDef_keys(map_f64_ClassDef m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_ClassDef map_f64_ClassDef_values(map_f64_ClassDef m){ arr_ClassDef r=arr_ClassDef_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_ClassDef_push(r,m->values[i]); return r; }
static int64_t map_f64_ClassDef_len(map_f64_ClassDef m){ return m->len; }
static void map_f64_ClassDef_print(map_f64_ClassDef m){ (void)m; }
static uint64_t map_f64_P_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_P map_f64_P_new(void){ map_f64_P m=(map_f64_P)GC_MALLOC(sizeof(map_f64_P_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_P*)GC_MALLOC(8*sizeof(s_P)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_P_grow(map_f64_P m){ int64_t oc=m->cap; double* ok=m->keys; s_P* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_P*)GC_MALLOC(nc*sizeof(s_P)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_P_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_P_set(map_f64_P m, double k, s_P val){ if(m->len*10>=m->cap*7) map_f64_P_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_P map_f64_P_get(map_f64_P m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_P){0}; if(m->keys[i]==k) return m->values[i]; } return (s_P){0}; }
static int64_t map_f64_P_has(map_f64_P m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_P_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_P_keys(map_f64_P m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_P map_f64_P_values(map_f64_P m){ arr_P r=arr_P_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_P_push(r,m->values[i]); return r; }
static int64_t map_f64_P_len(map_f64_P m){ return m->len; }
static void map_f64_P_print(map_f64_P m){ (void)m; }
static uint64_t map_f64_Binds_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_Binds map_f64_Binds_new(void){ map_f64_Binds m=(map_f64_Binds)GC_MALLOC(sizeof(map_f64_Binds_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_Binds*)GC_MALLOC(8*sizeof(s_Binds)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_Binds_grow(map_f64_Binds m){ int64_t oc=m->cap; double* ok=m->keys; s_Binds* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_Binds*)GC_MALLOC(nc*sizeof(s_Binds)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_Binds_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_Binds_set(map_f64_Binds m, double k, s_Binds val){ if(m->len*10>=m->cap*7) map_f64_Binds_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Binds map_f64_Binds_get(map_f64_Binds m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Binds){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Binds){0}; }
static int64_t map_f64_Binds_has(map_f64_Binds m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Binds_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_Binds_keys(map_f64_Binds m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_Binds map_f64_Binds_values(map_f64_Binds m){ arr_Binds r=arr_Binds_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Binds_push(r,m->values[i]); return r; }
static int64_t map_f64_Binds_len(map_f64_Binds m){ return m->len; }
static void map_f64_Binds_print(map_f64_Binds m){ (void)m; }
static uint64_t map_f64_Syms_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_Syms map_f64_Syms_new(void){ map_f64_Syms m=(map_f64_Syms)GC_MALLOC(sizeof(map_f64_Syms_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_Syms*)GC_MALLOC(8*sizeof(s_Syms)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_Syms_grow(map_f64_Syms m){ int64_t oc=m->cap; double* ok=m->keys; s_Syms* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_Syms*)GC_MALLOC(nc*sizeof(s_Syms)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_Syms_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_Syms_set(map_f64_Syms m, double k, s_Syms val){ if(m->len*10>=m->cap*7) map_f64_Syms_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Syms map_f64_Syms_get(map_f64_Syms m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Syms){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Syms){0}; }
static int64_t map_f64_Syms_has(map_f64_Syms m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Syms_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_Syms_keys(map_f64_Syms m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_Syms map_f64_Syms_values(map_f64_Syms m){ arr_Syms r=arr_Syms_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Syms_push(r,m->values[i]); return r; }
static int64_t map_f64_Syms_len(map_f64_Syms m){ return m->len; }
static void map_f64_Syms_print(map_f64_Syms m){ (void)m; }
static uint64_t map_f64_Expr_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_Expr map_f64_Expr_new(void){ map_f64_Expr m=(map_f64_Expr)GC_MALLOC(sizeof(map_f64_Expr_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_Expr*)GC_MALLOC(8*sizeof(s_Expr)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_Expr_grow(map_f64_Expr m){ int64_t oc=m->cap; double* ok=m->keys; s_Expr* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_Expr*)GC_MALLOC(nc*sizeof(s_Expr)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_Expr_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_Expr_set(map_f64_Expr m, double k, s_Expr val){ if(m->len*10>=m->cap*7) map_f64_Expr_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Expr map_f64_Expr_get(map_f64_Expr m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Expr){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Expr){0}; }
static int64_t map_f64_Expr_has(map_f64_Expr m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Expr_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_Expr_keys(map_f64_Expr m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_Expr map_f64_Expr_values(map_f64_Expr m){ arr_Expr r=arr_Expr_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Expr_push(r,m->values[i]); return r; }
static int64_t map_f64_Expr_len(map_f64_Expr m){ return m->len; }
static void map_f64_Expr_print(map_f64_Expr m){ (void)m; }
static uint64_t map_f64_Stmt_hash(double k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }
static map_f64_Stmt map_f64_Stmt_new(void){ map_f64_Stmt m=(map_f64_Stmt)GC_MALLOC(sizeof(map_f64_Stmt_s)); m->cap=8; m->len=0; m->keys=(double*)GC_MALLOC(8*sizeof(double)); m->values=(s_Stmt*)GC_MALLOC(8*sizeof(s_Stmt)); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }
static void map_f64_Stmt_grow(map_f64_Stmt m){ int64_t oc=m->cap; double* ok=m->keys; s_Stmt* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=(double*)GC_MALLOC(nc*sizeof(double)); m->values=(s_Stmt*)GC_MALLOC(nc*sizeof(s_Stmt)); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h=map_f64_Stmt_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }
static void map_f64_Stmt_set(map_f64_Stmt m, double k, s_Stmt val){ if(m->len*10>=m->cap*7) map_f64_Stmt_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if(m->keys[i]==k){ m->values[i]=val; return; } } }
static s_Stmt map_f64_Stmt_get(map_f64_Stmt m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return (s_Stmt){0}; if(m->keys[i]==k) return m->values[i]; } return (s_Stmt){0}; }
static int64_t map_f64_Stmt_has(map_f64_Stmt m, double k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h=map_f64_Stmt_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if(m->keys[i]==k) return 1; } return 0; }
static arr_f64 map_f64_Stmt_keys(map_f64_Stmt m){ arr_f64 r=arr_f64_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_f64_push(r,m->keys[i]); return r; }
static arr_Stmt map_f64_Stmt_values(map_f64_Stmt m){ arr_Stmt r=arr_Stmt_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_Stmt_push(r,m->values[i]); return r; }
static int64_t map_f64_Stmt_len(map_f64_Stmt m){ return m->len; }
static void map_f64_Stmt_print(map_f64_Stmt m){ (void)m; }
typedef struct { int tag; int64_t ok; const char* err; } res_i64;
static res_i64 mk_ok_i64(int64_t v){ res_i64 r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_i64 mk_err_i64(const char* m){ res_i64 r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; const char* ok; const char* err; } res_str;
static res_str mk_ok_str(const char* v){ res_str r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_str mk_err_str(const char* m){ res_str r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_Token ok; const char* err; } res_Token;
static res_Token mk_ok_Token(s_Token v){ res_Token r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_Token mk_err_Token(const char* m){ res_Token r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_StructDef ok; const char* err; } res_StructDef;
static res_StructDef mk_ok_StructDef(s_StructDef v){ res_StructDef r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_StructDef mk_err_StructDef(const char* m){ res_StructDef r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_EnumDef ok; const char* err; } res_EnumDef;
static res_EnumDef mk_ok_EnumDef(s_EnumDef v){ res_EnumDef r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_EnumDef mk_err_EnumDef(const char* m){ res_EnumDef r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_Func ok; const char* err; } res_Func;
static res_Func mk_ok_Func(s_Func v){ res_Func r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_Func mk_err_Func(const char* m){ res_Func r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_ClassDef ok; const char* err; } res_ClassDef;
static res_ClassDef mk_ok_ClassDef(s_ClassDef v){ res_ClassDef r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_ClassDef mk_err_ClassDef(const char* m){ res_ClassDef r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_P ok; const char* err; } res_P;
static res_P mk_ok_P(s_P v){ res_P r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_P mk_err_P(const char* m){ res_P r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_Binds ok; const char* err; } res_Binds;
static res_Binds mk_ok_Binds(s_Binds v){ res_Binds r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_Binds mk_err_Binds(const char* m){ res_Binds r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_Syms ok; const char* err; } res_Syms;
static res_Syms mk_ok_Syms(s_Syms v){ res_Syms r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_Syms mk_err_Syms(const char* m){ res_Syms r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_Expr ok; const char* err; } res_Expr;
static res_Expr mk_ok_Expr(s_Expr v){ res_Expr r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_Expr mk_err_Expr(const char* m){ res_Expr r; r.tag=1; r.err=m; return r; }
typedef struct { int tag; s_Stmt ok; const char* err; } res_Stmt;
static res_Stmt mk_ok_Stmt(s_Stmt v){ res_Stmt r; r.tag=0; r.ok=v; r.err=""; return r; }
static res_Stmt mk_err_Stmt(const char* m){ res_Stmt r; r.tag=1; r.err=m; return r; }
static s_Token mk_Token(int64_t kind, const char* text, int64_t pos){ s_Token __s; __s.kind=kind; __s.text=text; __s.pos=pos; return __s; }
static s_StructDef mk_StructDef(const char* name, arr_str fnames, arr_str ftypes){ s_StructDef __s; __s.name=name; __s.fnames=fnames; __s.ftypes=ftypes; return __s; }
static s_EnumDef mk_EnumDef(const char* name, arr_str vnames, arr_str vftypes){ s_EnumDef __s; __s.name=name; __s.vnames=vnames; __s.vftypes=vftypes; return __s; }
static s_Func mk_Func(const char* name, arr_str params, arr_str ptypes, const char* ret, const char* lib, arr_str tparams, arr_Stmt body){ s_Func __s; __s.name=name; __s.params=params; __s.ptypes=ptypes; __s.ret=ret; __s.lib=lib; __s.tparams=tparams; __s.body=body; return __s; }
static s_ClassDef mk_ClassDef(const char* name, const char* parent, arr_str fnames, arr_str ftypes, arr_Func methods, arr_str vflags){ s_ClassDef __s; __s.name=name; __s.parent=parent; __s.fnames=fnames; __s.ftypes=ftypes; __s.methods=methods; __s.vflags=vflags; return __s; }
static s_P mk_P(arr_Token toks, int64_t pos, const char* src){ s_P __s; __s.toks=toks; __s.pos=pos; __s.src=src; return __s; }
static s_Binds mk_Binds(arr_str names, arr_Expr vals){ s_Binds __s; __s.names=names; __s.vals=vals; return __s; }
static s_Syms mk_Syms(map_str_str vty, map_str_str fld, map_str_str ctors, map_str_str frets, map_str_str evar, map_str_str vft, arr_Func gfns, arr_Func lams){ s_Syms __s; __s.vty=vty; __s.fld=fld; __s.ctors=ctors; __s.frets=frets; __s.evar=evar; __s.vft=vft; __s.gfns=gfns; __s.lams=lams; return __s; }
static void print_Token(s_Token v){ printf("Token{"); printf("kind: "); printf("%lld", (long long)(v.kind)); printf(", "); printf("text: "); printf("%s", v.text); printf(", "); printf("pos: "); printf("%lld", (long long)(v.pos)); printf("}"); }
static void print_StructDef(s_StructDef v){ printf("StructDef{"); printf("name: "); printf("%s", v.name); printf(", "); printf("fnames: "); printf("?"); printf(", "); printf("ftypes: "); printf("?"); printf("}"); }
static void print_EnumDef(s_EnumDef v){ printf("EnumDef{"); printf("name: "); printf("%s", v.name); printf(", "); printf("vnames: "); printf("?"); printf(", "); printf("vftypes: "); printf("?"); printf("}"); }
static void print_Func(s_Func v){ printf("Func{"); printf("name: "); printf("%s", v.name); printf(", "); printf("params: "); printf("?"); printf(", "); printf("ptypes: "); printf("?"); printf(", "); printf("ret: "); printf("%s", v.ret); printf(", "); printf("lib: "); printf("%s", v.lib); printf(", "); printf("tparams: "); printf("?"); printf(", "); printf("body: "); printf("?"); printf("}"); }
static void print_ClassDef(s_ClassDef v){ printf("ClassDef{"); printf("name: "); printf("%s", v.name); printf(", "); printf("parent: "); printf("%s", v.parent); printf(", "); printf("fnames: "); printf("?"); printf(", "); printf("ftypes: "); printf("?"); printf(", "); printf("methods: "); printf("?"); printf(", "); printf("vflags: "); printf("?"); printf("}"); }
static void print_P(s_P v){ printf("P{"); printf("toks: "); printf("?"); printf(", "); printf("pos: "); printf("%lld", (long long)(v.pos)); printf(", "); printf("src: "); printf("%s", v.src); printf("}"); }
static void print_Binds(s_Binds v){ printf("Binds{"); printf("names: "); printf("?"); printf(", "); printf("vals: "); printf("?"); printf("}"); }
static void print_Syms(s_Syms v){ printf("Syms{"); printf("vty: "); printf("?"); printf(", "); printf("fld: "); printf("?"); printf(", "); printf("ctors: "); printf("?"); printf(", "); printf("frets: "); printf("?"); printf(", "); printf("evar: "); printf("?"); printf(", "); printf("vft: "); printf("?"); printf(", "); printf("gfns: "); printf("?"); printf(", "); printf("lams: "); printf("?"); printf("}"); }
static s_Expr mkv_Num(int64_t f0){ s_Expr v; v.tag=0; v.u.Num.f0=f0; return v; }
static s_Expr mkv_Flt(const char* f0){ s_Expr v; v.tag=1; v.u.Flt.f0=f0; return v; }
static s_Expr mkv_Str(const char* f0){ s_Expr v; v.tag=2; v.u.Str.f0=f0; return v; }
static s_Expr mkv_Var(const char* f0){ s_Expr v; v.tag=3; v.u.Var.f0=f0; return v; }
static s_Expr mkv_Bin(int64_t f0, s_Expr f1, s_Expr f2){ s_Expr v; v.tag=4; v.u.Bin.f0=f0; v.u.Bin.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Bin.f1=f1; v.u.Bin.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Bin.f2=f2; return v; }
static s_Expr mkv_Unary(int64_t f0, s_Expr f1){ s_Expr v; v.tag=5; v.u.Unary.f0=f0; v.u.Unary.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Unary.f1=f1; return v; }
static s_Expr mkv_Call(const char* f0, arr_Expr f1){ s_Expr v; v.tag=6; v.u.Call.f0=f0; v.u.Call.f1=f1; return v; }
static s_Expr mkv_Field(s_Expr f0, const char* f1){ s_Expr v; v.tag=7; v.u.Field.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Field.f0=f0; v.u.Field.f1=f1; return v; }
static s_Expr mkv_Index(s_Expr f0, s_Expr f1){ s_Expr v; v.tag=8; v.u.Index.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Index.f0=f0; v.u.Index.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Index.f1=f1; return v; }
static s_Expr mkv_Array(arr_Expr f0, const char* f1){ s_Expr v; v.tag=9; v.u.Array.f0=f0; v.u.Array.f1=f1; return v; }
static s_Expr mkv_MapLit(const char* f0, arr_Expr f1, arr_Expr f2){ s_Expr v; v.tag=10; v.u.MapLit.f0=f0; v.u.MapLit.f1=f1; v.u.MapLit.f2=f2; return v; }
static s_Expr mkv_Addr(s_Expr f0){ s_Expr v; v.tag=11; v.u.Addr.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Addr.f0=f0; return v; }
static s_Expr mkv_Match(s_Expr f0, arr_str f1, arr_str f2, arr_Expr f3){ s_Expr v; v.tag=12; v.u.Match.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Match.f0=f0; v.u.Match.f1=f1; v.u.Match.f2=f2; v.u.Match.f3=f3; return v; }
static s_Expr mkv_IfE(s_Expr f0, s_Expr f1, s_Expr f2){ s_Expr v; v.tag=13; v.u.IfE.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.IfE.f0=f0; v.u.IfE.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.IfE.f1=f1; v.u.IfE.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.IfE.f2=f2; return v; }
static s_Expr mkv_Try(s_Expr f0){ s_Expr v; v.tag=14; v.u.Try.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Try.f0=f0; return v; }
static s_Expr mkv_Lambda(arr_str f0, arr_str f1, s_Expr f2, int64_t f3){ s_Expr v; v.tag=15; v.u.Lambda.f0=f0; v.u.Lambda.f1=f1; v.u.Lambda.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.Lambda.f2=f2; v.u.Lambda.f3=f3; return v; }
static s_Expr mkv_Tuple(arr_Expr f0){ s_Expr v; v.tag=16; v.u.Tuple.f0=f0; return v; }
static s_Expr mkv_BlockE(arr_Stmt f0){ s_Expr v; v.tag=17; v.u.BlockE.f0=f0; return v; }
static s_Expr mkv_Bad(){ s_Expr v; v.tag=18; return v; }
static s_Stmt mkv_SDecl(const char* f0, s_Expr f1, int64_t f2){ s_Stmt v; v.tag=0; v.u.SDecl.f0=f0; v.u.SDecl.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SDecl.f1=f1; v.u.SDecl.f2=f2; return v; }
static s_Stmt mkv_SDestructure(arr_str f0, s_Expr f1){ s_Stmt v; v.tag=1; v.u.SDestructure.f0=f0; v.u.SDestructure.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SDestructure.f1=f1; return v; }
static s_Stmt mkv_SAssign(const char* f0, s_Expr f1, int64_t f2){ s_Stmt v; v.tag=2; v.u.SAssign.f0=f0; v.u.SAssign.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SAssign.f1=f1; v.u.SAssign.f2=f2; return v; }
static s_Stmt mkv_SIdxAssign(s_Expr f0, s_Expr f1, s_Expr f2, int64_t f3){ s_Stmt v; v.tag=3; v.u.SIdxAssign.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SIdxAssign.f0=f0; v.u.SIdxAssign.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SIdxAssign.f1=f1; v.u.SIdxAssign.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SIdxAssign.f2=f2; v.u.SIdxAssign.f3=f3; return v; }
static s_Stmt mkv_SFieldAssign(s_Expr f0, const char* f1, s_Expr f2, int64_t f3){ s_Stmt v; v.tag=4; v.u.SFieldAssign.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SFieldAssign.f0=f0; v.u.SFieldAssign.f1=f1; v.u.SFieldAssign.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SFieldAssign.f2=f2; v.u.SFieldAssign.f3=f3; return v; }
static s_Stmt mkv_SReturn(s_Expr f0, int64_t f1){ s_Stmt v; v.tag=5; v.u.SReturn.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SReturn.f0=f0; v.u.SReturn.f1=f1; return v; }
static s_Stmt mkv_SPrint(s_Expr f0, int64_t f1){ s_Stmt v; v.tag=6; v.u.SPrint.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SPrint.f0=f0; v.u.SPrint.f1=f1; return v; }
static s_Stmt mkv_SIf(s_Expr f0, arr_Stmt f1, arr_Stmt f2){ s_Stmt v; v.tag=7; v.u.SIf.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SIf.f0=f0; v.u.SIf.f1=f1; v.u.SIf.f2=f2; return v; }
static s_Stmt mkv_SLoop(s_Expr f0, arr_Stmt f1){ s_Stmt v; v.tag=8; v.u.SLoop.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SLoop.f0=f0; v.u.SLoop.f1=f1; return v; }
static s_Stmt mkv_SLoopIn(const char* f0, s_Expr f1, arr_Stmt f2){ s_Stmt v; v.tag=9; v.u.SLoopIn.f0=f0; v.u.SLoopIn.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SLoopIn.f1=f1; v.u.SLoopIn.f2=f2; return v; }
static s_Stmt mkv_SLoopKV(const char* f0, const char* f1, s_Expr f2, arr_Stmt f3){ s_Stmt v; v.tag=10; v.u.SLoopKV.f0=f0; v.u.SLoopKV.f1=f1; v.u.SLoopKV.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SLoopKV.f2=f2; v.u.SLoopKV.f3=f3; return v; }
static s_Stmt mkv_SLoopRange(const char* f0, s_Expr f1, s_Expr f2, arr_Stmt f3){ s_Stmt v; v.tag=11; v.u.SLoopRange.f0=f0; v.u.SLoopRange.f1=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SLoopRange.f1=f1; v.u.SLoopRange.f2=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SLoopRange.f2=f2; v.u.SLoopRange.f3=f3; return v; }
static s_Stmt mkv_SBreak(){ s_Stmt v; v.tag=12; return v; }
static s_Stmt mkv_SContinue(){ s_Stmt v; v.tag=13; return v; }
static s_Stmt mkv_SExpr(s_Expr f0, int64_t f1){ s_Stmt v; v.tag=14; v.u.SExpr.f0=(s_Expr*)GC_MALLOC(sizeof(s_Expr)); *v.u.SExpr.f0=f0; v.u.SExpr.f1=f1; return v; }

#undef system
extern int system(const char*);
static int64_t f_system(const char* a0){ return (int64_t)system(a0); }

int64_t f_TK_EOF(void);
int64_t f_TK_INT(void);
int64_t f_TK_IDENT(void);
int64_t f_TK_PLUS(void);
int64_t f_TK_MINUS(void);
int64_t f_TK_STAR(void);
int64_t f_TK_SLASH(void);
int64_t f_TK_LPAREN(void);
int64_t f_TK_RPAREN(void);
int64_t f_TK_LBRACE(void);
int64_t f_TK_RBRACE(void);
int64_t f_TK_COMMA(void);
int64_t f_TK_WALRUS(void);
int64_t f_TK_LT(void);
int64_t f_TK_GT(void);
int64_t f_TK_LE(void);
int64_t f_TK_GE(void);
int64_t f_TK_EQ(void);
int64_t f_TK_NE(void);
int64_t f_TK_ASSIGN(void);
int64_t f_TK_AND(void);
int64_t f_TK_OR(void);
int64_t f_TK_NOT(void);
int64_t f_TK_STR(void);
int64_t f_TK_PERCENT(void);
int64_t f_TK_DOT(void);
int64_t f_TK_SEMI(void);
int64_t f_TK_COLON(void);
int64_t f_TK_LBRACK(void);
int64_t f_TK_RBRACK(void);
int64_t f_TK_AMP(void);
int64_t f_TK_QUEST(void);
int64_t f_TK_FLOAT(void);
int64_t f_TK_DOTDOT(void);
int64_t f_TK_LTLT(void);
int64_t f_TK_GTGT(void);
int64_t f_TK_CARET(void);
int64_t f_TK_PIPE(void);
int64_t f_TK_PIPEGT(void);
int64_t f_TK_CHAR(void);
int64_t f_TK_CONCAT(void);
int64_t f_TK_QQ(void);
int64_t f_TK_RAWSTR(void);
int64_t f_TK_ERR(void);
int64_t f_is_digit(int64_t v_c);
int64_t f_is_alpha(int64_t v_c);
int64_t f_is_space(int64_t v_c);
arr_Token f_lex(const char* v_src);
int64_t f_OP_ADD(void);
int64_t f_OP_SUB(void);
int64_t f_OP_MUL(void);
int64_t f_OP_DIV(void);
int64_t f_OP_MOD(void);
int64_t f_OP_LT(void);
int64_t f_OP_GT(void);
int64_t f_OP_LE(void);
int64_t f_OP_GE(void);
int64_t f_OP_EQ(void);
int64_t f_OP_NE(void);
int64_t f_OP_AND(void);
int64_t f_OP_OR(void);
int64_t f_OP_NOT(void);
int64_t f_OP_SHL(void);
int64_t f_OP_SHR(void);
int64_t f_OP_BAND(void);
int64_t f_OP_BXOR(void);
int64_t f_OP_BOR(void);
int64_t f_OP_CAT(void);
int64_t f_OP_QQ(void);
s_Token f_cur(s_P* v_p);
int64_t f_ckind(s_P* v_p);
const char* f_ctext(s_P* v_p);
void f_adv(s_P* v_p);
const char* f_tok_name(int64_t v_k);
void f_report_at(const char* v_src, int64_t v_pos, const char* v_msg);
void f_eat(s_P* v_p, int64_t v_k);
int64_t f_cmp_op(int64_t v_k);
s_Expr f_parse_expr(s_P* v_p);
s_Expr f_parse_coalesce(s_P* v_p);
arr_Expr f_prepend_arg(s_Expr v_x, arr_Expr v_xs);
s_Expr f_pipe_call(s_Expr v_lhs, s_Expr v_rhs);
s_Expr f_parse_pipe(s_P* v_p);
s_Expr f_parse_or(s_P* v_p);
s_Expr f_parse_and(s_P* v_p);
s_Expr f_parse_bitor(s_P* v_p);
s_Expr f_parse_bitxor(s_P* v_p);
s_Expr f_parse_bitand(s_P* v_p);
s_Expr f_parse_cmp(s_P* v_p);
s_Expr f_parse_shift(s_P* v_p);
s_Expr f_parse_add(s_P* v_p);
s_Expr f_parse_mul(s_P* v_p);
s_Expr f_parse_unary(s_P* v_p);
s_Expr f_parse_factor(s_P* v_p);
s_Expr f_parse_brace_expr(s_P* v_p);
int64_t f_str_has_interp(const char* v_s);
s_Expr f_parse_interp(const char* v_s);
int64_t f_char_code(const char* v_s);
int64_t f_is_bad(s_Expr v_e);
int64_t f_is_lower_ident(const char* v_s);
s_Expr f_subst_var_hit(const char* v_n, const char* v_nm, s_Expr v_repl, s_Expr v_orig);
arr_Expr f_subst_args(arr_Expr v_es, const char* v_nm, s_Expr v_repl);
s_Expr f_subst_var(s_Expr v_e, const char* v_nm, s_Expr v_repl);
s_Expr f_parse_arm_cond(s_P* v_p, arr_Expr v_scruts, int64_t v_is_tuple, s_Binds* v_bnd);
s_Expr f_build_if_chain(arr_Expr v_conds, arr_Expr v_bodies);
s_Expr f_parse_match_desugar(s_P* v_p, arr_Expr v_scruts, int64_t v_is_tuple);
arr_str f_struct_field_names(arr_Token v_toks, const char* v_name);
int64_t f_brace_is_block(s_P* v_p);
s_Expr f_parse_primary(s_P* v_p);
arr_Stmt f_parse_block(s_P* v_p);
int64_t f_is_destructure_ahead(s_P* v_p);
s_Stmt f_parse_destructure(s_P* v_p);
s_Stmt f_parse_stmt(s_P* v_p);
int64_t f_compound_op(int64_t v_k);
int64_t f_is_lvalue_assign(s_P* v_p);
s_Stmt f_mk_lvalue_assign(s_Expr v_lhs, s_Expr v_rhs, int64_t v_spos);
s_Expr f_parse_decl_rhs(s_P* v_p, const char* v_ann);
s_Expr f_stamp_array_ann(s_Expr v_e, const char* v_ety);
s_Expr f_stamp_map_ann(s_Expr v_e, const char* v_mty);
s_Expr f_stamp_if_empty(arr_Expr v_elems, const char* v_old, const char* v_ety);
s_StructDef f_parse_struct(s_P* v_p);
s_Func f_parse_method(s_P* v_p, const char* v_cls);
s_ClassDef f_parse_class(s_P* v_p);
s_EnumDef f_parse_enum(s_P* v_p);
int64_t f_is_boxed_ft(const char* v_ft);
const char* f_boxed_enum(const char* v_ft, const char* v_selfname);
int64_t f_is_enum_name(arr_EnumDef v_enums, const char* v_nm);
int64_t f_is_struct_name(arr_StructDef v_structs, const char* v_nm);
int64_t f_is_prim_name(const char* v_nm);
const char* f_mark_ctype(const char* v_ty, arr_StructDef v_structs, arr_EnumDef v_enums);
arr_Func f_mark_extern_ctypes(arr_Func v_externs, arr_StructDef v_structs, arr_EnumDef v_enums);
arr_EnumDef f_box_cross_enums(arr_EnumDef v_enums);
const char* f_parse_type(s_P* v_p);
int64_t f_is_array_ann(const char* v_ty);
const char* f_elem_of_ann(const char* v_ty);
int64_t f_is_map_ann(const char* v_ty);
int64_t f_is_ptr_ann(const char* v_ty);
const char* f_deref_ann(const char* v_ty);
int64_t f_is_result_ann(const char* v_ty);
const char* f_result_inner(const char* v_ty);
int64_t f_is_fn_ann(const char* v_ty);
const char* f_fn_ret_of(const char* v_sig);
const char* f_fn_type_of(int64_t v_np, const char* v_ret);
const char* f_lambda_type(s_Syms* v_sy, arr_str v_ps, arr_str v_pts, s_Expr v_b);
const char* f_fn_type_of_pts(arr_str v_pts, const char* v_ret);
arr_str f_fn_params_of(const char* v_sig);
const char* f_under_ptr(const char* v_ty);
int64_t f_is_tuple_ann(const char* v_ty);
arr_str f_tuple_elems(const char* v_ty);
const char* f_tuple_suffix(const char* v_ty);
s_Func f_parse_func(s_P* v_p);
s_Func f_parse_extern(s_P* v_p);
const char* f_cty_proto(const char* v_ty);
const char* f_gen_extern(s_Func v_f);
const char* f_op_c(int64_t v_op);
const char* f_ty_of(s_Syms* v_sy, const char* v_name);
int64_t f_declared(s_Syms* v_sy, const char* v_name);
void f_set_ty(s_Syms* v_sy, const char* v_name, const char* v_ty);
int64_t f_is_ctor(s_Syms* v_sy, const char* v_name);
int64_t f_is_variant(s_Syms* v_sy, const char* v_name);
const char* f_fkey(const char* v_sname, const char* v_fname);
const char* f_arr_suffix(const char* v_ety);
int64_t f_is_c_type_ann(const char* v_ty);
const char* f_c_type_name(const char* v_ty);
const char* f_cty(const char* v_ty);
const char* f_map_vtype(const char* v_ty);
const char* f_map_ktype(const char* v_ty);
const char* f_map_cty(const char* v_ty);
const char* f_type_of_expr(s_Syms* v_sy, s_Expr v_e);
const char* f_match_type(s_Syms* v_sy, arr_Expr v_bodies);
const char* f_index_type(s_Syms* v_sy, s_Expr v_obj);
const char* f_array_type_of(s_Syms* v_sy, arr_Expr v_elems, const char* v_ety);
const char* f_map_lit_type(const char* v_mty);
const char* f_bin_type(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r);
int64_t f_is_native_call(const char* v_fname);
const char* f_call_type_a(s_Syms* v_sy, const char* v_fname, arr_Expr v_args);
const char* f_field_type(s_Syms* v_sy, s_Expr v_obj, const char* v_fname);
int64_t f_expr_is_str(s_Expr v_e, s_Syms* v_sy);
int64_t f_confident(const char* v_t);
int64_t f_is_num(const char* v_t);
const char* f_tcon_var(s_Syms* v_sy, const char* v_name);
const char* f_builtin_fixed_ret(const char* v_fname);
const char* f_tcon_call(s_Syms* v_sy, const char* v_fname, arr_Expr v_args);
const char* f_tcon_field(s_Syms* v_sy, s_Expr v_obj, const char* v_fname);
const char* f_tcon_index(s_Syms* v_sy, s_Expr v_obj);
const char* f_tcon_bin(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r);
const char* f_tcon_addr(s_Syms* v_sy, s_Expr v_x);
const char* f_type_confident(s_Syms* v_sy, s_Expr v_e);
const char* f_ty_cat(const char* v_t);
int64_t f_is_ancestor(s_Syms* v_sy, const char* v_anc, const char* v_desc);
int64_t f_cat_incompatible(const char* v_a, const char* v_b);
int64_t f_incompatible(s_Syms* v_sy, const char* v_a, const char* v_b);
const char* f_gen_args(s_Syms* v_sy, arr_Expr v_args);
const char* f_gen_str(const char* v_s);
const char* f_gen_var(s_Syms* v_sy, const char* v_name);
const char* f_gen_expr(s_Syms* v_sy, s_Expr v_e);
const char* f_gen_ife(s_Syms* v_sy, s_Expr v_c, s_Expr v_t, s_Expr v_el2);
const char* f_gen_try(s_Syms* v_sy, s_Expr v_e);
const char* f_gen_match(s_Syms* v_sy, s_Expr v_sc, arr_str v_vnames, arr_str v_vbinds, arr_Expr v_bodies);
const char* f_gen_arm_binds(s_Syms* v_sy, const char* v_vname, arr_str v_binds);
void f_bind_arm_types(s_Syms* v_sy, const char* v_vname, arr_str v_binds);
int64_t f_variant_tag(s_Syms* v_sy, const char* v_sty, const char* v_vname);
arr_str f_split_semi(const char* v_s);
const char* f_gen_field(s_Syms* v_sy, s_Expr v_obj, const char* v_fnm);
const char* f_gen_index(s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx);
const char* f_gen_array(s_Syms* v_sy, arr_Expr v_elems, const char* v_ety);
const char* f_maplit_type(s_Syms* v_sy, const char* v_mty, arr_Expr v_keys, arr_Expr v_vals);
const char* f_gen_maplit(s_Syms* v_sy, const char* v_mty, arr_Expr v_keys, arr_Expr v_vals);
arr_str f_empty_strs(void);
arr_Expr f_no_exprs(void);
arr_str f_lam_params(s_Expr v_e);
s_Expr f_lam_body(s_Expr v_e);
int64_t f_is_param(arr_str v_ps, const char* v_nm);
arr_str f_fv_add(arr_str v_acc, const char* v_name);
arr_str f_fv_args(arr_Expr v_args, arr_str v_acc);
arr_str f_fv_walk(s_Expr v_e, arr_str v_acc);
arr_str f_fv_body(arr_Stmt v_body, arr_str v_acc);
arr_str f_fv_stmt(s_Stmt v_s, arr_str v_acc);
arr_str f_free_vars(s_Expr v_b);
const char* f_gen_map(s_Syms* v_sy, s_Expr v_arr, s_Expr v_lam);
const char* f_gen_filter(s_Syms* v_sy, s_Expr v_arr, s_Expr v_lam);
const char* f_gen_reduce(s_Syms* v_sy, s_Expr v_arr, s_Expr v_init, s_Expr v_lam);
int64_t f_is_generic(s_Syms* v_sy, const char* v_name);
int64_t f_is_tparam(s_Func v_f, const char* v_ty);
int64_t f_is_array_tparam(s_Func v_f, const char* v_ty);
const char* f_subst_tparam(s_Func v_f, const char* v_ty, const char* v_concrete);
s_Func f_find_gfn(s_Syms* v_sy, const char* v_name);
const char* f_generic_T(s_Syms* v_sy, s_Func v_f, arr_Expr v_args);
const char* f_inline_stmt_value(s_Syms* v_sy, s_Stmt v_s);
const char* f_gen_generic_call(s_Syms* v_sy, const char* v_name, arr_Expr v_args);
const char* f_gen_lambda(s_Syms* v_sy, int64_t v_id);
const char* f_gen_closure_call(s_Syms* v_sy, const char* v_fname, const char* v_ret, arr_Expr v_args);
const char* f_class_parent(s_Syms* v_sy, const char* v_cls);
int64_t f_is_class_method(s_Syms* v_sy, const char* v_cls, const char* v_m);
const char* f_defining_class(s_Syms* v_sy, const char* v_cls, const char* v_m);
const char* f_recv_ptr(s_Syms* v_sy, s_Expr v_e);
const char* f_gen_method_call(s_Syms* v_sy, const char* v_cls, const char* v_m, arr_Expr v_args);
const char* f_vsig_ret(s_Syms* v_sy, const char* v_cls, const char* v_m);
const char* f_vsig_args(s_Syms* v_sy, const char* v_cls, const char* v_m);
const char* f_gen_vtable_typedef(s_Syms* v_sy, const char* v_cls);
const char* f_gen_vtable_instance(s_Syms* v_sy, const char* v_cls);
const char* f_gen_call(s_Syms* v_sy, const char* v_fname, arr_Expr v_args);
const char* f_gen_qq_index(s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, s_Expr v_r);
const char* f_gen_qq(s_Syms* v_sy, s_Expr v_l, s_Expr v_r);
const char* f_gen_bin(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r);
const char* f_gen_decl(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_ind);
const char* f_tuple_type_of(s_Syms* v_sy, arr_Expr v_elems);
const char* f_gen_tuple_lit(s_Syms* v_sy, arr_Expr v_elems);
const char* f_gen_destructure(s_Syms* v_sy, arr_str v_names, s_Expr v_e, const char* v_ind);
const char* f_hoist_destructure(s_Syms* v_sy, arr_str v_names, s_Expr v_e, const char* v_ind);
const char* f_gen_tuple_typedef(const char* v_ty);
const char* f_block_type(s_Syms* v_sy, arr_Stmt v_body);
const char* f_gen_blk_decl(s_Syms* v_sy, const char* v_name, s_Expr v_e);
const char* f_gen_blk_stmt(s_Syms* v_sy, s_Stmt v_s);
const char* f_gen_blk_tail(s_Syms* v_sy, s_Stmt v_s);
const char* f_gen_block_e(s_Syms* v_sy, arr_Stmt v_body);
const char* f_gen_if(s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb, const char* v_ind);
const char* f_gen_stmts(s_Syms* v_sy, arr_Stmt v_body, const char* v_ind);
const char* f_hoist_decls(s_Syms* v_sy, arr_Stmt v_body, const char* v_ind);
const char* f_hoist_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_ind);
const char* f_hoist_one(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_ind);
const char* f_hoist_loopin(s_Syms* v_sy, const char* v_vnm, s_Expr v_coll, arr_Stmt v_b, const char* v_ind);
const char* f_hoist_loopkv(s_Syms* v_sy, const char* v_kn, const char* v_vn, s_Expr v_coll, arr_Stmt v_b, const char* v_ind);
const char* f_gen_fn_body(s_Syms* v_sy, arr_Stmt v_body, const char* v_ret, const char* v_ind);
const char* f_gen_tail_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_ret, const char* v_ind);
const char* f_gen_tail_if(s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb, const char* v_ret, const char* v_ind);
const char* f_gen_print(s_Syms* v_sy, s_Expr v_e, const char* v_ind);
const char* f_gen_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_ind);
const char* f_gen_assign(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_ind);
const char* f_gen_idx_assign(s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, s_Expr v_e, const char* v_ind);
const char* f_coll_elem_type(s_Syms* v_sy, s_Expr v_coll);
const char* f_gen_loop_in(s_Syms* v_sy, const char* v_vnm, s_Expr v_coll, arr_Stmt v_body, const char* v_ind);
const char* f_gen_loop_range(s_Syms* v_sy, const char* v_vnm, s_Expr v_lo, s_Expr v_hi, arr_Stmt v_body, const char* v_ind);
const char* f_hoist_range(s_Syms* v_sy, const char* v_vnm, arr_Stmt v_b, const char* v_ind);
int64_t f_collect_range_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_lo, s_Expr v_hi, arr_Stmt v_b);
const char* f_gen_loop_kv(s_Syms* v_sy, const char* v_kn, const char* v_vn, s_Expr v_coll, arr_Stmt v_body, const char* v_ind);
const char* f_cty_ret(const char* v_ret);
const char* f_gen_sig(s_Func v_f, const char* v_ret);
const char* f_gen_lam_sig(s_Func v_lf);
const char* f_gen_env_struct(s_Syms* v_base, s_Func v_lf);
const char* f_gen_lifted_body(s_Syms* v_base, s_Func v_lf);
const char* f_gen_arr_typedef(const char* v_suf, const char* v_et);
const char* f_gen_arr_helpers(const char* v_suf, const char* v_et);
const char* f_pf_spec(const char* v_t);
const char* f_pf_arg(const char* v_t, const char* v_slot);
int64_t f_is_scalar_ty(const char* v_t);
const char* f_gen_map_print(const char* v_nm, const char* v_kt, const char* v_vt);
const char* f_gen_map_fwd(const char* v_nm);
const char* f_gen_map_node(const char* v_nm, const char* v_kt, const char* v_vt);
const char* f_gen_map_helpers(const char* v_nm, const char* v_kt, const char* v_vt, const char* v_dflt);
const char* f_map_dflt(const char* v_vt);
const char* f_map_nm(const char* v_kt, const char* v_vt);
const char* f_gen_map_fwds_all(arr_str v_keys, arr_str v_vals);
const char* f_gen_map_nodes_all(arr_str v_keys, arr_str v_vals);
const char* f_gen_map_helpers_all(arr_str v_keys, arr_str v_vals);
const char* f_gen_res(const char* v_suf, const char* v_et);
const char* f_gen_struct_fwd(s_StructDef v_sd);
const char* f_gen_struct_body(s_StructDef v_sd);
const char* f_gen_struct_printer(s_StructDef v_sd);
const char* f_gen_struct_ctor(s_StructDef v_sd);
const char* f_gen_enum_fwd(s_EnumDef v_ed);
const char* f_gen_enum_body(s_EnumDef v_ed);
const char* f_gen_enum_ctors(s_EnumDef v_ed);
void f_seed_params(s_Syms* v_sy, s_Func v_f);
s_Syms f_seed_fn(s_Syms* v_base, s_Func v_f);
int64_t f_register_lam(s_Syms* v_base, s_Syms* v_sy, arr_str v_ps, arr_str v_pts, s_Expr v_b, int64_t v_id);
int64_t f_collect2(s_Syms* v_base, s_Syms* v_sy, s_Expr v_a, s_Expr v_b);
int64_t f_collect3(s_Syms* v_base, s_Syms* v_sy, s_Expr v_a, s_Expr v_b, s_Expr v_c);
int64_t f_collect_args(s_Syms* v_base, s_Syms* v_sy, arr_Expr v_args);
int64_t f_collect_kv(s_Syms* v_base, s_Syms* v_sy, arr_Expr v_keys, arr_Expr v_vals);
int64_t f_collect_match_e(s_Syms* v_base, s_Syms* v_sy, s_Expr v_sc, arr_Expr v_bodies);
int64_t f_collect_if_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb);
int64_t f_collect_body_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b);
int64_t f_collect_coll_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_coll, arr_Stmt v_b);
int64_t f_collect_call(s_Syms* v_base, s_Syms* v_sy, const char* v_fname, arr_Expr v_args);
int64_t f_collect_lams_expr(s_Syms* v_base, s_Syms* v_sy, s_Expr v_e);
int64_t f_collect_lams_stmt(s_Syms* v_base, s_Syms* v_sy, s_Stmt v_s);
int64_t f_collect_lams(s_Syms* v_base, s_Syms* v_sy, arr_Stmt v_body);
const char* f_infer_ret(s_Func v_f, s_Syms v_base);
const char* f_first_return_type(s_Syms* v_sy, arr_Stmt v_body);
int64_t f_expr_is_cmp(s_Expr v_e);
int64_t f_expr_is_call(s_Expr v_e);
const char* f_call_fname(s_Expr v_e);
const char* f_tail_sexpr_type(s_Syms* v_sy, s_Expr v_e);
const char* f_last_expr_type(s_Syms* v_sy, s_Stmt v_s);
const char* f_stmt_return_type(s_Syms* v_sy, s_Stmt v_s);
const char* f_decl_side(s_Syms* v_sy, const char* v_name, s_Expr v_e);
const char* f_either_return(s_Syms* v_sy, arr_Stmt v_b, arr_Stmt v_eb);
int64_t f_has_sub(const char* v_s, const char* v_sub);
const char* f_var_name(s_Expr v_e);
int64_t f_is_empty_maplit(s_Expr v_e);
const char* f_map_assign_type(s_Syms* v_sy, arr_Stmt v_body, const char* v_m);
const char* f_scan_loopin(s_Syms* v_sy, const char* v_vnm, s_Expr v_coll, arr_Stmt v_b, const char* v_m);
const char* f_scan_loopkv(s_Syms* v_sy, const char* v_kn, const char* v_vn, s_Expr v_coll, arr_Stmt v_b, const char* v_m);
const char* f_scan_range(s_Syms* v_sy, const char* v_v, arr_Stmt v_b, const char* v_m);
const char* f_scan_either(s_Syms* v_sy, arr_Stmt v_b, arr_Stmt v_eb, const char* v_m);
const char* f_idx_assign_type(s_Syms* v_sy, s_Expr v_o, s_Expr v_idx, s_Expr v_e, const char* v_m);
const char* f_map_assign_type_s(s_Syms* v_sy, s_Stmt v_s, const char* v_m);
int64_t f_is_empty_array(s_Expr v_e);
const char* f_push_args_elem(s_Syms* v_sy, const char* v_fname, arr_Expr v_args);
const char* f_push_call_elem(s_Syms* v_sy, s_Expr v_e);
const char* f_same_name_push(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_a);
const char* f_push_assign_elem(s_Syms* v_sy, s_Stmt v_s, const char* v_a);
const char* f_array_elem_from_push(s_Syms* v_sy, arr_Stmt v_body, const char* v_a);
s_Stmt f_stamp_decl_e(s_Syms* v_sy, arr_Stmt v_body, const char* v_name, s_Expr v_e, int64_t v_pos);
s_Stmt f_stamp_decl(s_Syms* v_sy, arr_Stmt v_body, s_Stmt v_s);
int64_t f_seed_one(s_Syms* v_sy, const char* v_name, s_Expr v_e);
int64_t f_seed_decl_s(s_Syms* v_sy, s_Stmt v_s);
arr_Stmt f_stamp_empty_maps(s_Syms* v_sy, arr_Stmt v_body);
int64_t f_slot_has(const char* v_slots, const char* v_m);
int64_t f_find_func_i(arr_Func v_funcs, const char* v_name);
int64_t f_bad_in_arith(const char* v_t);
int64_t f_arith_or_bit_nonadd(int64_t v_op);
int64_t f_is_cmp_op(int64_t v_op);
int64_t f_binop_check(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r, int64_t v_pos, const char* v_src);
int64_t f_callarg_check(arr_Func v_funcs, s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_field_check(s_Syms* v_sy, s_Expr v_obj, const char* v_fname, int64_t v_pos, const char* v_src);
int64_t f_index_check(s_Syms* v_sy, s_Expr v_obj, int64_t v_pos, const char* v_src);
int64_t f_chk2(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_a, s_Expr v_b, int64_t v_pos, const char* v_src);
int64_t f_chk3(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_a, s_Expr v_b, s_Expr v_c, int64_t v_pos, const char* v_src);
int64_t f_chk_args(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_chk_kv(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_ks, arr_Expr v_vs, int64_t v_pos, const char* v_src);
int64_t f_chk_try(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_x, int64_t v_pos, const char* v_src);
int64_t f_homog_check(s_Syms* v_sy, arr_Expr v_elems, const char* v_msg, int64_t v_pos, const char* v_src);
int64_t f_chk_array(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_elems, int64_t v_pos, const char* v_src);
int64_t f_chk_maplit(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_ks, arr_Expr v_vs, int64_t v_pos, const char* v_src);
int64_t f_vn_has(arr_str v_vnames, const char* v_v);
int64_t f_match_check(s_Syms* v_sy, s_Expr v_scrut, arr_str v_vnames, arr_str v_vbinds, int64_t v_pos, const char* v_src);
int64_t f_chk_match(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_sc, arr_str v_vnames, arr_str v_vbinds, arr_Expr v_bd, int64_t v_pos, const char* v_src);
int64_t f_chk_bin(arr_Func v_funcs, s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r, int64_t v_pos, const char* v_src);
int64_t f_lambda_arity(s_Expr v_e);
int64_t f_hof_arity_check(const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_generic_arity_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_ok_ret_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_chk_call(arr_Func v_funcs, s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_chk_field(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, const char* v_fname, int64_t v_pos, const char* v_src);
int64_t f_chk_index(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, int64_t v_pos, const char* v_src);
int64_t f_check_expr(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_e, int64_t v_pos, const char* v_src);
int64_t f_chk_assign(arr_Func v_funcs, s_Syms* v_sy, const char* v_name, s_Expr v_e, int64_t v_pos, const char* v_src);
int64_t f_chk_return(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_e, int64_t v_pos, const char* v_src);
int64_t f_chk_field_assign(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, const char* v_fnm, s_Expr v_e, int64_t v_pos, const char* v_src);
const char* f_idx_slot_type(s_Syms* v_sy, s_Expr v_obj);
int64_t f_chk_idx_assign(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, s_Expr v_e, int64_t v_pos, const char* v_src);
int64_t f_ctor_arg_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_variant_arg_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src);
int64_t f_check_tail_expr(s_Syms* v_sy, s_Expr v_e, int64_t v_pos, const char* v_want, const char* v_src);
int64_t f_check_tail_if(s_Syms* v_sy, arr_Stmt v_b, arr_Stmt v_eb, const char* v_want, const char* v_src);
int64_t f_check_tail_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_want, const char* v_src);
int64_t f_check_tail_return(s_Syms* v_sy, arr_Stmt v_body, const char* v_want, const char* v_src);
int64_t f_chk_if(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb, const char* v_src);
int64_t f_chk_body(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, const char* v_src);
int64_t f_chk_coll(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_coll, arr_Stmt v_b, const char* v_src);
int64_t f_chk_range(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_lo, s_Expr v_hi, arr_Stmt v_b, const char* v_src);
int64_t f_check_stmt(arr_Func v_funcs, s_Syms* v_sy, s_Stmt v_s, const char* v_src);
int64_t f_check_stmts(arr_Func v_funcs, s_Syms* v_sy, arr_Stmt v_body, const char* v_src);
int64_t f_check_fn(arr_Func v_funcs, s_Syms* v_base, s_Func v_f, const char* v_src);
int64_t f_check_program(arr_Func v_funcs, s_Syms* v_base, arr_Stmt v_mains, const char* v_src);
const char* f_compile_to_c(const char* v_src, const char* v_dir);
const char* f_link_flags(const char* v_cprog);
arr_str f_csrc_list(const char* v_cprog);
const char* f_dirname(const char* v_path);
const char* f_import_path(const char* v_line);
int64_t f_has_import(const char* v_src);
int64_t f_is_math_export(const char* v_nm);
const char* f_auto_imports(const char* v_src);
const char* f_resolve_imports(const char* v_src, const char* v_dir, map_str_str v_seen);

int64_t f_TK_EOF(void) {
    return 0;
}

int64_t f_TK_INT(void) {
    return 1;
}

int64_t f_TK_IDENT(void) {
    return 2;
}

int64_t f_TK_PLUS(void) {
    return 3;
}

int64_t f_TK_MINUS(void) {
    return 4;
}

int64_t f_TK_STAR(void) {
    return 5;
}

int64_t f_TK_SLASH(void) {
    return 6;
}

int64_t f_TK_LPAREN(void) {
    return 7;
}

int64_t f_TK_RPAREN(void) {
    return 8;
}

int64_t f_TK_LBRACE(void) {
    return 9;
}

int64_t f_TK_RBRACE(void) {
    return 10;
}

int64_t f_TK_COMMA(void) {
    return 11;
}

int64_t f_TK_WALRUS(void) {
    return 12;
}

int64_t f_TK_LT(void) {
    return 13;
}

int64_t f_TK_GT(void) {
    return 14;
}

int64_t f_TK_LE(void) {
    return 15;
}

int64_t f_TK_GE(void) {
    return 16;
}

int64_t f_TK_EQ(void) {
    return 17;
}

int64_t f_TK_NE(void) {
    return 18;
}

int64_t f_TK_ASSIGN(void) {
    return 19;
}

int64_t f_TK_AND(void) {
    return 20;
}

int64_t f_TK_OR(void) {
    return 21;
}

int64_t f_TK_NOT(void) {
    return 22;
}

int64_t f_TK_STR(void) {
    return 23;
}

int64_t f_TK_PERCENT(void) {
    return 24;
}

int64_t f_TK_DOT(void) {
    return 25;
}

int64_t f_TK_SEMI(void) {
    return 26;
}

int64_t f_TK_COLON(void) {
    return 27;
}

int64_t f_TK_LBRACK(void) {
    return 28;
}

int64_t f_TK_RBRACK(void) {
    return 29;
}

int64_t f_TK_AMP(void) {
    return 30;
}

int64_t f_TK_QUEST(void) {
    return 32;
}

int64_t f_TK_FLOAT(void) {
    return 33;
}

int64_t f_TK_DOTDOT(void) {
    return 34;
}

int64_t f_TK_LTLT(void) {
    return 35;
}

int64_t f_TK_GTGT(void) {
    return 36;
}

int64_t f_TK_CARET(void) {
    return 37;
}

int64_t f_TK_PIPE(void) {
    return 38;
}

int64_t f_TK_PIPEGT(void) {
    return 39;
}

int64_t f_TK_CHAR(void) {
    return 40;
}

int64_t f_TK_CONCAT(void) {
    return 41;
}

int64_t f_TK_QQ(void) {
    return 42;
}

int64_t f_TK_RAWSTR(void) {
    return 43;
}

int64_t f_TK_ERR(void) {
    return 31;
}

int64_t f_is_digit(int64_t v_c) {
    return ((v_c >= 48) && (v_c <= 57));
}

int64_t f_is_alpha(int64_t v_c) {
    return ((((v_c >= 97) && (v_c <= 122)) || ((v_c >= 65) && (v_c <= 90))) || (v_c == 95));
}

int64_t f_is_space(int64_t v_c) {
    return ((((v_c == 32) || (v_c == 9)) || (v_c == 10)) || (v_c == 13));
}

arr_Token f_lex(const char* v_src) {
    arr_Token v_toks;
    int64_t v_i;
    int64_t v_c;
    int64_t v_j;
    int64_t v_d;
    int64_t v_dd;
    int64_t v_k;
    v_toks = ({ arr_Token __a = arr_Token_new(); __a; });
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_src)))) {
        v_c = ((int64_t)(unsigned char)(v_src)[v_i]);
        if (f_is_space(v_c)) {
            v_i = (v_i + 1);
            continue;
        }
        if ((((v_c == 47) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 47))) {
            while ((v_i < ((int64_t)strlen(v_src)))) {
                if ((((int64_t)(unsigned char)(v_src)[v_i]) == 10)) {
                    break;
                }
                v_i = (v_i + 1);
            }
            continue;
        }
        if ((((v_c == 47) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 42))) {
            v_i = (v_i + 2);
            while (((v_i + 1) < ((int64_t)strlen(v_src)))) {
                if (((((int64_t)(unsigned char)(v_src)[v_i]) == 42) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 47))) {
                    break;
                }
                v_i = (v_i + 1);
            }
            v_i = (v_i + 2);
            continue;
        }
        if ((v_c == 34)) {
            v_j = (v_i + 1);
            while ((v_j < ((int64_t)strlen(v_src)))) {
                v_d = ((int64_t)(unsigned char)(v_src)[v_j]);
                if ((v_d == 92)) {
                    v_j = (v_j + 2);
                    continue;
                }
                if ((v_d == 34)) {
                    break;
                }
                v_j = (v_j + 1);
            }
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_STR(), substr(v_src, (v_i + 1), v_j), v_i));
            v_i = (v_j + 1);
            continue;
        }
        if ((v_c == 96)) {
            v_j = (v_i + 1);
            while ((v_j < ((int64_t)strlen(v_src)))) {
                if ((((int64_t)(unsigned char)(v_src)[v_j]) == 96)) {
                    break;
                }
                v_j = (v_j + 1);
            }
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_RAWSTR(), substr(v_src, (v_i + 1), v_j), v_i));
            v_i = (v_j + 1);
            continue;
        }
        if ((v_c == 39)) {
            v_j = (v_i + 1);
            while ((v_j < ((int64_t)strlen(v_src)))) {
                v_d = ((int64_t)(unsigned char)(v_src)[v_j]);
                if ((v_d == 92)) {
                    v_j = (v_j + 2);
                    continue;
                }
                if ((v_d == 39)) {
                    break;
                }
                v_j = (v_j + 1);
            }
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_CHAR(), substr(v_src, (v_i + 1), v_j), v_i));
            v_i = (v_j + 1);
            continue;
        }
        if ((((v_c == 58) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 61))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_WALRUS(), ":=", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 60) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 61))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_LE(), "<=", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 62) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 61))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_GE(), ">=", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 61) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 61))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_EQ(), "==", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 33) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 61))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_NE(), "!=", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 38) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 38))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_AND(), "&&", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 124) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 124))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_OR(), "||", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 46) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 46))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_DOTDOT(), "..", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 60) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 60))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_LTLT(), "<<", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 62) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 62))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_GTGT(), ">>", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 124) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 62))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_PIPEGT(), "|>", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 43) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 43))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_CONCAT(), "++", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if ((((v_c == 63) && ((v_i + 1) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 63))) {
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_QQ(), "??", v_i));
            v_i = (v_i + 2);
            continue;
        }
        if (f_is_digit(v_c)) {
            v_j = v_i;
            while ((v_j < ((int64_t)strlen(v_src)))) {
                if ((f_is_digit(((int64_t)(unsigned char)(v_src)[v_j])) == (1 != 1))) {
                    break;
                }
                v_j = (v_j + 1);
            }
            if (((((v_j + 1) < ((int64_t)strlen(v_src))) && (((int64_t)(unsigned char)(v_src)[v_j]) == 46)) && f_is_digit(((int64_t)(unsigned char)(v_src)[(v_j + 1)])))) {
                v_j = (v_j + 1);
                while ((v_j < ((int64_t)strlen(v_src)))) {
                    if ((f_is_digit(((int64_t)(unsigned char)(v_src)[v_j])) == (1 != 1))) {
                        break;
                    }
                    v_j = (v_j + 1);
                }
                v_toks = arr_Token_push(v_toks, mk_Token(f_TK_FLOAT(), substr(v_src, v_i, v_j), v_i));
                v_i = v_j;
                continue;
            }
            if (((v_j < ((int64_t)strlen(v_src))) && (((int64_t)(unsigned char)(v_src)[v_j]) == 95))) {
                v_j = (v_j + 1);
                while ((v_j < ((int64_t)strlen(v_src)))) {
                    v_dd = ((int64_t)(unsigned char)(v_src)[v_j]);
                    if (((f_is_alpha(v_dd) == (1 != 1)) && (f_is_digit(v_dd) == (1 != 1)))) {
                        break;
                    }
                    v_j = (v_j + 1);
                }
            }
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_INT(), substr(v_src, v_i, v_j), v_i));
            v_i = v_j;
            continue;
        }
        if (f_is_alpha(v_c)) {
            v_j = v_i;
            while ((v_j < ((int64_t)strlen(v_src)))) {
                v_d = ((int64_t)(unsigned char)(v_src)[v_j]);
                if (((f_is_alpha(v_d) == (1 != 1)) && (f_is_digit(v_d) == (1 != 1)))) {
                    break;
                }
                v_j = (v_j + 1);
            }
            v_toks = arr_Token_push(v_toks, mk_Token(f_TK_IDENT(), substr(v_src, v_i, v_j), v_i));
            v_i = v_j;
            continue;
        }
        v_k = f_TK_ERR();
        if ((v_c == 43)) {
            v_k = f_TK_PLUS();
        }
        if ((v_c == 45)) {
            v_k = f_TK_MINUS();
        }
        if ((v_c == 42)) {
            v_k = f_TK_STAR();
        }
        if ((v_c == 47)) {
            v_k = f_TK_SLASH();
        }
        if ((v_c == 37)) {
            v_k = f_TK_PERCENT();
        }
        if ((v_c == 46)) {
            v_k = f_TK_DOT();
        }
        if ((v_c == 59)) {
            v_k = f_TK_SEMI();
        }
        if ((v_c == 58)) {
            v_k = f_TK_COLON();
        }
        if ((v_c == 91)) {
            v_k = f_TK_LBRACK();
        }
        if ((v_c == 93)) {
            v_k = f_TK_RBRACK();
        }
        if ((v_c == 40)) {
            v_k = f_TK_LPAREN();
        }
        if ((v_c == 41)) {
            v_k = f_TK_RPAREN();
        }
        if ((v_c == 123)) {
            v_k = f_TK_LBRACE();
        }
        if ((v_c == 125)) {
            v_k = f_TK_RBRACE();
        }
        if ((v_c == 44)) {
            v_k = f_TK_COMMA();
        }
        if ((v_c == 60)) {
            v_k = f_TK_LT();
        }
        if ((v_c == 62)) {
            v_k = f_TK_GT();
        }
        if ((v_c == 61)) {
            v_k = f_TK_ASSIGN();
        }
        if ((v_c == 33)) {
            v_k = f_TK_NOT();
        }
        if ((v_c == 38)) {
            v_k = f_TK_AMP();
        }
        if ((v_c == 94)) {
            v_k = f_TK_CARET();
        }
        if ((v_c == 124)) {
            v_k = f_TK_PIPE();
        }
        if ((v_c == 63)) {
            v_k = f_TK_QUEST();
        }
        v_toks = arr_Token_push(v_toks, mk_Token(v_k, substr(v_src, v_i, (v_i + 1)), v_i));
        v_i = (v_i + 1);
    }
    v_toks = arr_Token_push(v_toks, mk_Token(f_TK_EOF(), "", v_i));
    return v_toks;
}

int64_t f_OP_ADD(void) {
    return 1;
}

int64_t f_OP_SUB(void) {
    return 2;
}

int64_t f_OP_MUL(void) {
    return 3;
}

int64_t f_OP_DIV(void) {
    return 4;
}

int64_t f_OP_MOD(void) {
    return 14;
}

int64_t f_OP_LT(void) {
    return 5;
}

int64_t f_OP_GT(void) {
    return 6;
}

int64_t f_OP_LE(void) {
    return 7;
}

int64_t f_OP_GE(void) {
    return 8;
}

int64_t f_OP_EQ(void) {
    return 9;
}

int64_t f_OP_NE(void) {
    return 10;
}

int64_t f_OP_AND(void) {
    return 11;
}

int64_t f_OP_OR(void) {
    return 12;
}

int64_t f_OP_NOT(void) {
    return 13;
}

int64_t f_OP_SHL(void) {
    return 15;
}

int64_t f_OP_SHR(void) {
    return 16;
}

int64_t f_OP_BAND(void) {
    return 17;
}

int64_t f_OP_BXOR(void) {
    return 18;
}

int64_t f_OP_BOR(void) {
    return 19;
}

int64_t f_OP_CAT(void) {
    return 20;
}

int64_t f_OP_QQ(void) {
    return 21;
}

s_Token f_cur(s_P* v_p) {
    return arr_Token_get((v_p)->toks, (v_p)->pos);
}

int64_t f_ckind(s_P* v_p) {
    return (arr_Token_get((v_p)->toks, (v_p)->pos)).kind;
}

const char* f_ctext(s_P* v_p) {
    return (arr_Token_get((v_p)->toks, (v_p)->pos)).text;
}

void f_adv(s_P* v_p) {
    (v_p)->pos = ((v_p)->pos + 1);
}

const char* f_tok_name(int64_t v_k) {
    if ((v_k == f_TK_RPAREN())) {
        return "')'";
    }
    if ((v_k == f_TK_RBRACE())) {
        return "'}'";
    }
    if ((v_k == f_TK_RBRACK())) {
        return "']'";
    }
    if ((v_k == f_TK_LPAREN())) {
        return "'('";
    }
    if ((v_k == f_TK_LBRACE())) {
        return "'{'";
    }
    if ((v_k == f_TK_COLON())) {
        return "':'";
    }
    if ((v_k == f_TK_COMMA())) {
        return "','";
    }
    if ((v_k == f_TK_WALRUS())) {
        return "':='";
    }
    if ((v_k == f_TK_ASSIGN())) {
        return "'='";
    }
    if ((v_k == f_TK_GT())) {
        return "'>'";
    }
    return "the expected token";
}

void f_report_at(const char* v_src, int64_t v_pos, const char* v_msg) {
    int64_t v_line;
    int64_t v_lstart;
    int64_t v_i;
    int64_t v_lend;
    const char* v_caret;
    int64_t v_k;
    v_line = 1;
    v_lstart = 0;
    v_i = 0;
    while (((v_i < v_pos) && (v_i < ((int64_t)strlen(v_src))))) {
        if ((((int64_t)(unsigned char)(v_src)[v_i]) == 10)) {
            v_line = (v_line + 1);
            v_lstart = (v_i + 1);
        }
        v_i = (v_i + 1);
    }
    v_lend = v_lstart;
    while ((v_lend < ((int64_t)strlen(v_src)))) {
        if ((((int64_t)(unsigned char)(v_src)[v_lend]) == 10)) {
            break;
        }
        v_lend = (v_lend + 1);
    }
    printf("%s\n", scat(scat(scat(scat(scat("error ", i2s(v_line)), ":"), i2s(((v_pos - v_lstart) + 1))), ": "), v_msg));
    printf("%s\n", scat("  ", substr(v_src, v_lstart, v_lend)));
    v_caret = "  ";
    v_k = v_lstart;
    while ((v_k < v_pos)) {
        v_caret = scat(v_caret, " ");
        v_k = (v_k + 1);
    }
    printf("%s\n", scat(v_caret, "^"));
    exit((int)(1));
}

void f_eat(s_P* v_p, int64_t v_k) {
    if ((f_ckind(v_p) == v_k)) {
        f_adv(v_p);
    } else {
        f_report_at((v_p)->src, (arr_Token_get((v_p)->toks, (v_p)->pos)).pos, scat(scat(scat(scat("expected ", f_tok_name(v_k)), ", got '"), f_ctext(v_p)), "'"));
    }
}

int64_t f_cmp_op(int64_t v_k) {
    if ((v_k == f_TK_LT())) {
        return f_OP_LT();
    }
    if ((v_k == f_TK_GT())) {
        return f_OP_GT();
    }
    if ((v_k == f_TK_LE())) {
        return f_OP_LE();
    }
    if ((v_k == f_TK_GE())) {
        return f_OP_GE();
    }
    if ((v_k == f_TK_EQ())) {
        return f_OP_EQ();
    }
    if ((v_k == f_TK_NE())) {
        return f_OP_NE();
    }
    return 0;
}

s_Expr f_parse_expr(s_P* v_p) {
    return f_parse_coalesce(v_p);
}

s_Expr f_parse_coalesce(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_pipe(v_p);
    while ((f_ckind(v_p) == f_TK_QQ())) {
        f_adv(v_p);
        v_left = mkv_Bin(f_OP_QQ(), v_left, f_parse_pipe(v_p));
    }
    return v_left;
}

arr_Expr f_prepend_arg(s_Expr v_x, arr_Expr v_xs) {
    arr_Expr v_out;
    int64_t v_i;
    v_out = ({ arr_Expr __a = arr_Expr_new(); __a = arr_Expr_push(__a, v_x); __a; });
    v_i = 0;
    while ((v_i < arr_Expr_len(v_xs))) {
        v_out = arr_Expr_push(v_out, arr_Expr_get(v_xs, v_i));
        v_i = (v_i + 1);
    }
    return v_out;
}

s_Expr f_pipe_call(s_Expr v_lhs, s_Expr v_rhs) {
    return ({ s_Expr __m; s_Expr __s = v_rhs; if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = mkv_Call(v_n, ({ arr_Expr __a = arr_Expr_new(); __a = arr_Expr_push(__a, v_lhs); __a; })); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = mkv_Call(v_f, f_prepend_arg(v_lhs, v_a)); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = v_rhs; } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = v_rhs; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = v_rhs; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = v_rhs; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = v_rhs; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_x = *(__s.u.Bin.f1); s_Expr v_y = *(__s.u.Bin.f2); __m = v_rhs; } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = v_rhs; } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = v_rhs; } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = v_rhs; } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = v_rhs; } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = v_rhs; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = v_rhs; } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = v_rhs; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = v_rhs; } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = v_rhs; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = v_rhs; } else if(__s.tag==18){ __m = v_rhs; } __m; });
}

s_Expr f_parse_pipe(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_or(v_p);
    while ((f_ckind(v_p) == f_TK_PIPEGT())) {
        f_adv(v_p);
        v_left = f_pipe_call(v_left, f_parse_or(v_p));
    }
    return v_left;
}

s_Expr f_parse_or(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_and(v_p);
    while ((f_ckind(v_p) == f_TK_OR())) {
        f_adv(v_p);
        v_left = mkv_Bin(f_OP_OR(), v_left, f_parse_and(v_p));
    }
    return v_left;
}

s_Expr f_parse_and(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_bitor(v_p);
    while ((f_ckind(v_p) == f_TK_AND())) {
        f_adv(v_p);
        v_left = mkv_Bin(f_OP_AND(), v_left, f_parse_bitor(v_p));
    }
    return v_left;
}

s_Expr f_parse_bitor(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_bitxor(v_p);
    while ((f_ckind(v_p) == f_TK_PIPE())) {
        f_adv(v_p);
        v_left = mkv_Bin(f_OP_BOR(), v_left, f_parse_bitxor(v_p));
    }
    return v_left;
}

s_Expr f_parse_bitxor(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_bitand(v_p);
    while ((f_ckind(v_p) == f_TK_CARET())) {
        f_adv(v_p);
        v_left = mkv_Bin(f_OP_BXOR(), v_left, f_parse_bitand(v_p));
    }
    return v_left;
}

s_Expr f_parse_bitand(s_P* v_p) {
    s_Expr v_left;
    v_left = f_parse_cmp(v_p);
    while ((f_ckind(v_p) == f_TK_AMP())) {
        f_adv(v_p);
        v_left = mkv_Bin(f_OP_BAND(), v_left, f_parse_cmp(v_p));
    }
    return v_left;
}

s_Expr f_parse_cmp(s_P* v_p) {
    s_Expr v_left;
    int64_t v_o;
    v_left = f_parse_shift(v_p);
    v_o = f_cmp_op(f_ckind(v_p));
    if ((v_o != 0)) {
        f_adv(v_p);
        v_left = mkv_Bin(v_o, v_left, f_parse_shift(v_p));
    }
    return v_left;
}

s_Expr f_parse_shift(s_P* v_p) {
    s_Expr v_left;
    int64_t v_k;
    v_left = f_parse_add(v_p);
    while ((1 == 1)) {
        v_k = f_ckind(v_p);
        if ((v_k == f_TK_LTLT())) {
            f_adv(v_p);
            v_left = mkv_Bin(f_OP_SHL(), v_left, f_parse_add(v_p));
        } else {
            if ((v_k == f_TK_GTGT())) {
                f_adv(v_p);
                v_left = mkv_Bin(f_OP_SHR(), v_left, f_parse_add(v_p));
            } else {
                break;
            }
        }
    }
    return v_left;
}

s_Expr f_parse_add(s_P* v_p) {
    s_Expr v_left;
    int64_t v_k;
    v_left = f_parse_mul(v_p);
    while ((1 == 1)) {
        v_k = f_ckind(v_p);
        if ((v_k == f_TK_PLUS())) {
            f_adv(v_p);
            v_left = mkv_Bin(f_OP_ADD(), v_left, f_parse_mul(v_p));
        } else {
            if ((v_k == f_TK_MINUS())) {
                f_adv(v_p);
                v_left = mkv_Bin(f_OP_SUB(), v_left, f_parse_mul(v_p));
            } else {
                if ((v_k == f_TK_CONCAT())) {
                    f_adv(v_p);
                    v_left = mkv_Bin(f_OP_CAT(), v_left, f_parse_mul(v_p));
                } else {
                    break;
                }
            }
        }
    }
    return v_left;
}

s_Expr f_parse_mul(s_P* v_p) {
    s_Expr v_left;
    int64_t v_k;
    v_left = f_parse_unary(v_p);
    while ((1 == 1)) {
        v_k = f_ckind(v_p);
        if ((v_k == f_TK_STAR())) {
            f_adv(v_p);
            v_left = mkv_Bin(f_OP_MUL(), v_left, f_parse_unary(v_p));
        } else {
            if ((v_k == f_TK_SLASH())) {
                f_adv(v_p);
                v_left = mkv_Bin(f_OP_DIV(), v_left, f_parse_unary(v_p));
            } else {
                if ((v_k == f_TK_PERCENT())) {
                    f_adv(v_p);
                    v_left = mkv_Bin(f_OP_MOD(), v_left, f_parse_unary(v_p));
                } else {
                    break;
                }
            }
        }
    }
    return v_left;
}

s_Expr f_parse_unary(s_P* v_p) {
    if ((f_ckind(v_p) == f_TK_NOT())) {
        f_adv(v_p);
        return mkv_Unary(f_OP_NOT(), f_parse_unary(v_p));
    }
    if ((f_ckind(v_p) == f_TK_MINUS())) {
        f_adv(v_p);
        return mkv_Bin(f_OP_SUB(), mkv_Num(0), f_parse_unary(v_p));
    }
    if ((f_ckind(v_p) == f_TK_AMP())) {
        f_adv(v_p);
        return mkv_Addr(f_parse_unary(v_p));
    }
    return f_parse_factor(v_p);
}

s_Expr f_parse_factor(s_P* v_p) {
    s_Expr v_e;
    const char* v_fname;
    arr_Expr v_margs;
    s_Expr v_idx;
    v_e = f_parse_primary(v_p);
    while (((f_ckind(v_p) == f_TK_DOT()) || (f_ckind(v_p) == f_TK_LBRACK()))) {
        if ((f_ckind(v_p) == f_TK_DOT())) {
            f_adv(v_p);
            v_fname = f_ctext(v_p);
            f_adv(v_p);
            if ((f_ckind(v_p) == f_TK_LPAREN())) {
                f_adv(v_p);
                v_margs = ({ arr_Expr __a = arr_Expr_new(); __a = arr_Expr_push(__a, v_e); __a; });
                while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
                    v_margs = arr_Expr_push(v_margs, f_parse_expr(v_p));
                    if ((f_ckind(v_p) == f_TK_COMMA())) {
                        f_adv(v_p);
                    }
                }
                f_eat(v_p, f_TK_RPAREN());
                v_e = mkv_Call(v_fname, v_margs);
            } else {
                if ((strcmp(v_fname, "cstr") == 0)) {
                    v_e = mkv_Call("cstr", ({ arr_Expr __a = arr_Expr_new(); __a = arr_Expr_push(__a, v_e); __a; }));
                } else {
                    v_e = mkv_Field(v_e, v_fname);
                }
            }
        } else {
            f_adv(v_p);
            v_idx = f_parse_expr(v_p);
            f_eat(v_p, f_TK_RBRACK());
            v_e = mkv_Index(v_e, v_idx);
        }
    }
    if ((f_ckind(v_p) == f_TK_QUEST())) {
        f_adv(v_p);
        return mkv_Try(v_e);
    }
    return v_e;
}

s_Expr f_parse_brace_expr(s_P* v_p) {
    s_Expr v_e;
    f_eat(v_p, f_TK_LBRACE());
    v_e = f_parse_expr(v_p);
    f_eat(v_p, f_TK_RBRACE());
    return v_e;
}

int64_t f_str_has_interp(const char* v_s) {
    int64_t v_i;
    v_i = 0;
    while (((v_i + 1) < ((int64_t)strlen(v_s)))) {
        if (((((int64_t)(unsigned char)(v_s)[v_i]) == 36) && (((int64_t)(unsigned char)(v_s)[(v_i + 1)]) == 123))) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

s_Expr f_parse_interp(const char* v_s) {
    arr_Expr v_parts;
    int64_t v_i;
    int64_t v_start;
    int64_t v_depth;
    int64_t v_j;
    int64_t v_d;
    const char* v_esrc;
    arr_Token v_etoks;
    s_P v_ep;
    arr_Expr v_hole;
    s_Expr v_r;
    int64_t v_k;
    v_parts = ({ arr_Expr __a = arr_Expr_new(); __a; });
    v_i = 0;
    v_start = 0;
    while ((v_i < ((int64_t)strlen(v_s)))) {
        if (((((v_i + 1) < ((int64_t)strlen(v_s))) && (((int64_t)(unsigned char)(v_s)[v_i]) == 36)) && (((int64_t)(unsigned char)(v_s)[(v_i + 1)]) == 123))) {
            if ((v_i > v_start)) {
                v_parts = arr_Expr_push(v_parts, mkv_Str(substr(v_s, v_start, v_i)));
            }
            v_depth = 1;
            v_j = (v_i + 2);
            while ((v_j < ((int64_t)strlen(v_s)))) {
                v_d = ((int64_t)(unsigned char)(v_s)[v_j]);
                if ((v_d == 123)) {
                    v_depth = (v_depth + 1);
                }
                if ((v_d == 125)) {
                    v_depth = (v_depth - 1);
                    if ((v_depth == 0)) {
                        break;
                    }
                }
                v_j = (v_j + 1);
            }
            v_esrc = substr(v_s, (v_i + 2), v_j);
            v_etoks = f_lex(v_esrc);
            v_ep = mk_P(v_etoks, 0, v_esrc);
            v_hole = ({ arr_Expr __a = arr_Expr_new(); __a; });
            v_hole = arr_Expr_push(v_hole, f_parse_expr((&v_ep)));
            if ((f_ckind((&v_ep)) == f_TK_EOF())) {
                v_parts = arr_Expr_push(v_parts, mkv_Call("to_str", v_hole));
            } else {
                v_parts = arr_Expr_push(v_parts, mkv_Str(scat(scat(scat("$", "{"), v_esrc), "}")));
            }
            v_i = (v_j + 1);
            v_start = v_i;
        } else {
            v_i = (v_i + 1);
        }
    }
    if ((((int64_t)strlen(v_s)) > v_start)) {
        v_parts = arr_Expr_push(v_parts, mkv_Str(substr(v_s, v_start, ((int64_t)strlen(v_s)))));
    }
    if ((arr_Expr_len(v_parts) == 0)) {
        return mkv_Str("");
    }
    v_r = arr_Expr_get(v_parts, 0);
    v_k = 1;
    while ((v_k < arr_Expr_len(v_parts))) {
        v_r = mkv_Bin(f_OP_ADD(), v_r, arr_Expr_get(v_parts, v_k));
        v_k = (v_k + 1);
    }
    return v_r;
}

int64_t f_char_code(const char* v_s) {
    int64_t v_c;
    if ((((int64_t)strlen(v_s)) == 0)) {
        return 0;
    }
    if ((((int64_t)(unsigned char)(v_s)[0]) == 92)) {
        if ((((int64_t)strlen(v_s)) < 2)) {
            return 92;
        }
        v_c = ((int64_t)(unsigned char)(v_s)[1]);
        if ((v_c == 110)) {
            return 10;
        }
        if ((v_c == 116)) {
            return 9;
        }
        if ((v_c == 114)) {
            return 13;
        }
        if ((v_c == 48)) {
            return 0;
        }
        if ((v_c == 92)) {
            return 92;
        }
        if ((v_c == 39)) {
            return 39;
        }
        return v_c;
    }
    return ((int64_t)(unsigned char)(v_s)[0]);
}

int64_t f_is_bad(s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==18){ __m = (1 == 1); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = (1 != 1); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = (1 != 1); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = (1 != 1); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = (1 != 1); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = (1 != 1); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = (1 != 1); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = (1 != 1); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = (1 != 1); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = (1 != 1); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = (1 != 1); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = (1 != 1); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = (1 != 1); } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = (1 != 1); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = (1 != 1); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = (1 != 1); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = (1 != 1); } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = (1 != 1); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = (1 != 1); } __m; });
}

int64_t f_is_lower_ident(const char* v_s) {
    int64_t v_c;
    if ((((int64_t)strlen(v_s)) == 0)) {
        return (1 != 1);
    }
    v_c = ((int64_t)(unsigned char)(v_s)[0]);
    return ((v_c >= 97) && (v_c <= 122));
}

s_Expr f_subst_var_hit(const char* v_n, const char* v_nm, s_Expr v_repl, s_Expr v_orig) {
    if ((strcmp(v_n, v_nm) == 0)) {
        return v_repl;
    }
    return v_orig;
}

arr_Expr f_subst_args(arr_Expr v_es, const char* v_nm, s_Expr v_repl) {
    arr_Expr v_out;
    int64_t v_i;
    v_out = ({ arr_Expr __a = arr_Expr_new(); __a; });
    v_i = 0;
    while ((v_i < arr_Expr_len(v_es))) {
        v_out = arr_Expr_push(v_out, f_subst_var(arr_Expr_get(v_es, v_i), v_nm, v_repl));
        v_i = (v_i + 1);
    }
    return v_out;
}

s_Expr f_subst_var(s_Expr v_e, const char* v_nm, s_Expr v_repl) {
    return ({ s_Expr __m; s_Expr __s = v_e; if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = f_subst_var_hit(v_n, v_nm, v_repl, v_e); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = mkv_Tuple(f_subst_args(v_tes, v_nm, v_repl)); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = v_e; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = mkv_Bin(v_o, f_subst_var(v_a, v_nm, v_repl), f_subst_var(v_b, v_nm, v_repl)); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = mkv_Unary(v_o, f_subst_var(v_x, v_nm, v_repl)); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_ar = __s.u.Call.f1; __m = mkv_Call(v_f, f_subst_args(v_ar, v_nm, v_repl)); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = mkv_Field(f_subst_var(v_o, v_nm, v_repl), v_f); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = mkv_Index(f_subst_var(v_o, v_nm, v_repl), f_subst_var(v_ix, v_nm, v_repl)); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = mkv_Array(f_subst_args(v_es, v_nm, v_repl), v_et); } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_ks = __s.u.MapLit.f1; arr_Expr v_vs = __s.u.MapLit.f2; __m = mkv_MapLit(v_ml, f_subst_args(v_ks, v_nm, v_repl), f_subst_args(v_vs, v_nm, v_repl)); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = mkv_Addr(f_subst_var(v_x, v_nm, v_repl)); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = mkv_Match(f_subst_var(v_sc, v_nm, v_repl), v_vn, v_vb, f_subst_args(v_bd, v_nm, v_repl)); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = mkv_IfE(f_subst_var(v_c, v_nm, v_repl), f_subst_var(v_t, v_nm, v_repl), f_subst_var(v_el2, v_nm, v_repl)); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = mkv_Try(f_subst_var(v_x, v_nm, v_repl)); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = mkv_Lambda(v_ps, v_pts, f_subst_var(v_b, v_nm, v_repl), v_id); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = v_e; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = v_e; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = v_e; } else if(__s.tag==18){ __m = v_e; } __m; });
}

s_Expr f_parse_arm_cond(s_P* v_p, arr_Expr v_scruts, int64_t v_is_tuple, s_Binds* v_bnd) {
    s_Expr v_cond;
    int64_t v_j;
    s_Expr v_c;
    if (v_is_tuple) {
        if ((f_ckind(v_p) == f_TK_LPAREN())) {
            f_adv(v_p);
            v_cond = mkv_Bad();
            v_j = 0;
            while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
                if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "_") == 0))) {
                    f_adv(v_p);
                } else {
                    if (((((f_ckind(v_p) == f_TK_IDENT()) && f_is_lower_ident(f_ctext(v_p))) && (strcmp(f_ctext(v_p), "true") != 0)) && (strcmp(f_ctext(v_p), "false") != 0))) {
                        if ((v_j < arr_Expr_len(v_scruts))) {
                            (v_bnd)->names = arr_str_push((v_bnd)->names, f_ctext(v_p));
                            (v_bnd)->vals = arr_Expr_push((v_bnd)->vals, arr_Expr_get(v_scruts, v_j));
                        }
                        f_adv(v_p);
                    } else {
                        if ((v_j >= arr_Expr_len(v_scruts))) {
                            f_report_at((v_p)->src, (arr_Token_get((v_p)->toks, (v_p)->pos)).pos, "match arm has more components than the tuple scrutinee");
                        }
                        v_c = mkv_Bin(f_OP_EQ(), arr_Expr_get(v_scruts, v_j), f_parse_primary(v_p));
                        if (f_is_bad(v_cond)) {
                            v_cond = v_c;
                        } else {
                            v_cond = mkv_Bin(f_OP_AND(), v_cond, v_c);
                        }
                    }
                }
                v_j = (v_j + 1);
                if ((f_ckind(v_p) == f_TK_COMMA())) {
                    f_adv(v_p);
                }
            }
            f_eat(v_p, f_TK_RPAREN());
            if ((v_j < arr_Expr_len(v_scruts))) {
                f_report_at((v_p)->src, (arr_Token_get((v_p)->toks, (v_p)->pos)).pos, "match arm has fewer components than the tuple scrutinee");
            }
            return v_cond;
        }
        if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "_") == 0))) {
            f_adv(v_p);
        }
        return mkv_Bad();
    }
    if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "_") == 0))) {
        f_adv(v_p);
        return mkv_Bad();
    }
    return mkv_Bin(f_OP_EQ(), arr_Expr_get(v_scruts, 0), f_parse_primary(v_p));
}

s_Expr f_build_if_chain(arr_Expr v_conds, arr_Expr v_bodies) {
    s_Expr v_acc;
    int64_t v_i;
    v_acc = mkv_Bad();
    v_i = (arr_Expr_len(v_conds) - 1);
    while ((v_i >= 0)) {
        if (f_is_bad(arr_Expr_get(v_conds, v_i))) {
            v_acc = arr_Expr_get(v_bodies, v_i);
        } else {
            v_acc = mkv_IfE(arr_Expr_get(v_conds, v_i), arr_Expr_get(v_bodies, v_i), v_acc);
        }
        v_i = (v_i - 1);
    }
    return v_acc;
}

s_Expr f_parse_match_desugar(s_P* v_p, arr_Expr v_scruts, int64_t v_is_tuple) {
    arr_Expr v_conds;
    arr_Expr v_bodies;
    s_Binds v_bnd;
    s_Expr v_cond;
    s_Expr v_body;
    int64_t v_bi;
    v_conds = ({ arr_Expr __a = arr_Expr_new(); __a; });
    v_bodies = ({ arr_Expr __a = arr_Expr_new(); __a; });
    while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
        v_bnd = mk_Binds(f_empty_strs(), f_no_exprs());
        v_cond = f_parse_arm_cond(v_p, v_scruts, v_is_tuple, (&v_bnd));
        if ((f_ckind(v_p) == f_TK_ASSIGN())) {
            f_adv(v_p);
        }
        if ((f_ckind(v_p) == f_TK_GT())) {
            f_adv(v_p);
        }
        v_conds = arr_Expr_push(v_conds, v_cond);
        v_body = f_parse_expr(v_p);
        v_bi = 0;
        while ((v_bi < arr_str_len((v_bnd).names))) {
            v_body = f_subst_var(v_body, arr_str_get((v_bnd).names, v_bi), arr_Expr_get((v_bnd).vals, v_bi));
            v_bi = (v_bi + 1);
        }
        v_bodies = arr_Expr_push(v_bodies, v_body);
        if ((f_ckind(v_p) == f_TK_SEMI())) {
            f_adv(v_p);
        }
    }
    f_eat(v_p, f_TK_RBRACE());
    return f_build_if_chain(v_conds, v_bodies);
}

arr_str f_struct_field_names(arr_Token v_toks, const char* v_name) {
    arr_str v_out;
    int64_t v_i;
    int64_t v_depth;
    int64_t v_j;
    int64_t v_kk;
    v_out = ({ arr_str __a = arr_str_new(); __a; });
    v_i = 0;
    while ((v_i < arr_Token_len(v_toks))) {
        if ((((((arr_Token_get(v_toks, v_i)).kind == f_TK_IDENT()) && (strcmp((arr_Token_get(v_toks, v_i)).text, "st") == 0)) && ((v_i + 2) < arr_Token_len(v_toks))) && (strcmp((arr_Token_get(v_toks, (v_i + 1))).text, v_name) == 0))) {
            v_depth = 1;
            v_j = (v_i + 3);
            while ((((v_depth > 0) && (v_j < arr_Token_len(v_toks))) && ((arr_Token_get(v_toks, v_j)).kind != f_TK_EOF()))) {
                v_kk = (arr_Token_get(v_toks, v_j)).kind;
                if ((v_kk == f_TK_LBRACE())) {
                    v_depth = (v_depth + 1);
                } else {
                    if ((v_kk == f_TK_RBRACE())) {
                        v_depth = (v_depth - 1);
                    } else {
                        if (((((v_depth == 1) && (v_kk == f_TK_IDENT())) && ((v_j + 1) < arr_Token_len(v_toks))) && ((arr_Token_get(v_toks, (v_j + 1))).kind == f_TK_COLON()))) {
                            v_out = arr_str_push(v_out, (arr_Token_get(v_toks, v_j)).text);
                        }
                    }
                }
                v_j = (v_j + 1);
            }
            return v_out;
        }
        v_i = (v_i + 1);
    }
    return v_out;
}

int64_t f_brace_is_block(s_P* v_p) {
    int64_t v_i;
    int64_t v_depth;
    int64_t v_saw;
    int64_t v_kk;
    v_i = ((v_p)->pos + 1);
    v_depth = 1;
    v_saw = (1 != 1);
    while ((v_i < arr_Token_len((v_p)->toks))) {
        v_kk = (arr_Token_get((v_p)->toks, v_i)).kind;
        if (((v_kk == f_TK_RBRACE()) && (v_depth == 1))) {
            break;
        }
        v_saw = (1 == 1);
        if ((((v_kk == f_TK_LBRACE()) || (v_kk == f_TK_LBRACK())) || (v_kk == f_TK_LPAREN()))) {
            v_depth = (v_depth + 1);
        } else {
            if ((((v_kk == f_TK_RBRACE()) || (v_kk == f_TK_RBRACK())) || (v_kk == f_TK_RPAREN()))) {
                v_depth = (v_depth - 1);
            } else {
                if ((v_depth == 1)) {
                    if ((v_kk == f_TK_WALRUS())) {
                        return (1 == 1);
                    }
                    if ((v_kk == f_TK_SEMI())) {
                        return (1 == 1);
                    }
                    if ((v_kk == f_TK_COLON())) {
                        return (1 != 1);
                    }
                }
            }
        }
        v_i = (v_i + 1);
    }
    return v_saw;
}

s_Expr f_parse_primary(s_P* v_p) {
    int64_t v_k;
    int64_t v_v;
    const char* v_s;
    int64_t v_c;
    int64_t v_id;
    arr_str v_params;
    arr_str v_ptypes;
    arr_Expr v_scruts;
    int64_t v_is_tuple;
    int64_t v_fk;
    s_Expr v_scrut;
    arr_str v_vnames;
    arr_str v_vbinds;
    arr_Expr v_bodies;
    const char* v_vname;
    const char* v_binds;
    s_Expr v_body;
    s_Expr v_cond;
    s_Expr v_then_e;
    s_Expr v_els;
    arr_Expr v_elems;
    arr_Expr v_mkeys;
    arr_Expr v_mvals;
    const char* v_name;
    arr_Expr v_args;
    arr_str v_order;
    arr_str v_fnames;
    arr_Expr v_fvals;
    arr_Expr v_cargs;
    int64_t v_oi;
    int64_t v_fi;
    s_Expr v_inner;
    v_k = f_ckind(v_p);
    if ((v_k == f_TK_INT())) {
        v_v = s2i(f_ctext(v_p));
        f_adv(v_p);
        return mkv_Num(v_v);
    }
    if ((v_k == f_TK_FLOAT())) {
        v_s = f_ctext(v_p);
        f_adv(v_p);
        return mkv_Flt(v_s);
    }
    if ((v_k == f_TK_CHAR())) {
        v_c = f_char_code(f_ctext(v_p));
        f_adv(v_p);
        return mkv_Num(v_c);
    }
    if ((v_k == f_TK_STR())) {
        v_s = f_ctext(v_p);
        f_adv(v_p);
        if (f_str_has_interp(v_s)) {
            return f_parse_interp(v_s);
        }
        return mkv_Str(v_s);
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "fn") == 0))) {
        v_id = (v_p)->pos;
        f_adv(v_p);
        f_eat(v_p, f_TK_LPAREN());
        v_params = ({ arr_str __a = arr_str_new(); __a; });
        v_ptypes = ({ arr_str __a = arr_str_new(); __a; });
        while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
            if ((f_ckind(v_p) == f_TK_IDENT())) {
                v_params = arr_str_push(v_params, f_ctext(v_p));
                f_adv(v_p);
                if ((f_ckind(v_p) == f_TK_COLON())) {
                    f_adv(v_p);
                    v_ptypes = arr_str_push(v_ptypes, f_parse_type(v_p));
                } else {
                    v_ptypes = arr_str_push(v_ptypes, "i64");
                }
            } else {
                if ((f_ckind(v_p) == f_TK_COMMA())) {
                    f_adv(v_p);
                } else {
                    f_adv(v_p);
                }
            }
        }
        f_eat(v_p, f_TK_RPAREN());
        return mkv_Lambda(v_params, v_ptypes, f_parse_expr(v_p), v_id);
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "mt") == 0))) {
        f_adv(v_p);
        v_scruts = ({ arr_Expr __a = arr_Expr_new(); __a; });
        v_is_tuple = (1 != 1);
        if ((f_ckind(v_p) == f_TK_LPAREN())) {
            f_adv(v_p);
            v_scruts = arr_Expr_push(v_scruts, f_parse_expr(v_p));
            while ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
                v_scruts = arr_Expr_push(v_scruts, f_parse_expr(v_p));
                v_is_tuple = (1 == 1);
            }
            f_eat(v_p, f_TK_RPAREN());
        } else {
            v_scruts = arr_Expr_push(v_scruts, f_parse_expr(v_p));
        }
        f_eat(v_p, f_TK_LBRACE());
        v_fk = f_ckind(v_p);
        if (((((v_is_tuple || (v_fk == f_TK_INT())) || (v_fk == f_TK_STR())) || (v_fk == f_TK_CHAR())) || ((v_fk == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "_") == 0)))) {
            return f_parse_match_desugar(v_p, v_scruts, v_is_tuple);
        }
        v_scrut = arr_Expr_get(v_scruts, 0);
        v_vnames = ({ arr_str __a = arr_str_new(); __a; });
        v_vbinds = ({ arr_str __a = arr_str_new(); __a; });
        v_bodies = ({ arr_Expr __a = arr_Expr_new(); __a; });
        while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
            v_vname = f_ctext(v_p);
            f_adv(v_p);
            v_binds = "";
            if ((f_ckind(v_p) == f_TK_LPAREN())) {
                f_adv(v_p);
                while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
                    if ((((int64_t)strlen(v_binds)) > 0)) {
                        v_binds = scat(v_binds, ";");
                    }
                    v_binds = scat(v_binds, f_ctext(v_p));
                    f_adv(v_p);
                    if ((f_ckind(v_p) == f_TK_COMMA())) {
                        f_adv(v_p);
                    }
                }
                f_eat(v_p, f_TK_RPAREN());
            }
            if ((f_ckind(v_p) == f_TK_ASSIGN())) {
                f_adv(v_p);
            }
            if ((f_ckind(v_p) == f_TK_GT())) {
                f_adv(v_p);
            }
            v_body = f_parse_expr(v_p);
            v_vnames = arr_str_push(v_vnames, v_vname);
            v_vbinds = arr_str_push(v_vbinds, v_binds);
            v_bodies = arr_Expr_push(v_bodies, v_body);
            if ((f_ckind(v_p) == f_TK_SEMI())) {
                f_adv(v_p);
            }
        }
        f_eat(v_p, f_TK_RBRACE());
        return mkv_Match(v_scrut, v_vnames, v_vbinds, v_bodies);
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "if") == 0))) {
        f_adv(v_p);
        v_cond = f_parse_expr(v_p);
        v_then_e = f_parse_brace_expr(v_p);
        v_els = mkv_Bad();
        if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "el") == 0))) {
            f_adv(v_p);
            if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "if") == 0))) {
                v_els = f_parse_primary(v_p);
            } else {
                v_els = f_parse_brace_expr(v_p);
            }
        }
        return mkv_IfE(v_cond, v_then_e, v_els);
    }
    if ((v_k == f_TK_LBRACK())) {
        f_adv(v_p);
        v_elems = ({ arr_Expr __a = arr_Expr_new(); __a; });
        while (((f_ckind(v_p) != f_TK_RBRACK()) && (f_ckind(v_p) != f_TK_EOF()))) {
            v_elems = arr_Expr_push(v_elems, f_parse_expr(v_p));
            if ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
            }
        }
        f_eat(v_p, f_TK_RBRACK());
        return mkv_Array(v_elems, "");
    }
    if (((v_k == f_TK_LBRACE()) && f_brace_is_block(v_p))) {
        return mkv_BlockE(f_parse_block(v_p));
    }
    if ((v_k == f_TK_LBRACE())) {
        f_adv(v_p);
        v_mkeys = ({ arr_Expr __a = arr_Expr_new(); __a; });
        v_mvals = ({ arr_Expr __a = arr_Expr_new(); __a; });
        while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
            v_mkeys = arr_Expr_push(v_mkeys, f_parse_expr(v_p));
            f_eat(v_p, f_TK_COLON());
            v_mvals = arr_Expr_push(v_mvals, f_parse_expr(v_p));
            if ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
            }
        }
        f_eat(v_p, f_TK_RBRACE());
        return mkv_MapLit("", v_mkeys, v_mvals);
    }
    if ((v_k == f_TK_IDENT())) {
        v_name = f_ctext(v_p);
        if ((strcmp(v_name, "true") == 0)) {
            f_adv(v_p);
            return mkv_Bin(f_OP_EQ(), mkv_Num(1), mkv_Num(1));
        }
        if ((strcmp(v_name, "false") == 0)) {
            f_adv(v_p);
            return mkv_Bin(f_OP_NE(), mkv_Num(1), mkv_Num(1));
        }
        f_adv(v_p);
        if ((f_ckind(v_p) == f_TK_LPAREN())) {
            f_adv(v_p);
            v_args = ({ arr_Expr __a = arr_Expr_new(); __a; });
            while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
                v_args = arr_Expr_push(v_args, f_parse_expr(v_p));
                if ((f_ckind(v_p) == f_TK_COMMA())) {
                    f_adv(v_p);
                }
            }
            f_eat(v_p, f_TK_RPAREN());
            return mkv_Call(v_name, v_args);
        }
        if ((f_ckind(v_p) == f_TK_LBRACE())) {
            v_order = f_struct_field_names((v_p)->toks, v_name);
            if ((arr_str_len(v_order) > 0)) {
                f_adv(v_p);
                v_fnames = ({ arr_str __a = arr_str_new(); __a; });
                v_fvals = ({ arr_Expr __a = arr_Expr_new(); __a; });
                while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
                    v_fnames = arr_str_push(v_fnames, f_ctext(v_p));
                    f_adv(v_p);
                    f_eat(v_p, f_TK_COLON());
                    v_fvals = arr_Expr_push(v_fvals, f_parse_expr(v_p));
                    if ((f_ckind(v_p) == f_TK_COMMA())) {
                        f_adv(v_p);
                    }
                }
                f_eat(v_p, f_TK_RBRACE());
                v_cargs = ({ arr_Expr __a = arr_Expr_new(); __a; });
                v_oi = 0;
                while ((v_oi < arr_str_len(v_order))) {
                    v_fi = 0;
                    while ((v_fi < arr_str_len(v_fnames))) {
                        if ((strcmp(arr_str_get(v_fnames, v_fi), arr_str_get(v_order, v_oi)) == 0)) {
                            v_cargs = arr_Expr_push(v_cargs, arr_Expr_get(v_fvals, v_fi));
                        }
                        v_fi = (v_fi + 1);
                    }
                    v_oi = (v_oi + 1);
                }
                return mkv_Call(v_name, v_cargs);
            }
        }
        return mkv_Var(v_name);
    }
    if ((v_k == f_TK_LPAREN())) {
        f_adv(v_p);
        v_inner = f_parse_expr(v_p);
        if ((f_ckind(v_p) == f_TK_COMMA())) {
            v_elems = ({ arr_Expr __a = arr_Expr_new(); __a = arr_Expr_push(__a, v_inner); __a; });
            while ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
                if ((f_ckind(v_p) == f_TK_RPAREN())) {
                    break;
                }
                v_elems = arr_Expr_push(v_elems, f_parse_expr(v_p));
            }
            f_eat(v_p, f_TK_RPAREN());
            return mkv_Tuple(v_elems);
        }
        f_eat(v_p, f_TK_RPAREN());
        return v_inner;
    }
    f_report_at((v_p)->src, (arr_Token_get((v_p)->toks, (v_p)->pos)).pos, scat(scat("unexpected token '", f_ctext(v_p)), "' in expression"));
    f_adv(v_p);
    return mkv_Bad();
}

arr_Stmt f_parse_block(s_P* v_p) {
    arr_Stmt v_body;
    v_body = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
    if ((f_ckind(v_p) == f_TK_LBRACE())) {
        f_adv(v_p);
        while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
            if ((f_ckind(v_p) == f_TK_SEMI())) {
                f_adv(v_p);
                continue;
            }
            v_body = arr_Stmt_push(v_body, f_parse_stmt(v_p));
        }
        f_eat(v_p, f_TK_RBRACE());
    } else {
        v_body = arr_Stmt_push(v_body, f_parse_stmt(v_p));
    }
    return v_body;
}

int64_t f_is_destructure_ahead(s_P* v_p) {
    int64_t v_i;
    if ((f_ckind(v_p) != f_TK_IDENT())) {
        return (1 != 1);
    }
    v_i = ((v_p)->pos + 1);
    if (((v_i >= arr_Token_len((v_p)->toks)) || ((arr_Token_get((v_p)->toks, v_i)).kind != f_TK_COMMA()))) {
        return (1 != 1);
    }
    while (((v_i < arr_Token_len((v_p)->toks)) && ((arr_Token_get((v_p)->toks, v_i)).kind == f_TK_COMMA()))) {
        v_i = (v_i + 1);
        if (((v_i >= arr_Token_len((v_p)->toks)) || ((arr_Token_get((v_p)->toks, v_i)).kind != f_TK_IDENT()))) {
            return (1 != 1);
        }
        v_i = (v_i + 1);
    }
    return ((v_i < arr_Token_len((v_p)->toks)) && ((arr_Token_get((v_p)->toks, v_i)).kind == f_TK_WALRUS()));
}

s_Stmt f_parse_destructure(s_P* v_p) {
    arr_str v_names;
    v_names = ({ arr_str __a = arr_str_new(); __a; });
    while ((f_ckind(v_p) == f_TK_IDENT())) {
        v_names = arr_str_push(v_names, f_ctext(v_p));
        f_adv(v_p);
        if ((f_ckind(v_p) == f_TK_COMMA())) {
            f_adv(v_p);
        } else {
            break;
        }
    }
    f_eat(v_p, f_TK_WALRUS());
    return mkv_SDestructure(v_names, f_parse_expr(v_p));
}

s_Stmt f_parse_stmt(s_P* v_p) {
    int64_t v_k;
    const char* v_t;
    int64_t v_spos;
    s_Expr v_cond;
    arr_Stmt v_body;
    arr_Stmt v_else_b;
    const char* v_kn;
    const char* v_vn;
    s_Expr v_coll;
    const char* v_vnm;
    int64_t v_incl;
    s_Expr v_hi;
    s_Expr v_e;
    const char* v_name;
    const char* v_ann;
    int64_t v_cop;
    s_Expr v_lhs;
    v_k = f_ckind(v_p);
    v_t = f_ctext(v_p);
    v_spos = (f_cur(v_p)).pos;
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "rt") == 0))) {
        f_adv(v_p);
        return mkv_SReturn(f_parse_expr(v_p), v_spos);
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "br") == 0))) {
        f_adv(v_p);
        return mkv_SBreak();
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "ct") == 0))) {
        f_adv(v_p);
        return mkv_SContinue();
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "if") == 0))) {
        f_adv(v_p);
        v_cond = f_parse_expr(v_p);
        v_body = f_parse_block(v_p);
        v_else_b = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
        if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "el") == 0))) {
            f_adv(v_p);
            if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "if") == 0))) {
                v_else_b = arr_Stmt_push(v_else_b, f_parse_stmt(v_p));
            } else {
                v_else_b = f_parse_block(v_p);
            }
        }
        return mkv_SIf(v_cond, v_body, v_else_b);
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "lp") == 0))) {
        f_adv(v_p);
        if ((f_ckind(v_p) == f_TK_LBRACE())) {
            return mkv_SLoop(mkv_Num(1), f_parse_block(v_p));
        }
        if ((f_ckind(v_p) == f_TK_LPAREN())) {
            f_adv(v_p);
            v_kn = f_ctext(v_p);
            f_adv(v_p);
            f_eat(v_p, f_TK_COMMA());
            v_vn = f_ctext(v_p);
            f_adv(v_p);
            f_eat(v_p, f_TK_RPAREN());
            if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "in") == 0))) {
                f_adv(v_p);
            }
            v_coll = f_parse_expr(v_p);
            return mkv_SLoopKV(v_kn, v_vn, v_coll, f_parse_block(v_p));
        }
        if ((((f_ckind(v_p) == f_TK_IDENT()) && ((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind == f_TK_IDENT())) && (strcmp((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).text, "in") == 0))) {
            v_vnm = f_ctext(v_p);
            f_adv(v_p);
            f_adv(v_p);
            v_coll = f_parse_expr(v_p);
            if ((f_ckind(v_p) == f_TK_DOTDOT())) {
                f_adv(v_p);
                v_incl = (1 != 1);
                if ((f_ckind(v_p) == f_TK_ASSIGN())) {
                    f_adv(v_p);
                    v_incl = (1 == 1);
                }
                v_hi = f_parse_expr(v_p);
                if (v_incl) {
                    v_hi = mkv_Bin(f_OP_ADD(), v_hi, mkv_Num(1));
                }
                return mkv_SLoopRange(v_vnm, v_coll, v_hi, f_parse_block(v_p));
            }
            return mkv_SLoopIn(v_vnm, v_coll, f_parse_block(v_p));
        }
        v_cond = f_parse_expr(v_p);
        return mkv_SLoop(v_cond, f_parse_block(v_p));
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "println") == 0))) {
        f_adv(v_p);
        f_eat(v_p, f_TK_LPAREN());
        v_e = f_parse_expr(v_p);
        f_eat(v_p, f_TK_RPAREN());
        return mkv_SPrint(v_e, v_spos);
    }
    if (((v_k == f_TK_IDENT()) && (strcmp(v_t, "mu") == 0))) {
        f_adv(v_p);
        if (f_is_destructure_ahead(v_p)) {
            return f_parse_destructure(v_p);
        }
        v_name = f_ctext(v_p);
        f_adv(v_p);
        v_ann = "";
        if ((f_ckind(v_p) == f_TK_COLON())) {
            f_adv(v_p);
            v_ann = f_parse_type(v_p);
        }
        f_eat(v_p, f_TK_WALRUS());
        return mkv_SDecl(v_name, f_parse_decl_rhs(v_p, v_ann), v_spos);
    }
    if (f_is_destructure_ahead(v_p)) {
        return f_parse_destructure(v_p);
    }
    if (((v_k == f_TK_IDENT()) && ((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind == f_TK_COLON()))) {
        v_name = v_t;
        f_adv(v_p);
        f_adv(v_p);
        v_ann = f_parse_type(v_p);
        if ((f_ckind(v_p) == f_TK_WALRUS())) {
            f_adv(v_p);
        } else {
            f_eat(v_p, f_TK_ASSIGN());
        }
        return mkv_SDecl(v_name, f_parse_decl_rhs(v_p, v_ann), v_spos);
    }
    if (((v_k == f_TK_IDENT()) && ((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind == f_TK_WALRUS()))) {
        v_name = v_t;
        f_adv(v_p);
        f_adv(v_p);
        return mkv_SDecl(v_name, f_parse_expr(v_p), v_spos);
    }
    if (((v_k == f_TK_IDENT()) && ((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind == f_TK_ASSIGN()))) {
        v_name = v_t;
        f_adv(v_p);
        f_adv(v_p);
        return mkv_SAssign(v_name, f_parse_expr(v_p), v_spos);
    }
    if ((((v_k == f_TK_IDENT()) && (f_compound_op((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind) > 0)) && ((arr_Token_get((v_p)->toks, ((v_p)->pos + 2))).kind == f_TK_ASSIGN()))) {
        v_name = v_t;
        v_cop = f_compound_op((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind);
        f_adv(v_p);
        f_adv(v_p);
        f_adv(v_p);
        return mkv_SAssign(v_name, mkv_Bin(v_cop, mkv_Var(v_name), f_parse_expr(v_p)), v_spos);
    }
    if (((v_k == f_TK_IDENT()) && (((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind == f_TK_LBRACK()) || ((arr_Token_get((v_p)->toks, ((v_p)->pos + 1))).kind == f_TK_DOT())))) {
        if (f_is_lvalue_assign(v_p)) {
            v_lhs = f_parse_factor(v_p);
            if ((f_ckind(v_p) == f_TK_ASSIGN())) {
                f_adv(v_p);
                return f_mk_lvalue_assign(v_lhs, f_parse_expr(v_p), v_spos);
            }
            v_cop = f_compound_op(f_ckind(v_p));
            f_adv(v_p);
            f_adv(v_p);
            return f_mk_lvalue_assign(v_lhs, mkv_Bin(v_cop, v_lhs, f_parse_expr(v_p)), v_spos);
        }
    }
    return mkv_SExpr(f_parse_expr(v_p), v_spos);
}

int64_t f_compound_op(int64_t v_k) {
    if ((v_k == f_TK_PLUS())) {
        return f_OP_ADD();
    }
    if ((v_k == f_TK_MINUS())) {
        return f_OP_SUB();
    }
    if ((v_k == f_TK_STAR())) {
        return f_OP_MUL();
    }
    if ((v_k == f_TK_SLASH())) {
        return f_OP_DIV();
    }
    if ((v_k == f_TK_PERCENT())) {
        return f_OP_MOD();
    }
    return 0;
}

int64_t f_is_lvalue_assign(s_P* v_p) {
    int64_t v_i;
    int64_t v_kk;
    int64_t v_d;
    int64_t v_ik;
    v_i = ((v_p)->pos + 1);
    while ((v_i < arr_Token_len((v_p)->toks))) {
        v_kk = (arr_Token_get((v_p)->toks, v_i)).kind;
        if ((((v_kk == f_TK_DOT()) && ((v_i + 1) < arr_Token_len((v_p)->toks))) && ((arr_Token_get((v_p)->toks, (v_i + 1))).kind == f_TK_IDENT()))) {
            v_i = (v_i + 2);
        } else {
            if ((v_kk == f_TK_LBRACK())) {
                v_d = 1;
                v_i = (v_i + 1);
                while (((v_i < arr_Token_len((v_p)->toks)) && (v_d > 0))) {
                    v_ik = (arr_Token_get((v_p)->toks, v_i)).kind;
                    if ((v_ik == f_TK_LBRACK())) {
                        v_d = (v_d + 1);
                    } else {
                        if ((v_ik == f_TK_RBRACK())) {
                            v_d = (v_d - 1);
                        }
                    }
                    v_i = (v_i + 1);
                }
            } else {
                break;
            }
        }
    }
    if ((v_i >= arr_Token_len((v_p)->toks))) {
        return (1 != 1);
    }
    if (((arr_Token_get((v_p)->toks, v_i)).kind == f_TK_ASSIGN())) {
        return (1 == 1);
    }
    if ((((f_compound_op((arr_Token_get((v_p)->toks, v_i)).kind) > 0) && ((v_i + 1) < arr_Token_len((v_p)->toks))) && ((arr_Token_get((v_p)->toks, (v_i + 1))).kind == f_TK_ASSIGN()))) {
        return (1 == 1);
    }
    return (1 != 1);
}

s_Stmt f_mk_lvalue_assign(s_Expr v_lhs, s_Expr v_rhs, int64_t v_spos) {
    return ({ s_Stmt __m; s_Expr __s = v_lhs; if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = mkv_SIdxAssign(v_obj, v_idx, v_rhs, v_spos); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = mkv_SFieldAssign(v_obj, v_fnm, v_rhs, v_spos); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==10){ const char* v_mlt = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = mkv_SExpr(v_lhs, v_spos); } else if(__s.tag==18){ __m = mkv_SExpr(v_lhs, v_spos); } __m; });
}

s_Expr f_parse_decl_rhs(s_P* v_p, const char* v_ann) {
    s_Expr v_e;
    v_e = f_parse_expr(v_p);
    if (f_is_array_ann(v_ann)) {
        return f_stamp_array_ann(v_e, f_elem_of_ann(v_ann));
    }
    if (f_is_map_ann(v_ann)) {
        return f_stamp_map_ann(v_e, v_ann);
    }
    return v_e;
}

s_Expr f_stamp_array_ann(s_Expr v_e, const char* v_ety) {
    return ({ s_Expr __m; s_Expr __s = v_e; if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_old = __s.u.Array.f1; __m = f_stamp_if_empty(v_elems, v_old, v_ety); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = v_e; } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = v_e; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = v_e; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = v_e; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = v_e; } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = v_e; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = v_e; } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = v_e; } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = v_e; } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = v_e; } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_i = *(__s.u.Index.f1); __m = v_e; } else if(__s.tag==10){ const char* v_mlt = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = v_e; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = v_e; } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = v_e; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = v_e; } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = v_e; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = v_e; } else if(__s.tag==18){ __m = v_e; } __m; });
}

s_Expr f_stamp_map_ann(s_Expr v_e, const char* v_mty) {
    return ({ s_Expr __m; s_Expr __s = v_e; if(__s.tag==10){ const char* v_old = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = mkv_MapLit(v_mty, v_mks, v_mvs); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = v_e; } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = v_e; } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_old = __s.u.Array.f1; __m = v_e; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = v_e; } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = v_e; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = v_e; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = v_e; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = v_e; } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = v_e; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = v_e; } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = v_e; } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = v_e; } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = v_e; } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_i = *(__s.u.Index.f1); __m = v_e; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = v_e; } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = v_e; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = v_e; } else if(__s.tag==18){ __m = v_e; } __m; });
}

s_Expr f_stamp_if_empty(arr_Expr v_elems, const char* v_old, const char* v_ety) {
    if ((arr_Expr_len(v_elems) == 0)) {
        return mkv_Array(v_elems, v_ety);
    }
    return mkv_Array(v_elems, v_old);
}

s_StructDef f_parse_struct(s_P* v_p) {
    const char* v_name;
    arr_str v_fnames;
    arr_str v_ftypes;
    f_adv(v_p);
    v_name = f_ctext(v_p);
    f_adv(v_p);
    f_eat(v_p, f_TK_LBRACE());
    v_fnames = ({ arr_str __a = arr_str_new(); __a; });
    v_ftypes = ({ arr_str __a = arr_str_new(); __a; });
    while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
        if ((f_ckind(v_p) == f_TK_IDENT())) {
            v_fnames = arr_str_push(v_fnames, f_ctext(v_p));
            f_adv(v_p);
            f_eat(v_p, f_TK_COLON());
            v_ftypes = arr_str_push(v_ftypes, f_parse_type(v_p));
        } else {
            f_adv(v_p);
        }
    }
    f_eat(v_p, f_TK_RBRACE());
    return mk_StructDef(v_name, v_fnames, v_ftypes);
}

s_Func f_parse_method(s_P* v_p, const char* v_cls) {
    const char* v_name;
    arr_str v_pnames;
    arr_str v_ptypes;
    int64_t v_first;
    const char* v_pn;
    const char* v_ret;
    arr_str v_mtp;
    f_adv(v_p);
    v_name = f_ctext(v_p);
    f_adv(v_p);
    f_eat(v_p, f_TK_LPAREN());
    v_pnames = ({ arr_str __a = arr_str_new(); __a; });
    v_ptypes = ({ arr_str __a = arr_str_new(); __a; });
    v_first = (1 == 1);
    while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
        if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "mu") == 0))) {
            f_adv(v_p);
        }
        if ((f_ckind(v_p) == f_TK_IDENT())) {
            v_pn = f_ctext(v_p);
            f_adv(v_p);
            v_pnames = arr_str_push(v_pnames, v_pn);
            if ((f_ckind(v_p) == f_TK_COLON())) {
                f_adv(v_p);
                v_ptypes = arr_str_push(v_ptypes, f_parse_type(v_p));
            } else {
                if ((v_first && (strcmp(v_pn, "self") == 0))) {
                    v_ptypes = arr_str_push(v_ptypes, scat("*", v_cls));
                } else {
                    v_ptypes = arr_str_push(v_ptypes, "i64");
                }
            }
            v_first = (1 != 1);
        }
        if ((f_ckind(v_p) == f_TK_COMMA())) {
            f_adv(v_p);
        }
    }
    f_eat(v_p, f_TK_RPAREN());
    v_ret = "";
    if ((f_ckind(v_p) == f_TK_MINUS())) {
        f_adv(v_p);
    }
    if ((f_ckind(v_p) == f_TK_GT())) {
        f_adv(v_p);
        v_ret = f_parse_type(v_p);
    }
    v_mtp = ({ arr_str __a = arr_str_new(); __a; });
    return mk_Func(v_name, v_pnames, v_ptypes, v_ret, "", v_mtp, f_parse_block(v_p));
}

s_ClassDef f_parse_class(s_P* v_p) {
    const char* v_name;
    const char* v_parent;
    arr_str v_fnames;
    arr_str v_ftypes;
    arr_Func v_methods;
    arr_str v_vflags;
    f_adv(v_p);
    v_name = f_ctext(v_p);
    f_adv(v_p);
    v_parent = "";
    if ((f_ckind(v_p) == f_TK_COLON())) {
        f_adv(v_p);
        v_parent = f_ctext(v_p);
        f_adv(v_p);
    }
    f_eat(v_p, f_TK_LBRACE());
    v_fnames = ({ arr_str __a = arr_str_new(); __a; });
    v_ftypes = ({ arr_str __a = arr_str_new(); __a; });
    v_methods = ({ arr_Func __a = arr_Func_new(); __a; });
    v_vflags = ({ arr_str __a = arr_str_new(); __a; });
    while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
        if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "vt") == 0))) {
            f_adv(v_p);
            v_methods = arr_Func_push(v_methods, f_parse_method(v_p, v_name));
            v_vflags = arr_str_push(v_vflags, "1");
        } else {
            if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "fn") == 0))) {
                v_methods = arr_Func_push(v_methods, f_parse_method(v_p, v_name));
                v_vflags = arr_str_push(v_vflags, "0");
            } else {
                if ((f_ckind(v_p) == f_TK_IDENT())) {
                    v_fnames = arr_str_push(v_fnames, f_ctext(v_p));
                    f_adv(v_p);
                    f_eat(v_p, f_TK_COLON());
                    v_ftypes = arr_str_push(v_ftypes, f_parse_type(v_p));
                } else {
                    f_adv(v_p);
                }
            }
        }
    }
    f_eat(v_p, f_TK_RBRACE());
    return mk_ClassDef(v_name, v_parent, v_fnames, v_ftypes, v_methods, v_vflags);
}

s_EnumDef f_parse_enum(s_P* v_p) {
    const char* v_name;
    arr_str v_vnames;
    arr_str v_vftypes;
    const char* v_vn;
    const char* v_fts;
    int64_t v_first;
    const char* v_ft;
    f_adv(v_p);
    v_name = f_ctext(v_p);
    f_adv(v_p);
    f_eat(v_p, f_TK_LBRACE());
    v_vnames = ({ arr_str __a = arr_str_new(); __a; });
    v_vftypes = ({ arr_str __a = arr_str_new(); __a; });
    while (((f_ckind(v_p) != f_TK_RBRACE()) && (f_ckind(v_p) != f_TK_EOF()))) {
        if ((f_ckind(v_p) == f_TK_IDENT())) {
            v_vn = f_ctext(v_p);
            f_adv(v_p);
            v_fts = "";
            if ((f_ckind(v_p) == f_TK_LPAREN())) {
                f_adv(v_p);
                v_first = (1 == 1);
                while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
                    if ((f_ckind(v_p) == f_TK_IDENT())) {
                        f_adv(v_p);
                    }
                    f_eat(v_p, f_TK_COLON());
                    v_ft = f_parse_type(v_p);
                    if ((strcmp(v_ft, v_name) == 0)) {
                        v_ft = "@self";
                    }
                    if (v_first) {
                        v_fts = v_ft;
                    } else {
                        v_fts = scat(scat(v_fts, ";"), v_ft);
                    }
                    v_first = (1 != 1);
                    if ((f_ckind(v_p) == f_TK_COMMA())) {
                        f_adv(v_p);
                    }
                }
                f_eat(v_p, f_TK_RPAREN());
            }
            v_vnames = arr_str_push(v_vnames, v_vn);
            v_vftypes = arr_str_push(v_vftypes, v_fts);
            if ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
            }
        } else {
            f_adv(v_p);
        }
    }
    f_eat(v_p, f_TK_RBRACE());
    return mk_EnumDef(v_name, v_vnames, v_vftypes);
}

int64_t f_is_boxed_ft(const char* v_ft) {
    if ((strcmp(v_ft, "@self") == 0)) {
        return (1 == 1);
    }
    return ((((int64_t)strlen(v_ft)) >= 5) && (strcmp(substr(v_ft, 0, 5), "@box:") == 0));
}

const char* f_boxed_enum(const char* v_ft, const char* v_selfname) {
    if ((strcmp(v_ft, "@self") == 0)) {
        return v_selfname;
    }
    return substr(v_ft, 5, ((int64_t)strlen(v_ft)));
}

int64_t f_is_enum_name(arr_EnumDef v_enums, const char* v_nm) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_EnumDef_len(v_enums))) {
        if ((strcmp((arr_EnumDef_get(v_enums, v_i)).name, v_nm) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

int64_t f_is_struct_name(arr_StructDef v_structs, const char* v_nm) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_StructDef_len(v_structs))) {
        if ((strcmp((arr_StructDef_get(v_structs, v_i)).name, v_nm) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

int64_t f_is_prim_name(const char* v_nm) {
    return ((((((((((((((strcmp(v_nm, "i64") == 0) || (strcmp(v_nm, "i32") == 0)) || (strcmp(v_nm, "u32") == 0)) || (strcmp(v_nm, "f64") == 0)) || (strcmp(v_nm, "bool") == 0)) || (strcmp(v_nm, "str") == 0)) || (strcmp(v_nm, "bytes") == 0)) || (strcmp(v_nm, "i8") == 0)) || (strcmp(v_nm, "i16") == 0)) || (strcmp(v_nm, "u8") == 0)) || (strcmp(v_nm, "u16") == 0)) || (strcmp(v_nm, "u64") == 0)) || (strcmp(v_nm, "f32") == 0)) || (strcmp(v_nm, "void") == 0));
}

const char* f_mark_ctype(const char* v_ty, arr_StructDef v_structs, arr_EnumDef v_enums) {
    int64_t v_c;
    if ((((int64_t)strlen(v_ty)) == 0)) {
        return v_ty;
    }
    v_c = ((int64_t)(unsigned char)(v_ty)[0]);
    if (((((((v_c == 91) || (v_c == 123)) || (v_c == 42)) || (v_c == 33)) || (v_c == 64)) || (v_c == 40))) {
        return v_ty;
    }
    if (f_is_prim_name(v_ty)) {
        return v_ty;
    }
    if (f_is_struct_name(v_structs, v_ty)) {
        return v_ty;
    }
    if (f_is_enum_name(v_enums, v_ty)) {
        return v_ty;
    }
    return scat("@c:", v_ty);
}

arr_Func f_mark_extern_ctypes(arr_Func v_externs, arr_StructDef v_structs, arr_EnumDef v_enums) {
    arr_Func v_out;
    int64_t v_i;
    s_Func v_f;
    const char* v_nr;
    v_out = ({ arr_Func __a = arr_Func_new(); __a; });
    v_i = 0;
    while ((v_i < arr_Func_len(v_externs))) {
        v_f = arr_Func_get(v_externs, v_i);
        v_nr = f_mark_ctype((v_f).ret, v_structs, v_enums);
        v_out = arr_Func_push(v_out, mk_Func((v_f).name, (v_f).params, (v_f).ptypes, v_nr, (v_f).lib, (v_f).tparams, (v_f).body));
        v_i = (v_i + 1);
    }
    return v_out;
}

arr_EnumDef f_box_cross_enums(arr_EnumDef v_enums) {
    arr_EnumDef v_out;
    int64_t v_i;
    s_EnumDef v_ed;
    arr_str v_newvft;
    int64_t v_vi;
    const char* v_raw;
    arr_str v_fts;
    const char* v_nf;
    int64_t v_fi;
    const char* v_ft;
    v_out = ({ arr_EnumDef __a = arr_EnumDef_new(); __a; });
    v_i = 0;
    while ((v_i < arr_EnumDef_len(v_enums))) {
        v_ed = arr_EnumDef_get(v_enums, v_i);
        v_newvft = ({ arr_str __a = arr_str_new(); __a; });
        v_vi = 0;
        while ((v_vi < arr_str_len((v_ed).vftypes))) {
            v_raw = arr_str_get((v_ed).vftypes, v_vi);
            if ((((int64_t)strlen(v_raw)) == 0)) {
                v_newvft = arr_str_push(v_newvft, "");
            } else {
                v_fts = f_split_semi(v_raw);
                v_nf = "";
                v_fi = 0;
                while ((v_fi < arr_str_len(v_fts))) {
                    v_ft = arr_str_get(v_fts, v_fi);
                    if (((strcmp(v_ft, "@self") != 0) && f_is_enum_name(v_enums, v_ft))) {
                        v_ft = scat("@box:", v_ft);
                    }
                    if ((v_fi == 0)) {
                        v_nf = v_ft;
                    } else {
                        v_nf = scat(scat(v_nf, ";"), v_ft);
                    }
                    v_fi = (v_fi + 1);
                }
                v_newvft = arr_str_push(v_newvft, v_nf);
            }
            v_vi = (v_vi + 1);
        }
        v_out = arr_EnumDef_push(v_out, mk_EnumDef((v_ed).name, (v_ed).vnames, v_newvft));
        v_i = (v_i + 1);
    }
    return v_out;
}

const char* f_parse_type(s_P* v_p) {
    const char* v_inner;
    const char* v_kt;
    const char* v_vt;
    int64_t v_n;
    const char* v_sig;
    int64_t v_first;
    const char* v_ret;
    const char* v_t;
    if ((f_ckind(v_p) == f_TK_NOT())) {
        f_adv(v_p);
        return scat("!", f_parse_type(v_p));
    }
    if ((f_ckind(v_p) == f_TK_STAR())) {
        f_adv(v_p);
        return scat("*", f_parse_type(v_p));
    }
    if ((f_ckind(v_p) == f_TK_LBRACK())) {
        f_adv(v_p);
        v_inner = f_parse_type(v_p);
        f_eat(v_p, f_TK_RBRACK());
        return scat(scat("[", v_inner), "]");
    }
    if ((f_ckind(v_p) == f_TK_LBRACE())) {
        f_adv(v_p);
        v_kt = f_parse_type(v_p);
        f_eat(v_p, f_TK_COLON());
        v_vt = f_parse_type(v_p);
        f_eat(v_p, f_TK_RBRACE());
        return scat(scat(scat(scat("{", v_kt), ":"), v_vt), "}");
    }
    if ((f_ckind(v_p) == f_TK_LPAREN())) {
        f_adv(v_p);
        v_inner = "(";
        v_n = 0;
        while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
            if ((v_n > 0)) {
                v_inner = scat(v_inner, ";");
            }
            v_inner = scat(v_inner, f_parse_type(v_p));
            v_n = (v_n + 1);
            if ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
            }
        }
        f_eat(v_p, f_TK_RPAREN());
        if ((v_n == 1)) {
            return substr(v_inner, 1, ((int64_t)strlen(v_inner)));
        }
        return scat(v_inner, ")");
    }
    if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "fn") == 0))) {
        f_adv(v_p);
        f_eat(v_p, f_TK_LPAREN());
        v_sig = "@fn(";
        v_first = (1 == 1);
        while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
            if ((v_first == (1 != 1))) {
                v_sig = scat(v_sig, ",");
            }
            v_sig = scat(v_sig, f_parse_type(v_p));
            v_first = (1 != 1);
            if ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
            }
        }
        f_eat(v_p, f_TK_RPAREN());
        v_ret = "i64";
        if ((f_ckind(v_p) == f_TK_MINUS())) {
            f_adv(v_p);
        }
        if ((f_ckind(v_p) == f_TK_GT())) {
            f_adv(v_p);
            v_ret = f_parse_type(v_p);
        }
        return scat(scat(v_sig, ")"), v_ret);
    }
    if ((f_ckind(v_p) == f_TK_IDENT())) {
        v_t = f_ctext(v_p);
        f_adv(v_p);
        return v_t;
    }
    return "i64";
}

int64_t f_is_array_ann(const char* v_ty) {
    return ((((int64_t)strlen(v_ty)) > 0) && (((int64_t)(unsigned char)(v_ty)[0]) == 91));
}

const char* f_elem_of_ann(const char* v_ty) {
    return substr(v_ty, 1, (((int64_t)strlen(v_ty)) - 1));
}

int64_t f_is_map_ann(const char* v_ty) {
    return ((((int64_t)strlen(v_ty)) > 0) && (((int64_t)(unsigned char)(v_ty)[0]) == 123));
}

int64_t f_is_ptr_ann(const char* v_ty) {
    return ((((int64_t)strlen(v_ty)) > 0) && (((int64_t)(unsigned char)(v_ty)[0]) == 42));
}

const char* f_deref_ann(const char* v_ty) {
    return substr(v_ty, 1, ((int64_t)strlen(v_ty)));
}

int64_t f_is_result_ann(const char* v_ty) {
    return ((((int64_t)strlen(v_ty)) > 0) && (((int64_t)(unsigned char)(v_ty)[0]) == 33));
}

const char* f_result_inner(const char* v_ty) {
    return substr(v_ty, 1, ((int64_t)strlen(v_ty)));
}

int64_t f_is_fn_ann(const char* v_ty) {
    return ((((((int64_t)strlen(v_ty)) >= 3) && (((int64_t)(unsigned char)(v_ty)[0]) == 64)) && (((int64_t)(unsigned char)(v_ty)[1]) == 102)) && (((int64_t)(unsigned char)(v_ty)[2]) == 110));
}

const char* f_fn_ret_of(const char* v_sig) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_sig)))) {
        if ((((int64_t)(unsigned char)(v_sig)[v_i]) == 41)) {
            return substr(v_sig, (v_i + 1), ((int64_t)strlen(v_sig)));
        }
        v_i = (v_i + 1);
    }
    return "i64";
}

const char* f_fn_type_of(int64_t v_np, const char* v_ret) {
    const char* v_s;
    int64_t v_k;
    v_s = "@fn(";
    v_k = 0;
    while ((v_k < v_np)) {
        if ((v_k > 0)) {
            v_s = scat(v_s, ",");
        }
        v_s = scat(v_s, "i64");
        v_k = (v_k + 1);
    }
    return scat(scat(v_s, ")"), v_ret);
}

const char* f_lambda_type(s_Syms* v_sy, arr_str v_ps, arr_str v_pts, s_Expr v_b) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len(v_ps))) {
        if ((v_i < arr_str_len(v_pts))) {
            f_set_ty(v_sy, arr_str_get(v_ps, v_i), arr_str_get(v_pts, v_i));
        }
        v_i = (v_i + 1);
    }
    return f_fn_type_of_pts(v_pts, f_type_of_expr(v_sy, v_b));
}

const char* f_fn_type_of_pts(arr_str v_pts, const char* v_ret) {
    const char* v_s;
    int64_t v_k;
    v_s = "@fn(";
    v_k = 0;
    while ((v_k < arr_str_len(v_pts))) {
        if ((v_k > 0)) {
            v_s = scat(v_s, ",");
        }
        v_s = scat(v_s, arr_str_get(v_pts, v_k));
        v_k = (v_k + 1);
    }
    return scat(scat(v_s, ")"), v_ret);
}

arr_str f_fn_params_of(const char* v_sig) {
    arr_str v_out;
    int64_t v_i;
    int64_t v_depth;
    const char* v_cur;
    int64_t v_c;
    v_out = ({ arr_str __a = arr_str_new(); __a; });
    v_i = 4;
    v_depth = 0;
    v_cur = "";
    while ((v_i < ((int64_t)strlen(v_sig)))) {
        v_c = ((int64_t)(unsigned char)(v_sig)[v_i]);
        if ((v_c == 40)) {
            v_depth = (v_depth + 1);
            v_cur = scat(v_cur, "(");
        } else {
            if ((v_c == 41)) {
                if ((v_depth == 0)) {
                    break;
                }
                v_depth = (v_depth - 1);
                v_cur = scat(v_cur, ")");
            } else {
                if (((v_c == 44) && (v_depth == 0))) {
                    v_out = arr_str_push(v_out, v_cur);
                    v_cur = "";
                } else {
                    v_cur = scat(v_cur, substr(v_sig, v_i, (v_i + 1)));
                }
            }
        }
        v_i = (v_i + 1);
    }
    if ((((int64_t)strlen(v_cur)) > 0)) {
        v_out = arr_str_push(v_out, v_cur);
    }
    return v_out;
}

const char* f_under_ptr(const char* v_ty) {
    if (f_is_ptr_ann(v_ty)) {
        return f_deref_ann(v_ty);
    } else {
        return v_ty;
    }
}

int64_t f_is_tuple_ann(const char* v_ty) {
    return ((((int64_t)strlen(v_ty)) >= 2) && (((int64_t)(unsigned char)(v_ty)[0]) == 40));
}

arr_str f_tuple_elems(const char* v_ty) {
    arr_str v_out;
    const char* v_cur;
    int64_t v_i;
    v_out = ({ arr_str __a = arr_str_new(); __a; });
    v_cur = "";
    v_i = 1;
    while ((v_i < (((int64_t)strlen(v_ty)) - 1))) {
        if ((((int64_t)(unsigned char)(v_ty)[v_i]) == 59)) {
            v_out = arr_str_push(v_out, v_cur);
            v_cur = "";
        } else {
            v_cur = scat(v_cur, substr(v_ty, v_i, (v_i + 1)));
        }
        v_i = (v_i + 1);
    }
    v_out = arr_str_push(v_out, v_cur);
    return v_out;
}

const char* f_tuple_suffix(const char* v_ty) {
    arr_str v_es;
    const char* v_s;
    int64_t v_i;
    v_es = f_tuple_elems(v_ty);
    v_s = "";
    v_i = 0;
    while ((v_i < arr_str_len(v_es))) {
        if ((v_i > 0)) {
            v_s = scat(v_s, "_");
        }
        v_s = scat(v_s, f_arr_suffix(arr_str_get(v_es, v_i)));
        v_i = (v_i + 1);
    }
    return v_s;
}

s_Func f_parse_func(s_P* v_p) {
    const char* v_name;
    arr_str v_tparams;
    arr_str v_pnames;
    arr_str v_ptypes;
    const char* v_ret;
    f_adv(v_p);
    v_name = f_ctext(v_p);
    f_adv(v_p);
    v_tparams = ({ arr_str __a = arr_str_new(); __a; });
    if ((f_ckind(v_p) == f_TK_LT())) {
        f_adv(v_p);
        while (((f_ckind(v_p) != f_TK_GT()) && (f_ckind(v_p) != f_TK_EOF()))) {
            if ((f_ckind(v_p) == f_TK_IDENT())) {
                v_tparams = arr_str_push(v_tparams, f_ctext(v_p));
                f_adv(v_p);
            }
            if ((f_ckind(v_p) == f_TK_COMMA())) {
                f_adv(v_p);
            }
        }
        f_eat(v_p, f_TK_GT());
    }
    f_eat(v_p, f_TK_LPAREN());
    v_pnames = ({ arr_str __a = arr_str_new(); __a; });
    v_ptypes = ({ arr_str __a = arr_str_new(); __a; });
    while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
        if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "mu") == 0))) {
            f_adv(v_p);
        }
        if ((f_ckind(v_p) == f_TK_IDENT())) {
            v_pnames = arr_str_push(v_pnames, f_ctext(v_p));
            f_adv(v_p);
            if ((f_ckind(v_p) == f_TK_COLON())) {
                f_adv(v_p);
                v_ptypes = arr_str_push(v_ptypes, f_parse_type(v_p));
            } else {
                v_ptypes = arr_str_push(v_ptypes, "i64");
            }
        }
        if ((f_ckind(v_p) == f_TK_COMMA())) {
            f_adv(v_p);
        }
    }
    f_eat(v_p, f_TK_RPAREN());
    v_ret = "";
    if ((f_ckind(v_p) == f_TK_MINUS())) {
        f_adv(v_p);
    }
    if ((f_ckind(v_p) == f_TK_GT())) {
        f_adv(v_p);
        v_ret = f_parse_type(v_p);
    }
    return mk_Func(v_name, v_pnames, v_ptypes, v_ret, "", v_tparams, f_parse_block(v_p));
}

s_Func f_parse_extern(s_P* v_p) {
    const char* v_lib;
    const char* v_name;
    arr_str v_pnames;
    arr_str v_ptypes;
    const char* v_ret;
    arr_Stmt v_nobody;
    arr_str v_notp;
    f_adv(v_p);
    v_lib = "";
    if ((f_ckind(v_p) == f_TK_STR())) {
        v_lib = f_ctext(v_p);
        f_adv(v_p);
    }
    f_adv(v_p);
    v_name = f_ctext(v_p);
    f_adv(v_p);
    f_eat(v_p, f_TK_LPAREN());
    v_pnames = ({ arr_str __a = arr_str_new(); __a; });
    v_ptypes = ({ arr_str __a = arr_str_new(); __a; });
    while (((f_ckind(v_p) != f_TK_RPAREN()) && (f_ckind(v_p) != f_TK_EOF()))) {
        if ((f_ckind(v_p) == f_TK_DOTDOT())) {
            f_adv(v_p);
            if ((f_ckind(v_p) == f_TK_DOT())) {
                f_adv(v_p);
            }
            v_pnames = arr_str_push(v_pnames, "...");
            v_ptypes = arr_str_push(v_ptypes, "...");
        } else {
            if (((f_ckind(v_p) == f_TK_IDENT()) && (strcmp(f_ctext(v_p), "mu") == 0))) {
                f_adv(v_p);
            }
            if ((f_ckind(v_p) == f_TK_IDENT())) {
                v_pnames = arr_str_push(v_pnames, f_ctext(v_p));
                f_adv(v_p);
                if ((f_ckind(v_p) == f_TK_COLON())) {
                    f_adv(v_p);
                    v_ptypes = arr_str_push(v_ptypes, f_parse_type(v_p));
                } else {
                    v_ptypes = arr_str_push(v_ptypes, "i64");
                }
            }
        }
        if ((f_ckind(v_p) == f_TK_COMMA())) {
            f_adv(v_p);
        }
    }
    f_eat(v_p, f_TK_RPAREN());
    v_ret = "";
    if ((f_ckind(v_p) == f_TK_MINUS())) {
        f_adv(v_p);
    }
    if ((f_ckind(v_p) == f_TK_GT())) {
        f_adv(v_p);
        v_ret = f_parse_type(v_p);
    }
    v_nobody = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
    v_notp = ({ arr_str __a = arr_str_new(); __a; });
    return mk_Func(v_name, v_pnames, v_ptypes, v_ret, v_lib, v_notp, v_nobody);
}

const char* f_cty_proto(const char* v_ty) {
    if (f_is_c_type_ann(v_ty)) {
        return f_c_type_name(v_ty);
    }
    if ((strcmp(v_ty, "i32") == 0)) {
        return "int";
    }
    if ((strcmp(v_ty, "u32") == 0)) {
        return "unsigned int";
    }
    if ((strcmp(v_ty, "f64") == 0)) {
        return "double";
    }
    if ((strcmp(v_ty, "i64") == 0)) {
        return "int64_t";
    }
    if ((strcmp(v_ty, "bool") == 0)) {
        return "int";
    }
    if ((strcmp(v_ty, "str") == 0)) {
        return "const char*";
    }
    if ((strcmp(v_ty, "bytes") == 0)) {
        return "ailang_bytes";
    }
    if (f_is_ptr_ann(v_ty)) {
        return scat(f_cty_proto(f_deref_ann(v_ty)), "*");
    }
    return v_ty;
}

const char* f_gen_extern(s_Func v_f) {
    const char* v_rty;
    const char* v_out;
    const char* v_pr;
    int64_t v_pi;
    int64_t v_i;
    int64_t v_j;
    if (((arr_str_len((v_f).ptypes) > 0) && (strcmp(arr_str_get((v_f).ptypes, (arr_str_len((v_f).ptypes) - 1)), "...") == 0))) {
        return scat(scat(scat(scat(scat(scat("#undef ", (v_f).name), "\n#define f_"), (v_f).name), " "), (v_f).name), "\n");
    }
    v_rty = f_cty_ret((v_f).ret);
    v_out = scat(scat("#undef ", (v_f).name), "\n");
    v_pr = ({ const char* __r; if ((((int64_t)strlen((v_f).ret)) == 0)) { __r = "void"; } else { __r = f_cty_proto((v_f).ret); } __r; });
    v_out = scat(scat(scat(scat(scat(v_out, "extern "), v_pr), " "), (v_f).name), "(");
    v_pi = 0;
    while ((v_pi < arr_str_len((v_f).params))) {
        if ((v_pi > 0)) {
            v_out = scat(v_out, ", ");
        }
        v_out = scat(v_out, f_cty_proto(arr_str_get((v_f).ptypes, v_pi)));
        v_pi = (v_pi + 1);
    }
    if ((arr_str_len((v_f).params) == 0)) {
        v_out = scat(v_out, "void");
    }
    v_out = scat(v_out, ");\n");
    v_out = scat(scat(scat(scat(scat(v_out, "static "), v_rty), " f_"), (v_f).name), "(");
    v_i = 0;
    while ((v_i < arr_str_len((v_f).params))) {
        if ((v_i > 0)) {
            v_out = scat(v_out, ", ");
        }
        v_out = scat(scat(scat(v_out, f_cty(arr_str_get((v_f).ptypes, v_i))), " a"), i2s(v_i));
        v_i = (v_i + 1);
    }
    if ((arr_str_len((v_f).params) == 0)) {
        v_out = scat(v_out, "void");
    }
    v_out = scat(v_out, "){ ");
    if ((((int64_t)strlen((v_f).ret)) > 0)) {
        v_out = scat(scat(scat(v_out, "return ("), v_rty), ")");
    }
    v_out = scat(scat(v_out, (v_f).name), "(");
    v_j = 0;
    while ((v_j < arr_str_len((v_f).params))) {
        if ((v_j > 0)) {
            v_out = scat(v_out, ", ");
        }
        v_out = scat(scat(v_out, "a"), i2s(v_j));
        v_j = (v_j + 1);
    }
    return scat(v_out, "); }\n");
}

const char* f_op_c(int64_t v_op) {
    if ((v_op == f_OP_ADD())) {
        return " + ";
    }
    if ((v_op == f_OP_SUB())) {
        return " - ";
    }
    if ((v_op == f_OP_MUL())) {
        return " * ";
    }
    if ((v_op == f_OP_DIV())) {
        return " / ";
    }
    if ((v_op == f_OP_MOD())) {
        return " % ";
    }
    if ((v_op == f_OP_LT())) {
        return " < ";
    }
    if ((v_op == f_OP_GT())) {
        return " > ";
    }
    if ((v_op == f_OP_LE())) {
        return " <= ";
    }
    if ((v_op == f_OP_GE())) {
        return " >= ";
    }
    if ((v_op == f_OP_EQ())) {
        return " == ";
    }
    if ((v_op == f_OP_NE())) {
        return " != ";
    }
    if ((v_op == f_OP_AND())) {
        return " && ";
    }
    if ((v_op == f_OP_OR())) {
        return " || ";
    }
    if ((v_op == f_OP_SHL())) {
        return " << ";
    }
    if ((v_op == f_OP_SHR())) {
        return " >> ";
    }
    if ((v_op == f_OP_BAND())) {
        return " & ";
    }
    if ((v_op == f_OP_BXOR())) {
        return " ^ ";
    }
    if ((v_op == f_OP_BOR())) {
        return " | ";
    }
    return " ? ";
}

const char* f_ty_of(s_Syms* v_sy, const char* v_name) {
    if (map_str_str_has((v_sy)->vty, v_name)) {
        return map_str_str_get((v_sy)->vty, v_name);
    }
    if (f_is_variant(v_sy, v_name)) {
        return map_str_str_get((v_sy)->evar, v_name);
    }
    return "i64";
}

int64_t f_declared(s_Syms* v_sy, const char* v_name) {
    return map_str_str_has((v_sy)->vty, v_name);
}

void f_set_ty(s_Syms* v_sy, const char* v_name, const char* v_ty) {
    map_str_str_set((v_sy)->vty, v_name, v_ty);
}

int64_t f_is_ctor(s_Syms* v_sy, const char* v_name) {
    return map_str_str_has((v_sy)->ctors, v_name);
}

int64_t f_is_variant(s_Syms* v_sy, const char* v_name) {
    return map_str_str_has((v_sy)->evar, v_name);
}

const char* f_fkey(const char* v_sname, const char* v_fname) {
    return scat(scat(v_sname, "."), v_fname);
}

const char* f_arr_suffix(const char* v_ety) {
    if ((strcmp(v_ety, "bool") == 0)) {
        return "i64";
    } else {
        return v_ety;
    }
}

int64_t f_is_c_type_ann(const char* v_ty) {
    return ((((((int64_t)strlen(v_ty)) >= 3) && (((int64_t)(unsigned char)(v_ty)[0]) == 64)) && (((int64_t)(unsigned char)(v_ty)[1]) == 99)) && (((int64_t)(unsigned char)(v_ty)[2]) == 58));
}

const char* f_c_type_name(const char* v_ty) {
    return substr(v_ty, 3, ((int64_t)strlen(v_ty)));
}

const char* f_cty(const char* v_ty) {
    if ((strcmp(v_ty, "@vtp") == 0)) {
        return "void*";
    }
    if (f_is_c_type_ann(v_ty)) {
        return f_c_type_name(v_ty);
    }
    if ((strcmp(v_ty, "i64") == 0)) {
        return "int64_t";
    }
    if ((strcmp(v_ty, "bool") == 0)) {
        return "int64_t";
    }
    if ((strcmp(v_ty, "i32") == 0)) {
        return "int64_t";
    }
    if ((strcmp(v_ty, "u32") == 0)) {
        return "uint32_t";
    }
    if ((strcmp(v_ty, "f64") == 0)) {
        return "double";
    }
    if ((strcmp(v_ty, "bytes") == 0)) {
        return "ailang_bytes";
    }
    if ((strcmp(v_ty, "str") == 0)) {
        return "const char*";
    }
    if (f_is_array_ann(v_ty)) {
        return scat("arr_", f_arr_suffix(f_elem_of_ann(v_ty)));
    }
    if (f_is_map_ann(v_ty)) {
        return f_map_cty(v_ty);
    }
    if (f_is_ptr_ann(v_ty)) {
        return scat(f_cty(f_deref_ann(v_ty)), "*");
    }
    if (f_is_result_ann(v_ty)) {
        return scat("res_", f_arr_suffix(f_result_inner(v_ty)));
    }
    if (f_is_fn_ann(v_ty)) {
        return "closure_t";
    }
    if (f_is_tuple_ann(v_ty)) {
        return scat("tup_", f_tuple_suffix(v_ty));
    }
    return scat("s_", v_ty);
}

const char* f_map_vtype(const char* v_ty) {
    int64_t v_i;
    v_i = 1;
    while ((v_i < ((int64_t)strlen(v_ty)))) {
        if ((((int64_t)(unsigned char)(v_ty)[v_i]) == 58)) {
            break;
        }
        v_i = (v_i + 1);
    }
    return substr(v_ty, (v_i + 1), (((int64_t)strlen(v_ty)) - 1));
}

const char* f_map_ktype(const char* v_ty) {
    int64_t v_i;
    v_i = 1;
    while ((v_i < ((int64_t)strlen(v_ty)))) {
        if ((((int64_t)(unsigned char)(v_ty)[v_i]) == 58)) {
            break;
        }
        v_i = (v_i + 1);
    }
    return substr(v_ty, 1, v_i);
}

const char* f_map_cty(const char* v_ty) {
    return f_map_nm(f_map_ktype(v_ty), f_map_vtype(v_ty));
}

const char* f_type_of_expr(s_Syms* v_sy, s_Expr v_e) {
    return ({ const char* __m; s_Expr __s = v_e; if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = "i64"; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = "f64"; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = "str"; } else if(__s.tag==3){ const char* v_name = __s.u.Var.f0; __m = f_ty_of(v_sy, v_name); } else if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_bin_type(v_sy, v_op, v_l, v_r); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = "i64"; } else if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_call_type_a(v_sy, v_fname, v_args); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_field_type(v_sy, v_obj, v_fnm); } else if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_index_type(v_sy, v_obj); } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = f_array_type_of(v_sy, v_elems, v_ety); } else if(__s.tag==10){ const char* v_mty = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_maplit_type(v_sy, v_mty, v_mks, v_mvs); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = scat("*", f_type_of_expr(v_sy, v_x)); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_match_type(v_sy, v_bd); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = f_type_of_expr(v_sy, v_t); } else if(__s.tag==14){ s_Expr v_e = *(__s.u.Try.f0); __m = f_result_inner(f_type_of_expr(v_sy, v_e)); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = f_lambda_type(v_sy, v_ps, v_pts, v_b); } else if(__s.tag==16){ arr_Expr v_elems = __s.u.Tuple.f0; __m = f_tuple_type_of(v_sy, v_elems); } else if(__s.tag==17){ arr_Stmt v_body = __s.u.BlockE.f0; __m = f_block_type(v_sy, v_body); } else if(__s.tag==18){ __m = "i64"; } __m; });
}

const char* f_match_type(s_Syms* v_sy, arr_Expr v_bodies) {
    if ((arr_Expr_len(v_bodies) > 0)) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_bodies, 0));
    }
    return "i64";
}

const char* f_index_type(s_Syms* v_sy, s_Expr v_obj) {
    const char* v_t;
    v_t = f_type_of_expr(v_sy, v_obj);
    if (f_is_array_ann(v_t)) {
        return f_elem_of_ann(v_t);
    }
    if (f_is_map_ann(v_t)) {
        return f_map_vtype(v_t);
    }
    return "i64";
}

const char* f_array_type_of(s_Syms* v_sy, arr_Expr v_elems, const char* v_ety) {
    if ((((int64_t)strlen(v_ety)) > 0)) {
        return scat(scat("[", v_ety), "]");
    }
    if ((arr_Expr_len(v_elems) > 0)) {
        return scat(scat("[", f_type_of_expr(v_sy, arr_Expr_get(v_elems, 0))), "]");
    }
    return "[i64]";
}

const char* f_map_lit_type(const char* v_mty) {
    if ((((int64_t)strlen(v_mty)) > 0)) {
        return v_mty;
    }
    return "{str:str}";
}

const char* f_bin_type(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r) {
    if ((v_op == f_OP_CAT())) {
        return "str";
    }
    if ((v_op == f_OP_QQ())) {
        return f_type_of_expr(v_sy, v_l);
    }
    if ((((v_op == f_OP_ADD()) && (strcmp(f_type_of_expr(v_sy, v_l), "str") == 0)) && (strcmp(f_type_of_expr(v_sy, v_r), "str") == 0))) {
        return "str";
    }
    if (((((v_op == f_OP_ADD()) || (v_op == f_OP_SUB())) || (v_op == f_OP_MUL())) || (v_op == f_OP_DIV()))) {
        if (((strcmp(f_type_of_expr(v_sy, v_l), "f64") == 0) || (strcmp(f_type_of_expr(v_sy, v_r), "f64") == 0))) {
            return "f64";
        }
    }
    if (((v_op >= f_OP_LT()) && (v_op <= f_OP_OR()))) {
        return "bool";
    }
    return "i64";
}

int64_t f_is_native_call(const char* v_fname) {
    if ((((((((((((strcmp(v_fname, "tls_server_ctx") == 0) || (strcmp(v_fname, "tls_client_ctx") == 0)) || (strcmp(v_fname, "tls_free_ctx") == 0)) || (strcmp(v_fname, "tls_accept") == 0)) || (strcmp(v_fname, "tls_connect_fd") == 0)) || (strcmp(v_fname, "tls_send") == 0)) || (strcmp(v_fname, "tls_send_str") == 0)) || (strcmp(v_fname, "tls_recv") == 0)) || (strcmp(v_fname, "tls_close") == 0)) || (strcmp(v_fname, "tls_error") == 0)) || (strcmp(v_fname, "sha1") == 0))) {
        return (1 == 1);
    }
    if ((((((((((((((((strcmp(v_fname, "pg_connect") == 0) || (strcmp(v_fname, "pg_status") == 0)) || (strcmp(v_fname, "pg_error") == 0)) || (strcmp(v_fname, "pg_close") == 0)) || (strcmp(v_fname, "pg_exec") == 0)) || (strcmp(v_fname, "pg_ok") == 0)) || (strcmp(v_fname, "pg_result_error") == 0)) || (strcmp(v_fname, "pg_clear") == 0)) || (strcmp(v_fname, "pg_nrows") == 0)) || (strcmp(v_fname, "pg_ncols") == 0)) || (strcmp(v_fname, "pg_value") == 0)) || (strcmp(v_fname, "pg_isnull") == 0)) || (strcmp(v_fname, "pg_col_name") == 0)) || (strcmp(v_fname, "pg_affected") == 0)) || (strcmp(v_fname, "pg_escape") == 0))) {
        return (1 == 1);
    }
    return (1 != 1);
}

const char* f_call_type_a(s_Syms* v_sy, const char* v_fname, arr_Expr v_args) {
    s_Func v_f;
    const char* v_gt;
    const char* v_pdef;
    const char* v_rcv;
    const char* v_mk;
    const char* v_et;
    arr_str v_ps;
    if (f_is_generic(v_sy, v_fname)) {
        v_f = f_find_gfn(v_sy, v_fname);
        v_gt = f_generic_T(v_sy, v_f, v_args);
        if (f_is_tparam(v_f, (v_f).ret)) {
            return v_gt;
        }
        return (v_f).ret;
    }
    if ((((arr_Expr_len(v_args) >= 1) && (strcmp(f_var_name(arr_Expr_get(v_args, 0)), "super") == 0)) && map_str_str_has((v_sy)->evar, "@curclass"))) {
        v_pdef = f_defining_class(v_sy, f_class_parent(v_sy, map_str_str_get((v_sy)->evar, "@curclass")), v_fname);
        if (map_str_str_has((v_sy)->frets, scat(scat(v_pdef, "_"), v_fname))) {
            return map_str_str_get((v_sy)->frets, scat(scat(v_pdef, "_"), v_fname));
        }
    }
    if ((arr_Expr_len(v_args) >= 1)) {
        v_rcv = f_under_ptr(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)));
        if ((map_str_str_has((v_sy)->evar, scat("@class.", v_rcv)) && f_is_class_method(v_sy, v_rcv, v_fname))) {
            v_mk = scat(scat(f_defining_class(v_sy, v_rcv, v_fname), "_"), v_fname);
            if (map_str_str_has((v_sy)->frets, v_mk)) {
                return map_str_str_get((v_sy)->frets, v_mk);
            }
        }
    }
    if (((strcmp(v_fname, "map") == 0) && (arr_Expr_len(v_args) == 2))) {
        v_et = f_elem_of_ann(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)));
        v_ps = f_lam_params(arr_Expr_get(v_args, 1));
        f_set_ty(v_sy, arr_str_get(v_ps, 0), v_et);
        return scat(scat("[", f_type_of_expr(v_sy, f_lam_body(arr_Expr_get(v_args, 1)))), "]");
    }
    if (((strcmp(v_fname, "filter") == 0) && (arr_Expr_len(v_args) == 2))) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
    }
    if (((strcmp(v_fname, "reduce") == 0) && (arr_Expr_len(v_args) == 3))) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_args, 1));
    }
    if ((strcmp(v_fname, "to_str") == 0)) {
        return "str";
    }
    if ((strcmp(v_fname, "cstr") == 0)) {
        return "str";
    }
    if (((strcmp(v_fname, "print") == 0) || (strcmp(v_fname, "println") == 0))) {
        return "i64";
    }
    if ((strcmp(v_fname, "int_to_str") == 0)) {
        return "str";
    }
    if ((strcmp(v_fname, "str_to_int") == 0)) {
        return "i64";
    }
    if ((strcmp(v_fname, "float_to_str") == 0)) {
        return "str";
    }
    if ((strcmp(v_fname, "str_to_float") == 0)) {
        return "f64";
    }
    if ((strcmp(v_fname, "substring") == 0)) {
        return "str";
    }
    if ((strcmp(v_fname, "read_file") == 0)) {
        return "str";
    }
    if ((strcmp(v_fname, "write_file") == 0)) {
        return "bool";
    }
    if ((strcmp(v_fname, "str_to_bytes") == 0)) {
        return "bytes";
    }
    if ((strcmp(v_fname, "bytes_to_str") == 0)) {
        return "str";
    }
    if ((strcmp(v_fname, "bytes_at") == 0)) {
        return "i64";
    }
    if ((strcmp(v_fname, "bytes_slice") == 0)) {
        return "bytes";
    }
    if ((strcmp(v_fname, "read_file_bytes") == 0)) {
        return "bytes";
    }
    if ((strcmp(v_fname, "write_file_bytes") == 0)) {
        return "i64";
    }
    if ((strcmp(v_fname, "args") == 0)) {
        return "[str]";
    }
    if ((strcmp(v_fname, "len") == 0)) {
        return "i64";
    }
    if (((strcmp(v_fname, "ok") == 0) && (arr_Expr_len(v_args) > 0))) {
        return scat("!", f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)));
    }
    if (((strcmp(v_fname, "err") == 0) && (arr_Expr_len(v_args) > 0))) {
        return scat("!", f_ty_of(v_sy, "@ret"));
    }
    if (((strcmp(v_fname, "unwrap") == 0) && (arr_Expr_len(v_args) > 0))) {
        return f_result_inner(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)));
    }
    if (((strcmp(v_fname, "is_ok") == 0) || (strcmp(v_fname, "is_err") == 0))) {
        return "bool";
    }
    if ((strcmp(v_fname, "err_msg") == 0)) {
        return "str";
    }
    if (((((int64_t)strlen(v_fname)) > 4) && (strcmp(substr(v_fname, 0, 4), "err_") == 0))) {
        return scat("!", substr(v_fname, 4, ((int64_t)strlen(v_fname))));
    }
    if (((strcmp(v_fname, "keys") == 0) && (arr_Expr_len(v_args) > 0))) {
        return scat(scat("[", f_map_ktype(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)))), "]");
    }
    if (((strcmp(v_fname, "values") == 0) && (arr_Expr_len(v_args) > 0))) {
        return scat(scat("[", f_map_vtype(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)))), "]");
    }
    if ((((((strcmp(v_fname, "push") == 0) || (strcmp(v_fname, "slice") == 0)) || (strcmp(v_fname, "reverse") == 0)) || (strcmp(v_fname, "sort") == 0)) && (arr_Expr_len(v_args) > 0))) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
    }
    if ((((((strcmp(v_fname, "starts_with") == 0) || (strcmp(v_fname, "ends_with") == 0)) || (strcmp(v_fname, "contains") == 0)) || (strcmp(v_fname, "str_to_bool") == 0)) || (strcmp(v_fname, "has") == 0))) {
        return "bool";
    }
    if (((((strcmp(v_fname, "index_of") == 0) || (strcmp(v_fname, "ord") == 0)) || (strcmp(v_fname, "sign") == 0)) || (strcmp(v_fname, "clamp") == 0))) {
        return "i64";
    }
    if ((((((((((((((strcmp(v_fname, "to_upper") == 0) || (strcmp(v_fname, "to_lower") == 0)) || (strcmp(v_fname, "trim") == 0)) || (strcmp(v_fname, "replace") == 0)) || (strcmp(v_fname, "repeat") == 0)) || (strcmp(v_fname, "pad_left") == 0)) || (strcmp(v_fname, "pad_right") == 0)) || (strcmp(v_fname, "chr") == 0)) || (strcmp(v_fname, "read_line") == 0)) || (strcmp(v_fname, "get_env") == 0)) || (strcmp(v_fname, "exe_dir") == 0)) || (strcmp(v_fname, "format") == 0)) || (strcmp(v_fname, "join") == 0))) {
        return "str";
    }
    if ((strcmp(v_fname, "split") == 0)) {
        return "[str]";
    }
    if ((((((strcmp(v_fname, "now_ms") == 0) || (strcmp(v_fname, "now_us") == 0)) || (strcmp(v_fname, "mono_ms") == 0)) || (strcmp(v_fname, "sleep_ms") == 0)) || (strcmp(v_fname, "flush") == 0))) {
        return "i64";
    }
    if ((strcmp(v_fname, "time_iso") == 0)) {
        return "str";
    }
    if (((((((strcmp(v_fname, "tcp_listen") == 0) || (strcmp(v_fname, "tcp_accept") == 0)) || (strcmp(v_fname, "tcp_connect") == 0)) || (strcmp(v_fname, "sock_send") == 0)) || (strcmp(v_fname, "sock_send_str") == 0)) || (strcmp(v_fname, "sock_close") == 0))) {
        return "i64";
    }
    if ((strcmp(v_fname, "sock_recv") == 0)) {
        return "bytes";
    }
    if (((((strcmp(v_fname, "proc_fork") == 0) || (strcmp(v_fname, "proc_getpid") == 0)) || (strcmp(v_fname, "proc_no_zombies") == 0)) || (strcmp(v_fname, "proc_reap") == 0))) {
        return "i64";
    }
    if ((((((((((strcmp(v_fname, "thread_spawn") == 0) || (strcmp(v_fname, "thread_join") == 0)) || (strcmp(v_fname, "mutex_new") == 0)) || (strcmp(v_fname, "mutex_lock") == 0)) || (strcmp(v_fname, "mutex_unlock") == 0)) || (strcmp(v_fname, "chan_new") == 0)) || (strcmp(v_fname, "chan_send") == 0)) || (strcmp(v_fname, "chan_recv") == 0)) || (strcmp(v_fname, "chan_close") == 0))) {
        return "i64";
    }
    if ((strcmp(v_fname, "regex_match") == 0)) {
        return "bool";
    }
    if ((strcmp(v_fname, "regex_find") == 0)) {
        return "str";
    }
    if (((strcmp(v_fname, "abs") == 0) && (arr_Expr_len(v_args) > 0))) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
    }
    if ((strcmp(v_fname, "abs_i64") == 0)) {
        return "i64";
    }
    if ((strcmp(v_fname, "abs_f64") == 0)) {
        return "f64";
    }
    if (((strcmp(v_fname, "pop") == 0) && (arr_Expr_len(v_args) > 0))) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
    }
    if (((((((strcmp(v_fname, "tls_error") == 0) || (strcmp(v_fname, "pg_error") == 0)) || (strcmp(v_fname, "pg_result_error") == 0)) || (strcmp(v_fname, "pg_value") == 0)) || (strcmp(v_fname, "pg_col_name") == 0)) || (strcmp(v_fname, "pg_escape") == 0))) {
        return "str";
    }
    if (((strcmp(v_fname, "tls_recv") == 0) || (strcmp(v_fname, "sha1") == 0))) {
        return "bytes";
    }
    if (((strcmp(v_fname, "pg_ok") == 0) || (strcmp(v_fname, "pg_isnull") == 0))) {
        return "bool";
    }
    if (f_is_native_call(v_fname)) {
        return "i64";
    }
    if (f_is_ctor(v_sy, v_fname)) {
        return v_fname;
    }
    if (f_is_variant(v_sy, v_fname)) {
        return map_str_str_get((v_sy)->evar, v_fname);
    }
    if ((map_str_str_has((v_sy)->vty, v_fname) && f_is_fn_ann(map_str_str_get((v_sy)->vty, v_fname)))) {
        return f_fn_ret_of(map_str_str_get((v_sy)->vty, v_fname));
    }
    if (map_str_str_has((v_sy)->frets, v_fname)) {
        return map_str_str_get((v_sy)->frets, v_fname);
    }
    return "i64";
}

const char* f_field_type(s_Syms* v_sy, s_Expr v_obj, const char* v_fname) {
    const char* v_sty;
    const char* v_k;
    v_sty = f_under_ptr(f_type_of_expr(v_sy, v_obj));
    v_k = f_fkey(v_sty, v_fname);
    if (map_str_str_has((v_sy)->fld, v_k)) {
        return map_str_str_get((v_sy)->fld, v_k);
    }
    return "i64";
}

int64_t f_expr_is_str(s_Expr v_e, s_Syms* v_sy) {
    return (strcmp(f_type_of_expr(v_sy, v_e), "str") == 0);
}

int64_t f_confident(const char* v_t) {
    return ((((int64_t)strlen(v_t)) > 0) && (strcmp(v_t, "?") != 0));
}

int64_t f_is_num(const char* v_t) {
    return (((((((((((strcmp(v_t, "i64") == 0) || (strcmp(v_t, "i32") == 0)) || (strcmp(v_t, "i16") == 0)) || (strcmp(v_t, "i8") == 0)) || (strcmp(v_t, "u64") == 0)) || (strcmp(v_t, "u32") == 0)) || (strcmp(v_t, "u16") == 0)) || (strcmp(v_t, "u8") == 0)) || (strcmp(v_t, "f64") == 0)) || (strcmp(v_t, "f32") == 0)) || (strcmp(v_t, "bool") == 0));
}

const char* f_tcon_var(s_Syms* v_sy, const char* v_name) {
    if (map_str_str_has((v_sy)->vty, v_name)) {
        return map_str_str_get((v_sy)->vty, v_name);
    }
    if (f_is_variant(v_sy, v_name)) {
        return map_str_str_get((v_sy)->evar, v_name);
    }
    return "?";
}

const char* f_builtin_fixed_ret(const char* v_fname) {
    if ((((((((((((((((((((((((((((((((((strcmp(v_fname, "len") == 0) || (strcmp(v_fname, "str_to_int") == 0)) || (strcmp(v_fname, "bytes_at") == 0)) || (strcmp(v_fname, "index_of") == 0)) || (strcmp(v_fname, "ord") == 0)) || (strcmp(v_fname, "sign") == 0)) || (strcmp(v_fname, "clamp") == 0)) || (strcmp(v_fname, "now_ms") == 0)) || (strcmp(v_fname, "now_us") == 0)) || (strcmp(v_fname, "mono_ms") == 0)) || (strcmp(v_fname, "sleep_ms") == 0)) || (strcmp(v_fname, "flush") == 0)) || (strcmp(v_fname, "write_file_bytes") == 0)) || (strcmp(v_fname, "abs_i64") == 0)) || (strcmp(v_fname, "tcp_listen") == 0)) || (strcmp(v_fname, "tcp_accept") == 0)) || (strcmp(v_fname, "tcp_connect") == 0)) || (strcmp(v_fname, "sock_send") == 0)) || (strcmp(v_fname, "sock_send_str") == 0)) || (strcmp(v_fname, "sock_close") == 0)) || (strcmp(v_fname, "proc_fork") == 0)) || (strcmp(v_fname, "proc_getpid") == 0)) || (strcmp(v_fname, "proc_no_zombies") == 0)) || (strcmp(v_fname, "proc_reap") == 0)) || (strcmp(v_fname, "thread_spawn") == 0)) || (strcmp(v_fname, "thread_join") == 0)) || (strcmp(v_fname, "mutex_new") == 0)) || (strcmp(v_fname, "mutex_lock") == 0)) || (strcmp(v_fname, "mutex_unlock") == 0)) || (strcmp(v_fname, "chan_new") == 0)) || (strcmp(v_fname, "chan_send") == 0)) || (strcmp(v_fname, "chan_recv") == 0)) || (strcmp(v_fname, "chan_close") == 0))) {
        return "i64";
    }
    if ((((((((((((((((((((((((strcmp(v_fname, "to_str") == 0) || (strcmp(v_fname, "cstr") == 0)) || (strcmp(v_fname, "int_to_str") == 0)) || (strcmp(v_fname, "float_to_str") == 0)) || (strcmp(v_fname, "substring") == 0)) || (strcmp(v_fname, "read_file") == 0)) || (strcmp(v_fname, "bytes_to_str") == 0)) || (strcmp(v_fname, "err_msg") == 0)) || (strcmp(v_fname, "time_iso") == 0)) || (strcmp(v_fname, "regex_find") == 0)) || (strcmp(v_fname, "to_upper") == 0)) || (strcmp(v_fname, "to_lower") == 0)) || (strcmp(v_fname, "trim") == 0)) || (strcmp(v_fname, "replace") == 0)) || (strcmp(v_fname, "repeat") == 0)) || (strcmp(v_fname, "pad_left") == 0)) || (strcmp(v_fname, "pad_right") == 0)) || (strcmp(v_fname, "chr") == 0)) || (strcmp(v_fname, "read_line") == 0)) || (strcmp(v_fname, "get_env") == 0)) || (strcmp(v_fname, "exe_dir") == 0)) || (strcmp(v_fname, "format") == 0)) || (strcmp(v_fname, "join") == 0))) {
        return "str";
    }
    if (((strcmp(v_fname, "str_to_float") == 0) || (strcmp(v_fname, "abs_f64") == 0))) {
        return "f64";
    }
    if ((((((((((strcmp(v_fname, "starts_with") == 0) || (strcmp(v_fname, "ends_with") == 0)) || (strcmp(v_fname, "contains") == 0)) || (strcmp(v_fname, "str_to_bool") == 0)) || (strcmp(v_fname, "has") == 0)) || (strcmp(v_fname, "is_ok") == 0)) || (strcmp(v_fname, "is_err") == 0)) || (strcmp(v_fname, "regex_match") == 0)) || (strcmp(v_fname, "write_file") == 0))) {
        return "bool";
    }
    if (((strcmp(v_fname, "split") == 0) || (strcmp(v_fname, "args") == 0))) {
        return "[str]";
    }
    if (((((strcmp(v_fname, "str_to_bytes") == 0) || (strcmp(v_fname, "bytes_slice") == 0)) || (strcmp(v_fname, "read_file_bytes") == 0)) || (strcmp(v_fname, "sock_recv") == 0))) {
        return "bytes";
    }
    return "?";
}

const char* f_tcon_call(s_Syms* v_sy, const char* v_fname, arr_Expr v_args) {
    const char* v_bf;
    if (f_is_ctor(v_sy, v_fname)) {
        return v_fname;
    }
    if (f_is_variant(v_sy, v_fname)) {
        return map_str_str_get((v_sy)->evar, v_fname);
    }
    if (f_is_generic(v_sy, v_fname)) {
        return "?";
    }
    v_bf = f_builtin_fixed_ret(v_fname);
    if ((strcmp(v_bf, "?") != 0)) {
        return v_bf;
    }
    if (f_is_native_call(v_fname)) {
        return "?";
    }
    if (map_str_str_has((v_sy)->frets, v_fname)) {
        return map_str_str_get((v_sy)->frets, v_fname);
    }
    return "?";
}

const char* f_tcon_field(s_Syms* v_sy, s_Expr v_obj, const char* v_fname) {
    const char* v_ot;
    const char* v_sty;
    const char* v_k;
    v_ot = f_type_confident(v_sy, v_obj);
    if ((strcmp(v_ot, "?") == 0)) {
        return "?";
    }
    v_sty = f_under_ptr(v_ot);
    v_k = f_fkey(v_sty, v_fname);
    if (map_str_str_has((v_sy)->fld, v_k)) {
        return map_str_str_get((v_sy)->fld, v_k);
    }
    return "?";
}

const char* f_tcon_index(s_Syms* v_sy, s_Expr v_obj) {
    const char* v_t;
    v_t = f_type_confident(v_sy, v_obj);
    if ((strcmp(v_t, "?") == 0)) {
        return "?";
    }
    if (f_is_array_ann(v_t)) {
        return f_elem_of_ann(v_t);
    }
    if (f_is_map_ann(v_t)) {
        return f_map_vtype(v_t);
    }
    if ((strcmp(v_t, "str") == 0)) {
        return "i64";
    }
    if ((strcmp(v_t, "bytes") == 0)) {
        return "i64";
    }
    return "?";
}

const char* f_tcon_bin(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r) {
    const char* v_lt;
    const char* v_rr;
    if ((v_op == f_OP_CAT())) {
        return "str";
    }
    if (((v_op >= f_OP_LT()) && (v_op <= f_OP_OR()))) {
        return "bool";
    }
    v_lt = f_type_confident(v_sy, v_l);
    v_rr = f_type_confident(v_sy, v_r);
    if ((v_op == f_OP_QQ())) {
        return v_lt;
    }
    if (((strcmp(v_lt, "?") == 0) || (strcmp(v_rr, "?") == 0))) {
        return "?";
    }
    if ((((v_op == f_OP_ADD()) && (strcmp(v_lt, "str") == 0)) && (strcmp(v_rr, "str") == 0))) {
        return "str";
    }
    if ((f_is_num(v_lt) && f_is_num(v_rr))) {
        if (((strcmp(v_lt, "f64") == 0) || (strcmp(v_rr, "f64") == 0))) {
            return "f64";
        }
        return "i64";
    }
    return "?";
}

const char* f_tcon_addr(s_Syms* v_sy, s_Expr v_x) {
    const char* v_it;
    v_it = f_type_confident(v_sy, v_x);
    if ((strcmp(v_it, "?") == 0)) {
        return "?";
    }
    return scat("*", v_it);
}

const char* f_type_confident(s_Syms* v_sy, s_Expr v_e) {
    return ({ const char* __m; s_Expr __s = v_e; if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = "i64"; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = "f64"; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = "str"; } else if(__s.tag==3){ const char* v_name = __s.u.Var.f0; __m = f_tcon_var(v_sy, v_name); } else if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_tcon_bin(v_sy, v_op, v_l, v_r); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = "i64"; } else if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_tcon_call(v_sy, v_fname, v_args); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_tcon_field(v_sy, v_obj, v_fnm); } else if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_tcon_index(v_sy, v_obj); } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = "?"; } else if(__s.tag==10){ const char* v_mty = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = "?"; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = f_tcon_addr(v_sy, v_x); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = "?"; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = "?"; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.Try.f0); __m = "?"; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = "?"; } else if(__s.tag==16){ arr_Expr v_elems = __s.u.Tuple.f0; __m = "?"; } else if(__s.tag==17){ arr_Stmt v_body = __s.u.BlockE.f0; __m = "?"; } else if(__s.tag==18){ __m = "?"; } __m; });
}

const char* f_ty_cat(const char* v_t) {
    if (f_is_num(v_t)) {
        return "num";
    }
    if ((strcmp(v_t, "str") == 0)) {
        return "str";
    }
    if ((strcmp(v_t, "bytes") == 0)) {
        return "bytes";
    }
    if (f_is_array_ann(v_t)) {
        return "arr";
    }
    if (f_is_map_ann(v_t)) {
        return "map";
    }
    if (f_is_ptr_ann(v_t)) {
        return "skip";
    }
    if (f_is_fn_ann(v_t)) {
        return "skip";
    }
    if (f_is_tuple_ann(v_t)) {
        return "skip";
    }
    if (f_is_result_ann(v_t)) {
        return "skip";
    }
    if (f_is_c_type_ann(v_t)) {
        return "skip";
    }
    if ((strcmp(v_t, "?") == 0)) {
        return "skip";
    }
    return "agg";
}

int64_t f_is_ancestor(s_Syms* v_sy, const char* v_anc, const char* v_desc) {
    const char* v_cur;
    const char* v_p;
    v_cur = v_desc;
    while ((((int64_t)strlen(v_cur)) > 0)) {
        if ((map_str_str_has((v_sy)->evar, scat("@class.parent.", v_cur)) == (1 != 1))) {
            return (1 != 1);
        }
        v_p = map_str_str_get((v_sy)->evar, scat("@class.parent.", v_cur));
        if ((strcmp(v_p, v_anc) == 0)) {
            return (1 == 1);
        }
        v_cur = v_p;
    }
    return (1 != 1);
}

int64_t f_cat_incompatible(const char* v_a, const char* v_b) {
    const char* v_ca;
    const char* v_cb;
    if ((strcmp(v_a, v_b) == 0)) {
        return (1 != 1);
    }
    v_ca = f_ty_cat(v_a);
    v_cb = f_ty_cat(v_b);
    if (((strcmp(v_ca, "skip") == 0) || (strcmp(v_cb, "skip") == 0))) {
        return (1 != 1);
    }
    if ((strcmp(v_ca, v_cb) == 0)) {
        return (1 != 1);
    }
    return (1 == 1);
}

int64_t f_incompatible(s_Syms* v_sy, const char* v_a, const char* v_b) {
    const char* v_ca;
    const char* v_cb;
    if ((strcmp(v_a, v_b) == 0)) {
        return (1 != 1);
    }
    v_ca = f_ty_cat(v_a);
    v_cb = f_ty_cat(v_b);
    if (((strcmp(v_ca, "skip") == 0) || (strcmp(v_cb, "skip") == 0))) {
        return (1 != 1);
    }
    if ((strcmp(v_ca, v_cb) == 0)) {
        if ((strcmp(v_ca, "agg") == 0)) {
            if (f_is_ancestor(v_sy, v_a, v_b)) {
                return (1 != 1);
            }
            if (f_is_ancestor(v_sy, v_b, v_a)) {
                return (1 != 1);
            }
            return (1 == 1);
        }
        return (1 != 1);
    }
    return (1 == 1);
}

const char* f_gen_args(s_Syms* v_sy, arr_Expr v_args) {
    const char* v_out;
    int64_t v_i;
    v_out = "";
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        if ((v_i > 0)) {
            v_out = scat(v_out, ", ");
        }
        v_out = scat(v_out, f_gen_expr(v_sy, arr_Expr_get(v_args, v_i)));
        v_i = (v_i + 1);
    }
    return v_out;
}

const char* f_gen_str(const char* v_s) {
    return scat(scat("\"", v_s), "\"");
}

const char* f_gen_var(s_Syms* v_sy, const char* v_name) {
    if (f_is_variant(v_sy, v_name)) {
        return scat(scat("mkv_", v_name), "()");
    }
    return scat("v_", v_name);
}

const char* f_gen_expr(s_Syms* v_sy, s_Expr v_e) {
    return ({ const char* __m; s_Expr __s = v_e; if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = i2s(v_v); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = v_fs; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = f_gen_str(v_s); } else if(__s.tag==3){ const char* v_name = __s.u.Var.f0; __m = f_gen_var(v_sy, v_name); } else if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_gen_bin(v_sy, v_op, v_l, v_r); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = scat(scat("(!", f_gen_expr(v_sy, v_x)), ")"); } else if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_gen_call(v_sy, v_fname, v_args); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_gen_field(v_sy, v_obj, v_fnm); } else if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_gen_index(v_sy, v_obj, v_idx); } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = f_gen_array(v_sy, v_elems, v_ety); } else if(__s.tag==10){ const char* v_mty = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_gen_maplit(v_sy, v_mty, v_mks, v_mvs); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = scat(scat("(&", f_gen_expr(v_sy, v_x)), ")"); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_gen_match(v_sy, v_sc, v_vn, v_vb, v_bd); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = f_gen_ife(v_sy, v_c, v_t, v_el2); } else if(__s.tag==14){ s_Expr v_e = *(__s.u.Try.f0); __m = f_gen_try(v_sy, v_e); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = f_gen_lambda(v_sy, v_id); } else if(__s.tag==16){ arr_Expr v_elems = __s.u.Tuple.f0; __m = f_gen_tuple_lit(v_sy, v_elems); } else if(__s.tag==17){ arr_Stmt v_body = __s.u.BlockE.f0; __m = f_gen_block_e(v_sy, v_body); } else if(__s.tag==18){ __m = "0"; } __m; });
}

const char* f_gen_ife(s_Syms* v_sy, s_Expr v_c, s_Expr v_t, s_Expr v_el2) {
    const char* v_rty;
    v_rty = f_type_of_expr(v_sy, v_t);
    return scat(scat(scat(scat(scat(scat(scat(scat("({ ", f_cty(v_rty)), " __r; if ("), f_gen_expr(v_sy, v_c)), ") { __r = "), f_gen_expr(v_sy, v_t)), "; } else { __r = "), f_gen_expr(v_sy, v_el2)), "; } __r; })");
}

const char* f_gen_try(s_Syms* v_sy, s_Expr v_e) {
    const char* v_rcty;
    const char* v_fret;
    v_rcty = f_cty(f_type_of_expr(v_sy, v_e));
    v_fret = f_ty_of(v_sy, "@ret");
    return scat(scat(scat(scat(scat(scat("({ ", v_rcty), " __t = "), f_gen_expr(v_sy, v_e)), "; if(__t.tag) return mk_err_"), f_arr_suffix(v_fret)), "(__t.err); __t.ok; })");
}

const char* f_gen_match(s_Syms* v_sy, s_Expr v_sc, arr_str v_vnames, arr_str v_vbinds, arr_Expr v_bodies) {
    const char* v_sty;
    const char* v_rty;
    const char* v_out;
    int64_t v_i;
    const char* v_vname;
    arr_str v_binds;
    int64_t v_tag;
    const char* v_kw;
    v_sty = f_type_of_expr(v_sy, v_sc);
    if ((arr_str_len(v_vnames) > 0)) {
        f_bind_arm_types(v_sy, arr_str_get(v_vnames, 0), f_split_semi(arr_str_get(v_vbinds, 0)));
    }
    v_rty = f_match_type(v_sy, v_bodies);
    v_out = scat(scat(scat(scat(scat(scat("({ ", f_cty(v_rty)), " __m; "), f_cty(v_sty)), " __s = "), f_gen_expr(v_sy, v_sc)), "; ");
    v_i = 0;
    while ((v_i < arr_str_len(v_vnames))) {
        v_vname = arr_str_get(v_vnames, v_i);
        v_binds = f_split_semi(arr_str_get(v_vbinds, v_i));
        v_tag = f_variant_tag(v_sy, v_sty, v_vname);
        v_kw = ({ const char* __r; if ((v_i == 0)) { __r = "if"; } else { __r = "else if"; } __r; });
        v_out = scat(scat(scat(scat(v_out, v_kw), "(__s.tag=="), i2s(v_tag)), "){ ");
        v_out = scat(v_out, f_gen_arm_binds(v_sy, v_vname, v_binds));
        f_bind_arm_types(v_sy, v_vname, v_binds);
        v_out = scat(scat(scat(v_out, "__m = "), f_gen_expr(v_sy, arr_Expr_get(v_bodies, v_i))), "; } ");
        v_i = (v_i + 1);
    }
    return scat(v_out, "__m; })");
}

const char* f_gen_arm_binds(s_Syms* v_sy, const char* v_vname, arr_str v_binds) {
    arr_str v_ftypes;
    const char* v_out;
    int64_t v_i;
    const char* v_ft;
    const char* v_src;
    const char* v_rhs;
    const char* v_cft;
    v_ftypes = f_split_semi(map_str_str_get((v_sy)->vft, v_vname));
    v_out = "";
    v_i = 0;
    while ((v_i < arr_str_len(v_binds))) {
        v_ft = ({ const char* __r; if ((v_i < arr_str_len(v_ftypes))) { __r = arr_str_get(v_ftypes, v_i); } else { __r = "i64"; } __r; });
        v_src = scat(scat(scat("__s.u.", v_vname), ".f"), i2s(v_i));
        v_rhs = ({ const char* __r; if (f_is_boxed_ft(v_ft)) { __r = scat(scat("*(", v_src), ")"); } else { __r = v_src; } __r; });
        v_cft = ({ const char* __r; if (f_is_boxed_ft(v_ft)) { __r = f_cty(f_boxed_enum(v_ft, map_str_str_get((v_sy)->evar, v_vname))); } else { __r = f_cty(v_ft); } __r; });
        v_out = scat(scat(scat(scat(scat(scat(v_out, v_cft), " v_"), arr_str_get(v_binds, v_i)), " = "), v_rhs), "; ");
        v_i = (v_i + 1);
    }
    return v_out;
}

void f_bind_arm_types(s_Syms* v_sy, const char* v_vname, arr_str v_binds) {
    arr_str v_ftypes;
    int64_t v_i;
    const char* v_ft;
    const char* v_rt2;
    v_ftypes = f_split_semi(map_str_str_get((v_sy)->vft, v_vname));
    v_i = 0;
    while ((v_i < arr_str_len(v_binds))) {
        v_ft = ({ const char* __r; if ((v_i < arr_str_len(v_ftypes))) { __r = arr_str_get(v_ftypes, v_i); } else { __r = "i64"; } __r; });
        v_rt2 = ({ const char* __r; if (f_is_boxed_ft(v_ft)) { __r = f_boxed_enum(v_ft, map_str_str_get((v_sy)->evar, v_vname)); } else { __r = v_ft; } __r; });
        f_set_ty(v_sy, arr_str_get(v_binds, v_i), v_rt2);
        v_i = (v_i + 1);
    }
}

int64_t f_variant_tag(s_Syms* v_sy, const char* v_sty, const char* v_vname) {
    arr_str v_order;
    int64_t v_i;
    v_order = f_split_semi(map_str_str_get((v_sy)->evar, scat("@order.", v_sty)));
    v_i = 0;
    while ((v_i < arr_str_len(v_order))) {
        if ((strcmp(arr_str_get(v_order, v_i), v_vname) == 0)) {
            return v_i;
        }
        v_i = (v_i + 1);
    }
    return 0;
}

arr_str f_split_semi(const char* v_s) {
    arr_str v_out;
    int64_t v_start;
    int64_t v_i;
    v_out = ({ arr_str __a = arr_str_new(); __a; });
    if ((((int64_t)strlen(v_s)) == 0)) {
        return v_out;
    }
    v_start = 0;
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_s)))) {
        if ((((int64_t)(unsigned char)(v_s)[v_i]) == 59)) {
            v_out = arr_str_push(v_out, substr(v_s, v_start, v_i));
            v_start = (v_i + 1);
        }
        v_i = (v_i + 1);
    }
    v_out = arr_str_push(v_out, substr(v_s, v_start, ((int64_t)strlen(v_s))));
    return v_out;
}

const char* f_gen_field(s_Syms* v_sy, s_Expr v_obj, const char* v_fnm) {
    if (f_is_ptr_ann(f_type_of_expr(v_sy, v_obj))) {
        return scat(scat(scat("(", f_gen_expr(v_sy, v_obj)), ")->"), v_fnm);
    }
    return scat(scat(scat("(", f_gen_expr(v_sy, v_obj)), ")."), v_fnm);
}

const char* f_gen_index(s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx) {
    const char* v_ot;
    v_ot = f_type_of_expr(v_sy, v_obj);
    if (f_is_array_ann(v_ot)) {
        return scat(scat(scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_ot))), "_get("), f_gen_expr(v_sy, v_obj)), ", "), f_gen_expr(v_sy, v_idx)), ")");
    }
    if (f_is_map_ann(v_ot)) {
        return scat(scat(scat(scat(scat(f_map_cty(v_ot), "_get("), f_gen_expr(v_sy, v_obj)), ", "), f_gen_expr(v_sy, v_idx)), ")");
    }
    if ((strcmp(v_ot, "bytes") == 0)) {
        return scat(scat(scat(scat("bytes_at(", f_gen_expr(v_sy, v_obj)), ", "), f_gen_expr(v_sy, v_idx)), ")");
    }
    if ((strcmp(v_ot, "str") == 0)) {
        return scat(scat(scat(scat("((int64_t)(unsigned char)(", f_gen_expr(v_sy, v_obj)), ")["), f_gen_expr(v_sy, v_idx)), "])");
    }
    return "0";
}

const char* f_gen_array(s_Syms* v_sy, arr_Expr v_elems, const char* v_ety) {
    const char* v_at;
    const char* v_suf;
    const char* v_out;
    int64_t v_i;
    v_at = f_array_type_of(v_sy, v_elems, v_ety);
    v_suf = f_arr_suffix(f_elem_of_ann(v_at));
    v_out = scat(scat(scat(scat("({ arr_", v_suf), " __a = arr_"), v_suf), "_new(); ");
    v_i = 0;
    while ((v_i < arr_Expr_len(v_elems))) {
        v_out = scat(scat(scat(scat(scat(v_out, "__a = arr_"), v_suf), "_push(__a, "), f_gen_expr(v_sy, arr_Expr_get(v_elems, v_i))), "); ");
        v_i = (v_i + 1);
    }
    return scat(v_out, "__a; })");
}

const char* f_maplit_type(s_Syms* v_sy, const char* v_mty, arr_Expr v_keys, arr_Expr v_vals) {
    if ((arr_Expr_len(v_keys) > 0)) {
        return scat(scat(scat(scat("{", f_type_of_expr(v_sy, arr_Expr_get(v_keys, 0))), ":"), f_type_of_expr(v_sy, arr_Expr_get(v_vals, 0))), "}");
    }
    return f_map_lit_type(v_mty);
}

const char* f_gen_maplit(s_Syms* v_sy, const char* v_mty, arr_Expr v_keys, arr_Expr v_vals) {
    const char* v_mc;
    const char* v_out;
    int64_t v_i;
    v_mc = f_map_cty(f_maplit_type(v_sy, v_mty, v_keys, v_vals));
    if ((arr_Expr_len(v_keys) == 0)) {
        return scat(v_mc, "_new()");
    }
    v_out = scat(scat(scat(scat("({ ", v_mc), " __ml = "), v_mc), "_new(); ");
    v_i = 0;
    while ((v_i < arr_Expr_len(v_keys))) {
        v_out = scat(scat(scat(scat(scat(scat(v_out, v_mc), "_set(__ml, "), f_gen_expr(v_sy, arr_Expr_get(v_keys, v_i))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_vals, v_i))), "); ");
        v_i = (v_i + 1);
    }
    return scat(v_out, "__ml; })");
}

arr_str f_empty_strs(void) {
    arr_str v_e;
    v_e = ({ arr_str __a = arr_str_new(); __a; });
    return v_e;
}

arr_Expr f_no_exprs(void) {
    arr_Expr v_e;
    v_e = ({ arr_Expr __a = arr_Expr_new(); __a; });
    return v_e;
}

arr_str f_lam_params(s_Expr v_e) {
    return ({ arr_str __m; s_Expr __s = v_e; if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = v_ps; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = f_empty_strs(); } else if(__s.tag==1){ const char* v_s = __s.u.Flt.f0; __m = f_empty_strs(); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = f_empty_strs(); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = f_empty_strs(); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_empty_strs(); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = f_empty_strs(); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = f_empty_strs(); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = f_empty_strs(); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_i = *(__s.u.Index.f1); __m = f_empty_strs(); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = f_empty_strs(); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = f_empty_strs(); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = f_empty_strs(); } else if(__s.tag==10){ const char* v_m = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_empty_strs(); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = f_empty_strs(); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_empty_strs(); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = f_empty_strs(); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = f_empty_strs(); } else if(__s.tag==18){ __m = f_empty_strs(); } __m; });
}

s_Expr f_lam_body(s_Expr v_e) {
    return ({ s_Expr __m; s_Expr __s = v_e; if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = v_b; } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = mkv_Bad(); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = mkv_Bad(); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = mkv_Bad(); } else if(__s.tag==1){ const char* v_s = __s.u.Flt.f0; __m = mkv_Bad(); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = mkv_Bad(); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = mkv_Bad(); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = mkv_Bad(); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = mkv_Bad(); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = mkv_Bad(); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = mkv_Bad(); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_i = *(__s.u.Index.f1); __m = mkv_Bad(); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = mkv_Bad(); } else if(__s.tag==10){ const char* v_m = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = mkv_Bad(); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = mkv_Bad(); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = mkv_Bad(); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = mkv_Bad(); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = mkv_Bad(); } else if(__s.tag==18){ __m = mkv_Bad(); } __m; });
}

int64_t f_is_param(arr_str v_ps, const char* v_nm) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len(v_ps))) {
        if ((strcmp(arr_str_get(v_ps, v_i), v_nm) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

arr_str f_fv_add(arr_str v_acc, const char* v_name) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len(v_acc))) {
        if ((strcmp(arr_str_get(v_acc, v_i), v_name) == 0)) {
            return v_acc;
        }
        v_i = (v_i + 1);
    }
    return arr_str_push(v_acc, v_name);
}

arr_str f_fv_args(arr_Expr v_args, arr_str v_acc) {
    arr_str v_a;
    int64_t v_i;
    v_a = v_acc;
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        v_a = f_fv_walk(arr_Expr_get(v_args, v_i), v_a);
        v_i = (v_i + 1);
    }
    return v_a;
}

arr_str f_fv_walk(s_Expr v_e, arr_str v_acc) {
    return ({ arr_str __m; s_Expr __s = v_e; if(__s.tag==3){ const char* v_name = __s.u.Var.f0; __m = f_fv_add(v_acc, v_name); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = v_acc; } else if(__s.tag==1){ const char* v_s = __s.u.Flt.f0; __m = v_acc; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = v_acc; } else if(__s.tag==10){ const char* v_m = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_fv_args(v_mvs, f_fv_args(v_mks, v_acc)); } else if(__s.tag==18){ __m = v_acc; } else if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_fv_walk(v_r, f_fv_walk(v_l, v_acc)); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = f_fv_walk(v_x, v_acc); } else if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_fv_args(v_args, v_acc); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = f_fv_args(v_tes, v_acc); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = f_fv_body(v_bb, v_acc); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_fv_walk(v_obj, v_acc); } else if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_fv_walk(v_idx, f_fv_walk(v_obj, v_acc)); } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = f_fv_args(v_elems, v_acc); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = f_fv_walk(v_x, v_acc); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_fv_args(v_bd, f_fv_walk(v_sc, v_acc)); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = f_fv_walk(v_el2, f_fv_walk(v_t, f_fv_walk(v_c, v_acc))); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = f_fv_walk(v_x, v_acc); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = f_fv_walk(v_b, v_acc); } __m; });
}

arr_str f_fv_body(arr_Stmt v_body, arr_str v_acc) {
    arr_str v_a;
    int64_t v_i;
    v_a = v_acc;
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_a = f_fv_stmt(arr_Stmt_get(v_body, v_i), v_a);
        v_i = (v_i + 1);
    }
    return v_a;
}

arr_str f_fv_stmt(s_Stmt v_s, arr_str v_acc) {
    return ({ arr_str __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_fv_walk(v_e, v_acc); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_fv_walk(v_e, v_acc); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = f_fv_walk(v_e, v_acc); } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_ix = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = f_fv_walk(v_e, f_fv_walk(v_ix, f_fv_walk(v_o, v_acc))); } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = f_fv_walk(v_e, f_fv_walk(v_o, v_acc)); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = f_fv_walk(v_e, v_acc); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = f_fv_walk(v_e, v_acc); } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = f_fv_walk(v_e, v_acc); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_fv_body(v_eb, f_fv_body(v_b, f_fv_walk(v_c, v_acc))); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_fv_body(v_b, f_fv_walk(v_c, v_acc)); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_fv_body(v_b, f_fv_walk(v_coll, v_acc)); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_fv_body(v_b, f_fv_walk(v_coll, v_acc)); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_fv_body(v_b, f_fv_walk(v_hi, f_fv_walk(v_lo, v_acc))); } else if(__s.tag==12){ __m = v_acc; } else if(__s.tag==13){ __m = v_acc; } __m; });
}

arr_str f_free_vars(s_Expr v_b) {
    arr_str v_acc;
    v_acc = ({ arr_str __a = arr_str_new(); __a; });
    return f_fv_walk(v_b, v_acc);
}

const char* f_gen_map(s_Syms* v_sy, s_Expr v_arr, s_Expr v_lam) {
    const char* v_at;
    const char* v_et;
    arr_str v_ps;
    s_Expr v_b;
    const char* v_ut;
    const char* v_usuf;
    const char* v_tsuf;
    const char* v_o;
    v_at = f_type_of_expr(v_sy, v_arr);
    v_et = f_elem_of_ann(v_at);
    v_ps = f_lam_params(v_lam);
    f_set_ty(v_sy, arr_str_get(v_ps, 0), v_et);
    v_b = f_lam_body(v_lam);
    v_ut = f_type_of_expr(v_sy, v_b);
    v_usuf = f_arr_suffix(v_ut);
    v_tsuf = f_arr_suffix(v_et);
    v_o = scat(scat(scat(scat(scat(scat(scat(scat("({ arr_", v_usuf), " __hm = arr_"), v_usuf), "_new(); arr_"), v_tsuf), " __hs = "), f_gen_expr(v_sy, v_arr)), "; ");
    v_o = scat(scat(scat(scat(scat(v_o, "for(int64_t __hi=0; __hi<__hs.len; __hi++){ "), f_cty(v_et)), " v_"), arr_str_get(v_ps, 0)), " = __hs.data[__hi]; ");
    v_o = scat(scat(scat(scat(scat(v_o, "__hm = arr_"), v_usuf), "_push(__hm, "), f_gen_expr(v_sy, v_b)), "); } __hm; })");
    return v_o;
}

const char* f_gen_filter(s_Syms* v_sy, s_Expr v_arr, s_Expr v_lam) {
    const char* v_at;
    const char* v_et;
    arr_str v_ps;
    s_Expr v_b;
    const char* v_tsuf;
    const char* v_o;
    v_at = f_type_of_expr(v_sy, v_arr);
    v_et = f_elem_of_ann(v_at);
    v_ps = f_lam_params(v_lam);
    f_set_ty(v_sy, arr_str_get(v_ps, 0), v_et);
    v_b = f_lam_body(v_lam);
    v_tsuf = f_arr_suffix(v_et);
    v_o = scat(scat(scat(scat(scat(scat(scat(scat("({ arr_", v_tsuf), " __hm = arr_"), v_tsuf), "_new(); arr_"), v_tsuf), " __hs = "), f_gen_expr(v_sy, v_arr)), "; ");
    v_o = scat(scat(scat(scat(scat(v_o, "for(int64_t __hi=0; __hi<__hs.len; __hi++){ "), f_cty(v_et)), " v_"), arr_str_get(v_ps, 0)), " = __hs.data[__hi]; ");
    v_o = scat(scat(scat(scat(scat(scat(scat(v_o, "if("), f_gen_expr(v_sy, v_b)), "){ __hm = arr_"), v_tsuf), "_push(__hm, v_"), arr_str_get(v_ps, 0)), "); } } __hm; })");
    return v_o;
}

const char* f_gen_reduce(s_Syms* v_sy, s_Expr v_arr, s_Expr v_init, s_Expr v_lam) {
    const char* v_at;
    const char* v_et;
    const char* v_ut;
    arr_str v_ps;
    s_Expr v_b;
    const char* v_tsuf;
    const char* v_o;
    v_at = f_type_of_expr(v_sy, v_arr);
    v_et = f_elem_of_ann(v_at);
    v_ut = f_type_of_expr(v_sy, v_init);
    v_ps = f_lam_params(v_lam);
    f_set_ty(v_sy, arr_str_get(v_ps, 0), v_ut);
    f_set_ty(v_sy, arr_str_get(v_ps, 1), v_et);
    v_b = f_lam_body(v_lam);
    v_tsuf = f_arr_suffix(v_et);
    v_o = scat(scat(scat(scat(scat(scat(scat(scat("({ ", f_cty(v_ut)), " __ha = "), f_gen_expr(v_sy, v_init)), "; arr_"), v_tsuf), " __hs = "), f_gen_expr(v_sy, v_arr)), "; ");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "for(int64_t __hi=0; __hi<__hs.len; __hi++){ "), f_cty(v_ut)), " v_"), arr_str_get(v_ps, 0)), " = __ha; "), f_cty(v_et)), " v_"), arr_str_get(v_ps, 1)), " = __hs.data[__hi]; ");
    v_o = scat(scat(scat(v_o, "__ha = "), f_gen_expr(v_sy, v_b)), "; } __ha; })");
    return v_o;
}

int64_t f_is_generic(s_Syms* v_sy, const char* v_name) {
    if ((arr_Func_len((v_sy)->gfns) == 0)) {
        return (1 != 1);
    }
    return map_str_str_has((v_sy)->evar, scat("@generic.", v_name));
}

int64_t f_is_tparam(s_Func v_f, const char* v_ty) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len((v_f).tparams))) {
        if ((strcmp(arr_str_get((v_f).tparams, v_i), v_ty) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

int64_t f_is_array_tparam(s_Func v_f, const char* v_ty) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len((v_f).tparams))) {
        if ((strcmp(v_ty, scat(scat("[", arr_str_get((v_f).tparams, v_i)), "]")) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

const char* f_subst_tparam(s_Func v_f, const char* v_ty, const char* v_concrete) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len((v_f).tparams))) {
        if ((strcmp(v_ty, arr_str_get((v_f).tparams, v_i)) == 0)) {
            return v_concrete;
        }
        if ((strcmp(v_ty, scat(scat("[", arr_str_get((v_f).tparams, v_i)), "]")) == 0)) {
            return scat(scat("[", v_concrete), "]");
        }
        v_i = (v_i + 1);
    }
    return v_ty;
}

s_Func f_find_gfn(s_Syms* v_sy, const char* v_name) {
    int64_t v_i;
    arr_Stmt v_nob;
    arr_str v_noa;
    v_i = 0;
    while ((v_i < arr_Func_len((v_sy)->gfns))) {
        if ((strcmp((arr_Func_get((v_sy)->gfns, v_i)).name, v_name) == 0)) {
            return arr_Func_get((v_sy)->gfns, v_i);
        }
        v_i = (v_i + 1);
    }
    v_nob = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
    v_noa = ({ arr_str __a = arr_str_new(); __a; });
    return mk_Func("", v_noa, v_noa, "", "", v_noa, v_nob);
}

const char* f_generic_T(s_Syms* v_sy, s_Func v_f, arr_Expr v_args) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len((v_f).ptypes))) {
        if ((v_i < arr_Expr_len(v_args))) {
            if (f_is_tparam(v_f, arr_str_get((v_f).ptypes, v_i))) {
                return f_type_of_expr(v_sy, arr_Expr_get(v_args, v_i));
            }
            if (f_is_array_tparam(v_f, arr_str_get((v_f).ptypes, v_i))) {
                return f_elem_of_ann(f_type_of_expr(v_sy, arr_Expr_get(v_args, v_i)));
            }
        }
        v_i = (v_i + 1);
    }
    return "i64";
}

const char* f_inline_stmt_value(s_Syms* v_sy, s_Stmt v_s) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = scat(f_gen_expr(v_sy, v_e), ";"); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = scat(f_gen_expr(v_sy, v_e), ";"); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = "0;"; } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = "0;"; } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = "0;"; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = "0;"; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = "0;"; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = "0;"; } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = "0;"; } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = "0;"; } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = "0;"; } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = "0;"; } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = "0;"; } else if(__s.tag==12){ __m = "0;"; } else if(__s.tag==13){ __m = "0;"; } __m; });
}

const char* f_gen_generic_call(s_Syms* v_sy, const char* v_name, arr_Expr v_args) {
    s_Func v_f;
    const char* v_gt;
    const char* v_o;
    int64_t v_i;
    const char* v_pt;
    int64_t v_n;
    int64_t v_j;
    v_f = f_find_gfn(v_sy, v_name);
    v_gt = f_generic_T(v_sy, v_f, v_args);
    v_o = "({ ";
    v_i = 0;
    while ((v_i < arr_str_len((v_f).params))) {
        v_pt = f_subst_tparam(v_f, arr_str_get((v_f).ptypes, v_i), v_gt);
        f_set_ty(v_sy, arr_str_get((v_f).params, v_i), v_pt);
        v_o = scat(scat(scat(scat(scat(scat(v_o, f_cty(v_pt)), " v_"), arr_str_get((v_f).params, v_i)), " = "), f_gen_expr(v_sy, arr_Expr_get(v_args, v_i))), "; ");
        v_i = (v_i + 1);
    }
    v_n = arr_Stmt_len((v_f).body);
    v_j = 0;
    while ((v_j < (v_n - 1))) {
        v_o = scat(v_o, f_gen_stmt(v_sy, arr_Stmt_get((v_f).body, v_j), ""));
        v_j = (v_j + 1);
    }
    if ((v_n > 0)) {
        v_o = scat(v_o, f_inline_stmt_value(v_sy, arr_Stmt_get((v_f).body, (v_n - 1))));
    }
    return scat(v_o, " })");
}

const char* f_gen_lambda(s_Syms* v_sy, int64_t v_id) {
    const char* v_idstr;
    arr_str v_caps;
    const char* v_out;
    int64_t v_k;
    v_idstr = i2s(v_id);
    v_caps = f_split_semi(map_str_str_get((v_sy)->evar, scat("@lamcaps.", v_idstr)));
    if ((arr_str_len(v_caps) == 0)) {
        return scat(scat("((closure_t){ (void*)__lam_", v_idstr), ", (void*)0 })");
    }
    v_out = scat(scat(scat(scat(scat(scat("({ __env_", v_idstr), "* __e = (__env_"), v_idstr), "*)GC_MALLOC(sizeof(__env_"), v_idstr), ")); ");
    v_k = 0;
    while ((v_k < arr_str_len(v_caps))) {
        v_out = scat(scat(scat(scat(scat(v_out, "__e->v_"), arr_str_get(v_caps, v_k)), " = v_"), arr_str_get(v_caps, v_k)), "; ");
        v_k = (v_k + 1);
    }
    return scat(scat(scat(v_out, "((closure_t){ (void*)__lam_"), v_idstr), ", (void*)__e }); })");
}

const char* f_gen_closure_call(s_Syms* v_sy, const char* v_fname, const char* v_ret, arr_Expr v_args) {
    const char* v_cv;
    arr_str v_pts;
    const char* v_cast;
    int64_t v_k;
    const char* v_pt;
    const char* v_out;
    v_cv = f_gen_var(v_sy, v_fname);
    v_pts = f_fn_params_of(f_ty_of(v_sy, v_fname));
    v_cast = scat(f_cty(v_ret), " (*)(void*");
    v_k = 0;
    while ((v_k < arr_Expr_len(v_args))) {
        v_pt = ({ const char* __r; if ((v_k < arr_str_len(v_pts))) { __r = arr_str_get(v_pts, v_k); } else { __r = "i64"; } __r; });
        v_cast = scat(scat(v_cast, ", "), f_cty(v_pt));
        v_k = (v_k + 1);
    }
    v_cast = scat(v_cast, ")");
    v_out = scat(scat(scat(scat(scat(scat("(((", v_cast), ")("), v_cv), ").fn)(("), v_cv), ").env");
    v_k = 0;
    while ((v_k < arr_Expr_len(v_args))) {
        v_out = scat(scat(v_out, ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, v_k)));
        v_k = (v_k + 1);
    }
    return scat(v_out, "))");
}

const char* f_class_parent(s_Syms* v_sy, const char* v_cls) {
    if (map_str_str_has((v_sy)->evar, scat("@class.parent.", v_cls))) {
        return map_str_str_get((v_sy)->evar, scat("@class.parent.", v_cls));
    }
    return "";
}

int64_t f_is_class_method(s_Syms* v_sy, const char* v_cls, const char* v_m) {
    const char* v_c;
    v_c = v_cls;
    while ((((int64_t)strlen(v_c)) > 0)) {
        if (map_str_str_has((v_sy)->evar, scat(scat(scat("@class.defines.", v_c), "."), v_m))) {
            return (1 == 1);
        }
        v_c = f_class_parent(v_sy, v_c);
    }
    return (1 != 1);
}

const char* f_defining_class(s_Syms* v_sy, const char* v_cls, const char* v_m) {
    const char* v_c;
    v_c = v_cls;
    while ((((int64_t)strlen(v_c)) > 0)) {
        if (map_str_str_has((v_sy)->evar, scat(scat(scat("@class.defines.", v_c), "."), v_m))) {
            return v_c;
        }
        v_c = f_class_parent(v_sy, v_c);
    }
    return v_cls;
}

const char* f_recv_ptr(s_Syms* v_sy, s_Expr v_e) {
    if (f_is_ptr_ann(f_type_of_expr(v_sy, v_e))) {
        return f_gen_expr(v_sy, v_e);
    }
    return scat(scat("(&", f_gen_expr(v_sy, v_e)), ")");
}

const char* f_gen_method_call(s_Syms* v_sy, const char* v_cls, const char* v_m, arr_Expr v_args) {
    const char* v_recv;
    const char* v_vo;
    int64_t v_vi;
    const char* v_def;
    const char* v_r;
    const char* v_out;
    int64_t v_i;
    v_recv = f_recv_ptr(v_sy, arr_Expr_get(v_args, 0));
    if (map_str_str_has((v_sy)->evar, scat(scat(scat("@class.virt.", v_cls), "."), v_m))) {
        v_vo = scat(scat(scat(scat(scat(scat(scat("((vt_", v_cls), "*)("), v_recv), ")->__vt)->"), v_m), "((void*)"), v_recv);
        v_vi = 1;
        while ((v_vi < arr_Expr_len(v_args))) {
            v_vo = scat(scat(v_vo, ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, v_vi)));
            v_vi = (v_vi + 1);
        }
        return scat(v_vo, ")");
    }
    v_def = f_defining_class(v_sy, v_cls, v_m);
    v_r = v_recv;
    if ((strcmp(v_def, v_cls) != 0)) {
        v_r = scat(scat(scat("(s_", v_def), "*)"), v_recv);
    }
    v_out = scat(scat(scat(scat(scat("f_", v_def), "_"), v_m), "("), v_r);
    v_i = 1;
    while ((v_i < arr_Expr_len(v_args))) {
        v_out = scat(scat(v_out, ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, v_i)));
        v_i = (v_i + 1);
    }
    return scat(v_out, ")");
}

const char* f_vsig_ret(s_Syms* v_sy, const char* v_cls, const char* v_m) {
    const char* v_c;
    v_c = v_cls;
    while ((((int64_t)strlen(v_c)) > 0)) {
        if (map_str_str_has((v_sy)->evar, scat(scat(scat("@class.vret.", v_c), "."), v_m))) {
            return map_str_str_get((v_sy)->evar, scat(scat(scat("@class.vret.", v_c), "."), v_m));
        }
        v_c = f_class_parent(v_sy, v_c);
    }
    return "void";
}

const char* f_vsig_args(s_Syms* v_sy, const char* v_cls, const char* v_m) {
    const char* v_c;
    v_c = v_cls;
    while ((((int64_t)strlen(v_c)) > 0)) {
        if (map_str_str_has((v_sy)->evar, scat(scat(scat("@class.vargs.", v_c), "."), v_m))) {
            return map_str_str_get((v_sy)->evar, scat(scat(scat("@class.vargs.", v_c), "."), v_m));
        }
        v_c = f_class_parent(v_sy, v_c);
    }
    return "";
}

const char* f_gen_vtable_typedef(s_Syms* v_sy, const char* v_cls) {
    const char* v_out;
    arr_str v_parts;
    int64_t v_i;
    const char* v_m;
    v_out = scat(scat("typedef struct vt_", v_cls), " {\n");
    v_parts = split(map_str_str_get((v_sy)->evar, scat("@class.vslots.", v_cls)), ";");
    v_i = 0;
    while ((v_i < arr_str_len(v_parts))) {
        v_m = arr_str_get(v_parts, v_i);
        if ((((int64_t)strlen(v_m)) > 0)) {
            v_out = scat(scat(scat(scat(scat(scat(scat(v_out, "  "), f_vsig_ret(v_sy, v_cls, v_m)), " (*"), v_m), ")(void*"), f_vsig_args(v_sy, v_cls, v_m)), ");\n");
        }
        v_i = (v_i + 1);
    }
    return scat(scat(scat(v_out, "} vt_"), v_cls), ";\n");
}

const char* f_gen_vtable_instance(s_Syms* v_sy, const char* v_cls) {
    const char* v_out;
    arr_str v_parts;
    int64_t v_i;
    int64_t v_first;
    const char* v_m;
    v_out = scat(scat(scat(scat("const vt_", v_cls), " __vtbl_"), v_cls), " = { ");
    v_parts = split(map_str_str_get((v_sy)->evar, scat("@class.vslots.", v_cls)), ";");
    v_i = 0;
    v_first = (1 == 1);
    while ((v_i < arr_str_len(v_parts))) {
        v_m = arr_str_get(v_parts, v_i);
        if ((((int64_t)strlen(v_m)) > 0)) {
            if ((v_first == (1 != 1))) {
                v_out = scat(v_out, ", ");
            }
            v_first = (1 != 1);
            v_out = scat(scat(scat(scat(scat(scat(scat(scat(v_out, "("), f_vsig_ret(v_sy, v_cls, v_m)), "(*)(void*"), f_vsig_args(v_sy, v_cls, v_m)), "))f_"), f_defining_class(v_sy, v_cls, v_m)), "_"), v_m);
        }
        v_i = (v_i + 1);
    }
    return scat(v_out, " };\n");
}

const char* f_gen_call(s_Syms* v_sy, const char* v_fname, arr_Expr v_args) {
    const char* v_pdef;
    const char* v_so;
    int64_t v_sj;
    const char* v_rcv;
    const char* v_t;
    const char* v_suf;
    if (f_is_generic(v_sy, v_fname)) {
        return f_gen_generic_call(v_sy, v_fname, v_args);
    }
    if ((((arr_Expr_len(v_args) >= 1) && (strcmp(f_var_name(arr_Expr_get(v_args, 0)), "super") == 0)) && map_str_str_has((v_sy)->evar, "@curclass"))) {
        v_pdef = f_defining_class(v_sy, f_class_parent(v_sy, map_str_str_get((v_sy)->evar, "@curclass")), v_fname);
        v_so = scat(scat(scat(scat(scat(scat("f_", v_pdef), "_"), v_fname), "((s_"), v_pdef), "*)v_self");
        v_sj = 1;
        while ((v_sj < arr_Expr_len(v_args))) {
            v_so = scat(scat(v_so, ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, v_sj)));
            v_sj = (v_sj + 1);
        }
        return scat(v_so, ")");
    }
    if ((arr_Expr_len(v_args) >= 1)) {
        v_rcv = f_under_ptr(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)));
        if ((map_str_str_has((v_sy)->evar, scat("@class.", v_rcv)) && f_is_class_method(v_sy, v_rcv, v_fname))) {
            return f_gen_method_call(v_sy, v_rcv, v_fname, v_args);
        }
    }
    if (((strcmp(v_fname, "map") == 0) && (arr_Expr_len(v_args) == 2))) {
        return f_gen_map(v_sy, arr_Expr_get(v_args, 0), arr_Expr_get(v_args, 1));
    }
    if (((strcmp(v_fname, "filter") == 0) && (arr_Expr_len(v_args) == 2))) {
        return f_gen_filter(v_sy, arr_Expr_get(v_args, 0), arr_Expr_get(v_args, 1));
    }
    if (((strcmp(v_fname, "reduce") == 0) && (arr_Expr_len(v_args) == 3))) {
        return f_gen_reduce(v_sy, arr_Expr_get(v_args, 0), arr_Expr_get(v_args, 1), arr_Expr_get(v_args, 2));
    }
    if (((strcmp(v_fname, "to_str") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if ((strcmp(v_t, "str") == 0)) {
            return f_gen_expr(v_sy, arr_Expr_get(v_args, 0));
        }
        if ((strcmp(v_t, "f64") == 0)) {
            return scat(scat("f2s(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        if ((strcmp(v_t, "bool") == 0)) {
            return scat(scat("((", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ") ? \"true\" : \"false\")");
        }
        if ((strcmp(v_t, "bytes") == 0)) {
            return scat(scat("bytes_to_str(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        return scat(scat("i2s(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "print") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if ((strcmp(v_t, "str") == 0)) {
            return scat(scat("printf(\"%s\", ", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        if ((strcmp(v_t, "f64") == 0)) {
            return scat(scat("printf(\"%g\", (double)(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "))");
        }
        if ((strcmp(v_t, "bool") == 0)) {
            return scat(scat("printf(\"%s\", (", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ") ? \"true\" : \"false\")");
        }
        if ((strcmp(v_t, "bytes") == 0)) {
            return scat(scat("print_bytes(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        return scat(scat("printf(\"%lld\", (long long)(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "))");
    }
    if (((strcmp(v_fname, "println") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if ((strcmp(v_t, "str") == 0)) {
            return scat(scat("printf(\"%s\\n\", ", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        if ((strcmp(v_t, "f64") == 0)) {
            return scat(scat("printf(\"%g\\n\", (double)(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "))");
        }
        if ((strcmp(v_t, "bool") == 0)) {
            return scat(scat("printf(\"%s\\n\", (", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ") ? \"true\" : \"false\")");
        }
        if ((strcmp(v_t, "bytes") == 0)) {
            return scat(scat("(print_bytes(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "), putchar(10))");
        }
        if ((f_is_map_ann(v_t) && f_is_scalar_ty(f_map_vtype(v_t)))) {
            return scat(scat(scat(scat("(", f_map_cty(v_t)), "_print("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "), putchar(10))");
        }
        return scat(scat("printf(\"%lld\\n\", (long long)(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "))");
    }
    if (((strcmp(v_fname, "int_to_str") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("i2s(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "str_to_int") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("s2i(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "float_to_str") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("f2s(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "str_to_float") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("s2f(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "cstr") == 0) && (arr_Expr_len(v_args) == 1))) {
        return f_gen_expr(v_sy, arr_Expr_get(v_args, 0));
    }
    if (((strcmp(v_fname, "substring") == 0) && (arr_Expr_len(v_args) == 3))) {
        return scat(scat(scat(scat(scat(scat("substr(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 2))), ")");
    }
    if (((strcmp(v_fname, "read_file") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("read_file_c(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "write_file") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat(scat(scat("write_file_c(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
    }
    if (((strcmp(v_fname, "str_to_bytes") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("str_to_bytes(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "bytes_to_str") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("bytes_to_str(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "bytes_at") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat(scat(scat("bytes_at(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
    }
    if (((strcmp(v_fname, "bytes_slice") == 0) && (arr_Expr_len(v_args) == 3))) {
        return scat(scat(scat(scat(scat(scat("bytes_slice(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 2))), ")");
    }
    if (((strcmp(v_fname, "read_file_bytes") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("read_file_bytes(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "write_file_bytes") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat(scat(scat("write_file_bytes(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
    }
    if (((strcmp(v_fname, "args") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "ailang_args()";
    }
    if (((strcmp(v_fname, "exit") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("exit((int)(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "))");
    }
    if (((strcmp(v_fname, "ok") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat(scat(scat("mk_ok_", f_arr_suffix(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)))), "("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "err") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat(scat(scat("mk_err_", f_arr_suffix(f_ty_of(v_sy, "@ret"))), "("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "unwrap") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ").ok");
    }
    if (((strcmp(v_fname, "is_ok") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("((", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ").tag==0)");
    }
    if (((strcmp(v_fname, "is_err") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("((", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ").tag!=0)");
    }
    if (((strcmp(v_fname, "err_msg") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ").err");
    }
    if ((((((int64_t)strlen(v_fname)) > 4) && (strcmp(substr(v_fname, 0, 4), "err_") == 0)) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat(scat(scat("mk_err_", substr(v_fname, 4, ((int64_t)strlen(v_fname)))), "("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "len") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_len("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        if ((strcmp(v_t, "bytes") == 0)) {
            return scat(scat("(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ").len");
        }
        if (f_is_map_ann(v_t)) {
            return scat(scat(scat(f_map_cty(v_t), "_len("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        return scat(scat("((int64_t)strlen(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), "))");
    }
    if (((strcmp(v_fname, "push") == 0) && (arr_Expr_len(v_args) == 2))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            v_suf = f_arr_suffix(f_elem_of_ann(v_t));
            return scat(scat(scat(scat(scat(scat("arr_", v_suf), "_push("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
        }
    }
    if (((strcmp(v_fname, "has") == 0) && (arr_Expr_len(v_args) == 2))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_map_ann(v_t)) {
            return scat(scat(scat(scat(scat(f_map_cty(v_t), "_has("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
        }
    }
    if (((strcmp(v_fname, "slice") == 0) && (arr_Expr_len(v_args) == 3))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat(scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_slice("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 2))), ")");
        }
    }
    if (((strcmp(v_fname, "reverse") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_reverse("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
    }
    if (((strcmp(v_fname, "sort") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_sort("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
    }
    if ((((strcmp(v_fname, "keys") == 0) || (strcmp(v_fname, "values") == 0)) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_map_ann(v_t)) {
            return scat(scat(scat(scat(scat(f_map_cty(v_t), "_"), v_fname), "("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
    }
    if (((strcmp(v_fname, "starts_with") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("starts_with(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "ends_with") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("ends_with(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "to_upper") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("to_upper(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "to_lower") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("to_lower(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "trim") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("trim(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "replace") == 0) && (arr_Expr_len(v_args) == 3))) {
        return scat(scat("str_replace(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "repeat") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("repeat(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "pad_left") == 0) && (arr_Expr_len(v_args) == 3))) {
        return scat(scat("pad_left(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "pad_right") == 0) && (arr_Expr_len(v_args) == 3))) {
        return scat(scat("pad_right(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "chr") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("chr(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "ord") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("ord(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "str_to_bool") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("str_to_bool(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "sign") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("sign(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "clamp") == 0) && (arr_Expr_len(v_args) == 3))) {
        return scat(scat("clamp(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "read_line") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "read_line()";
    }
    if (((strcmp(v_fname, "get_env") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("get_env(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "exe_dir") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "exe_dir()";
    }
    if (((strcmp(v_fname, "split") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat(scat(scat("split(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
    }
    if (((strcmp(v_fname, "now_ms") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "now_ms()";
    }
    if (((strcmp(v_fname, "now_us") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "now_us()";
    }
    if (((strcmp(v_fname, "mono_ms") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "mono_ms()";
    }
    if (((strcmp(v_fname, "sleep_ms") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("sleep_ms(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "time_iso") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("time_iso(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "flush") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "fflush(stdout)";
    }
    if (((strcmp(v_fname, "tcp_listen") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("tcp_listen(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "tcp_accept") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("tcp_accept(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "tcp_connect") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("tcp_connect(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "sock_send") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("sock_send(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "sock_send_str") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("sock_send_str(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "sock_recv") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("sock_recv(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "sock_close") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("sock_close(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "proc_fork") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "proc_fork()";
    }
    if (((strcmp(v_fname, "proc_getpid") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "proc_getpid()";
    }
    if (((strcmp(v_fname, "proc_no_zombies") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "proc_no_zombies()";
    }
    if (((strcmp(v_fname, "proc_reap") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "proc_reap()";
    }
    if (((strcmp(v_fname, "thread_spawn") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("thread_spawn(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "thread_join") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("thread_join(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "mutex_new") == 0) && (arr_Expr_len(v_args) == 0))) {
        return "mutex_new()";
    }
    if (((strcmp(v_fname, "mutex_lock") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("mutex_lock(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "mutex_unlock") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("mutex_unlock(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "chan_new") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("chan_new(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "chan_send") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("chan_send(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "chan_recv") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("chan_recv(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "chan_close") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("chan_close(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "regex_match") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("regex_match(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "regex_find") == 0) && (arr_Expr_len(v_args) == 2))) {
        return scat(scat("regex_find(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "format") == 0) && (arr_Expr_len(v_args) >= 1))) {
        return scat(scat("format(", f_gen_args(v_sy, v_args)), ")");
    }
    if (((strcmp(v_fname, "abs") == 0) && (arr_Expr_len(v_args) == 1))) {
        if ((strcmp(f_type_of_expr(v_sy, arr_Expr_get(v_args, 0)), "f64") == 0)) {
            return scat(scat("abs_f64(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
        return scat(scat("abs_i64(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "abs_i64") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("abs_i64(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "abs_f64") == 0) && (arr_Expr_len(v_args) == 1))) {
        return scat(scat("abs_f64(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
    }
    if (((strcmp(v_fname, "contains") == 0) && (arr_Expr_len(v_args) == 2))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_contains("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
        }
        return scat(scat(scat(scat("str_contains(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
    }
    if (((strcmp(v_fname, "index_of") == 0) && (arr_Expr_len(v_args) == 2))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_index_of("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
        }
        return scat(scat(scat(scat("str_index_of(", f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
    }
    if (((strcmp(v_fname, "pop") == 0) && (arr_Expr_len(v_args) == 1))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_pop("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ")");
        }
    }
    if (((strcmp(v_fname, "join") == 0) && (arr_Expr_len(v_args) == 2))) {
        v_t = f_type_of_expr(v_sy, arr_Expr_get(v_args, 0));
        if (f_is_array_ann(v_t)) {
            return scat(scat(scat(scat(scat(scat("arr_", f_arr_suffix(f_elem_of_ann(v_t))), "_join("), f_gen_expr(v_sy, arr_Expr_get(v_args, 0))), ", "), f_gen_expr(v_sy, arr_Expr_get(v_args, 1))), ")");
        }
    }
    if (f_is_native_call(v_fname)) {
        return scat(scat(scat(v_fname, "("), f_gen_args(v_sy, v_args)), ")");
    }
    if (f_is_ctor(v_sy, v_fname)) {
        return scat(scat(scat(scat("mk_", v_fname), "("), f_gen_args(v_sy, v_args)), ")");
    }
    if (f_is_variant(v_sy, v_fname)) {
        return scat(scat(scat(scat("mkv_", v_fname), "("), f_gen_args(v_sy, v_args)), ")");
    }
    if ((map_str_str_has((v_sy)->vty, v_fname) && f_is_fn_ann(map_str_str_get((v_sy)->vty, v_fname)))) {
        return f_gen_closure_call(v_sy, v_fname, f_fn_ret_of(map_str_str_get((v_sy)->vty, v_fname)), v_args);
    }
    return scat(scat(scat(scat("f_", v_fname), "("), f_gen_args(v_sy, v_args)), ")");
}

const char* f_gen_qq_index(s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, s_Expr v_r) {
    const char* v_ot;
    v_ot = f_type_of_expr(v_sy, v_obj);
    if (f_is_map_ann(v_ot)) {
        return scat(scat(scat(scat(scat(scat(scat(scat(scat(scat("(", f_map_cty(v_ot)), "_has("), f_gen_expr(v_sy, v_obj)), ", "), f_gen_expr(v_sy, v_idx)), ") ? "), f_gen_index(v_sy, v_obj, v_idx)), " : "), f_gen_expr(v_sy, v_r)), ")");
    }
    return f_gen_index(v_sy, v_obj, v_idx);
}

const char* f_gen_qq(s_Syms* v_sy, s_Expr v_l, s_Expr v_r) {
    return ({ const char* __m; s_Expr __s = v_l; if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_gen_qq_index(v_sy, v_obj, v_idx, v_r); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==10){ const char* v_mlt = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_e2 = *(__s.u.IfE.f2); __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = f_gen_expr(v_sy, v_r); } else if(__s.tag==18){ __m = f_gen_expr(v_sy, v_r); } __m; });
}

const char* f_gen_bin(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r) {
    if ((v_op == f_OP_QQ())) {
        return f_gen_qq(v_sy, v_l, v_r);
    }
    if ((((v_op == f_OP_ADD()) && f_expr_is_str(v_l, v_sy)) && f_expr_is_str(v_r, v_sy))) {
        return scat(scat(scat(scat("scat(", f_gen_expr(v_sy, v_l)), ", "), f_gen_expr(v_sy, v_r)), ")");
    }
    if ((v_op == f_OP_CAT())) {
        return scat(scat(scat(scat("scat(", f_gen_expr(v_sy, v_l)), ", "), f_gen_expr(v_sy, v_r)), ")");
    }
    if ((((v_op == f_OP_EQ()) && f_expr_is_str(v_l, v_sy)) && f_expr_is_str(v_r, v_sy))) {
        return scat(scat(scat(scat("(strcmp(", f_gen_expr(v_sy, v_l)), ", "), f_gen_expr(v_sy, v_r)), ") == 0)");
    }
    if ((((v_op == f_OP_NE()) && f_expr_is_str(v_l, v_sy)) && f_expr_is_str(v_r, v_sy))) {
        return scat(scat(scat(scat("(strcmp(", f_gen_expr(v_sy, v_l)), ", "), f_gen_expr(v_sy, v_r)), ") != 0)");
    }
    return scat(scat(scat(scat("(", f_gen_expr(v_sy, v_l)), f_op_c(v_op)), f_gen_expr(v_sy, v_r)), ")");
}

const char* f_gen_decl(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_ind) {
    f_set_ty(v_sy, v_name, f_type_of_expr(v_sy, v_e));
    return scat(scat(scat(scat(scat(v_ind, "v_"), v_name), " = "), f_gen_expr(v_sy, v_e)), ";\n");
}

const char* f_tuple_type_of(s_Syms* v_sy, arr_Expr v_elems) {
    const char* v_s;
    int64_t v_i;
    v_s = "(";
    v_i = 0;
    while ((v_i < arr_Expr_len(v_elems))) {
        if ((v_i > 0)) {
            v_s = scat(v_s, ";");
        }
        v_s = scat(v_s, f_type_of_expr(v_sy, arr_Expr_get(v_elems, v_i)));
        v_i = (v_i + 1);
    }
    return scat(v_s, ")");
}

const char* f_gen_tuple_lit(s_Syms* v_sy, arr_Expr v_elems) {
    const char* v_ty;
    const char* v_out;
    int64_t v_i;
    v_ty = f_tuple_type_of(v_sy, v_elems);
    v_out = scat(scat("(", f_cty(v_ty)), "){ ");
    v_i = 0;
    while ((v_i < arr_Expr_len(v_elems))) {
        if ((v_i > 0)) {
            v_out = scat(v_out, ", ");
        }
        v_out = scat(v_out, f_gen_expr(v_sy, arr_Expr_get(v_elems, v_i)));
        v_i = (v_i + 1);
    }
    return scat(v_out, " }");
}

const char* f_gen_destructure(s_Syms* v_sy, arr_str v_names, s_Expr v_e, const char* v_ind) {
    const char* v_ty;
    arr_str v_ets;
    const char* v_out;
    int64_t v_i;
    const char* v_ft;
    v_ty = f_type_of_expr(v_sy, v_e);
    v_ets = f_tuple_elems(v_ty);
    v_out = scat(scat(scat(scat(scat(v_ind, "{ "), f_cty(v_ty)), " __dt = "), f_gen_expr(v_sy, v_e)), ";");
    v_i = 0;
    while ((v_i < arr_str_len(v_names))) {
        if ((strcmp(arr_str_get(v_names, v_i), "_") != 0)) {
            v_ft = ({ const char* __r; if ((v_i < arr_str_len(v_ets))) { __r = arr_str_get(v_ets, v_i); } else { __r = "i64"; } __r; });
            f_set_ty(v_sy, arr_str_get(v_names, v_i), v_ft);
            v_out = scat(scat(scat(scat(scat(v_out, " v_"), arr_str_get(v_names, v_i)), " = __dt._"), i2s(v_i)), ";");
        }
        v_i = (v_i + 1);
    }
    return scat(v_out, " }\n");
}

const char* f_hoist_destructure(s_Syms* v_sy, arr_str v_names, s_Expr v_e, const char* v_ind) {
    const char* v_ty;
    arr_str v_ets;
    const char* v_out;
    int64_t v_i;
    const char* v_ft;
    v_ty = f_type_of_expr(v_sy, v_e);
    v_ets = f_tuple_elems(v_ty);
    v_out = "";
    v_i = 0;
    while ((v_i < arr_str_len(v_names))) {
        if ((strcmp(arr_str_get(v_names, v_i), "_") != 0)) {
            v_ft = ({ const char* __r; if ((v_i < arr_str_len(v_ets))) { __r = arr_str_get(v_ets, v_i); } else { __r = "i64"; } __r; });
            if ((f_declared(v_sy, arr_str_get(v_names, v_i)) == (1 != 1))) {
                f_set_ty(v_sy, arr_str_get(v_names, v_i), v_ft);
                v_out = scat(scat(scat(scat(scat(v_out, v_ind), f_cty(v_ft)), " v_"), arr_str_get(v_names, v_i)), ";\n");
            }
        }
        v_i = (v_i + 1);
    }
    return v_out;
}

const char* f_gen_tuple_typedef(const char* v_ty) {
    arr_str v_es;
    const char* v_out;
    int64_t v_i;
    v_es = f_tuple_elems(v_ty);
    v_out = "typedef struct { ";
    v_i = 0;
    while ((v_i < arr_str_len(v_es))) {
        v_out = scat(scat(scat(scat(v_out, f_cty(arr_str_get(v_es, v_i))), " _"), i2s(v_i)), "; ");
        v_i = (v_i + 1);
    }
    return scat(scat(scat(v_out, "} tup_"), f_tuple_suffix(v_ty)), ";\n");
}

const char* f_block_type(s_Syms* v_sy, arr_Stmt v_body) {
    int64_t v_n;
    v_n = arr_Stmt_len(v_body);
    if ((v_n == 0)) {
        return "i64";
    }
    return f_last_expr_type(v_sy, arr_Stmt_get(v_body, (v_n - 1)));
}

const char* f_gen_blk_decl(s_Syms* v_sy, const char* v_name, s_Expr v_e) {
    const char* v_ty;
    v_ty = f_type_of_expr(v_sy, v_e);
    f_set_ty(v_sy, v_name, v_ty);
    return scat(scat(scat(scat(scat(f_cty(v_ty), " v_"), v_name), " = "), f_gen_expr(v_sy, v_e)), "; ");
}

const char* f_gen_blk_stmt(s_Syms* v_sy, s_Stmt v_s) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_gen_blk_decl(v_sy, v_name, v_e); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==12){ __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==13){ __m = f_gen_stmt(v_sy, v_s, ""); } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = f_gen_stmt(v_sy, v_s, ""); } __m; });
}

const char* f_gen_blk_tail(s_Syms* v_sy, s_Stmt v_s) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = scat(f_gen_expr(v_sy, v_e), "; "); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = scat(f_gen_expr(v_sy, v_e), "; "); } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = scat(f_gen_blk_decl(v_sy, v_name, v_e), "0; "); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = scat(f_gen_stmt(v_sy, v_s, ""), "0; "); } else if(__s.tag==12){ __m = "break; 0; "; } else if(__s.tag==13){ __m = "continue; 0; "; } __m; });
}

const char* f_gen_block_e(s_Syms* v_sy, arr_Stmt v_body) {
    int64_t v_n;
    const char* v_out;
    int64_t v_i;
    v_n = arr_Stmt_len(v_body);
    if ((v_n == 0)) {
        return "0";
    }
    v_out = "({ ";
    v_i = 0;
    while ((v_i < (v_n - 1))) {
        v_out = scat(v_out, f_gen_blk_stmt(v_sy, arr_Stmt_get(v_body, v_i)));
        v_i = (v_i + 1);
    }
    return scat(scat(v_out, f_gen_blk_tail(v_sy, arr_Stmt_get(v_body, (v_n - 1)))), "})");
}

const char* f_gen_if(s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb, const char* v_ind) {
    const char* v_out;
    v_out = scat(scat(scat(scat(scat(scat(v_ind, "if ("), f_gen_expr(v_sy, v_c)), ") {\n"), f_gen_stmts(v_sy, v_b, scat(v_ind, "    "))), v_ind), "}");
    if ((arr_Stmt_len(v_eb) > 0)) {
        v_out = scat(scat(scat(scat(v_out, " else {\n"), f_gen_stmts(v_sy, v_eb, scat(v_ind, "    "))), v_ind), "}");
    }
    return scat(v_out, "\n");
}

const char* f_gen_stmts(s_Syms* v_sy, arr_Stmt v_body, const char* v_ind) {
    const char* v_out;
    int64_t v_i;
    v_out = "";
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_out = scat(v_out, f_gen_stmt(v_sy, arr_Stmt_get(v_body, v_i), v_ind));
        v_i = (v_i + 1);
    }
    return v_out;
}

const char* f_hoist_decls(s_Syms* v_sy, arr_Stmt v_body, const char* v_ind) {
    const char* v_out;
    int64_t v_i;
    v_out = "";
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_out = scat(v_out, f_hoist_stmt(v_sy, arr_Stmt_get(v_body, v_i), v_ind));
        v_i = (v_i + 1);
    }
    return v_out;
}

const char* f_hoist_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_ind) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_hoist_one(v_sy, v_name, v_e, v_ind); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_hoist_destructure(v_sy, v_names, v_e, v_ind); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = scat(f_hoist_decls(v_sy, v_b, v_ind), f_hoist_decls(v_sy, v_eb, v_ind)); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_hoist_decls(v_sy, v_b, v_ind); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_hoist_loopin(v_sy, v_vnm, v_coll, v_b, v_ind); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_hoist_loopkv(v_sy, v_kn, v_vn, v_coll, v_b, v_ind); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_hoist_range(v_sy, v_v, v_b, v_ind); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = ""; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = ""; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = ""; } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = ""; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = ""; } else if(__s.tag==12){ __m = ""; } else if(__s.tag==13){ __m = ""; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = ""; } __m; });
}

const char* f_hoist_one(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_ind) {
    const char* v_ety;
    if (f_declared(v_sy, v_name)) {
        return "";
    }
    v_ety = f_type_of_expr(v_sy, v_e);
    f_set_ty(v_sy, v_name, v_ety);
    return scat(scat(scat(scat(v_ind, f_cty(v_ety)), " v_"), v_name), ";\n");
}

const char* f_hoist_loopin(s_Syms* v_sy, const char* v_vnm, s_Expr v_coll, arr_Stmt v_b, const char* v_ind) {
    f_set_ty(v_sy, v_vnm, f_coll_elem_type(v_sy, v_coll));
    return f_hoist_decls(v_sy, v_b, v_ind);
}

const char* f_hoist_loopkv(s_Syms* v_sy, const char* v_kn, const char* v_vn, s_Expr v_coll, arr_Stmt v_b, const char* v_ind) {
    const char* v_t;
    v_t = f_type_of_expr(v_sy, v_coll);
    f_set_ty(v_sy, v_kn, f_map_ktype(v_t));
    f_set_ty(v_sy, v_vn, f_map_vtype(v_t));
    return f_hoist_decls(v_sy, v_b, v_ind);
}

const char* f_gen_fn_body(s_Syms* v_sy, arr_Stmt v_body, const char* v_ret, const char* v_ind) {
    int64_t v_n;
    const char* v_out;
    int64_t v_i;
    v_n = arr_Stmt_len(v_body);
    if ((v_n == 0)) {
        return "";
    }
    if ((((int64_t)strlen(v_ret)) == 0)) {
        return f_gen_stmts(v_sy, v_body, v_ind);
    }
    v_out = "";
    v_i = 0;
    while ((v_i < (v_n - 1))) {
        v_out = scat(v_out, f_gen_stmt(v_sy, arr_Stmt_get(v_body, v_i), v_ind));
        v_i = (v_i + 1);
    }
    return scat(v_out, f_gen_tail_stmt(v_sy, arr_Stmt_get(v_body, (v_n - 1)), v_ret, v_ind));
}

const char* f_gen_tail_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_ret, const char* v_ind) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = scat(scat(scat(v_ind, "return "), f_gen_expr(v_sy, v_e)), ";\n"); } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_gen_decl(v_sy, v_name, v_e, v_ind); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_gen_destructure(v_sy, v_names, v_e, v_ind); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = f_gen_assign(v_sy, v_name, v_e, v_ind); } else if(__s.tag==3){ s_Expr v_obj = *(__s.u.SIdxAssign.f0); s_Expr v_idx = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = f_gen_idx_assign(v_sy, v_obj, v_idx, v_e, v_ind); } else if(__s.tag==4){ s_Expr v_obj = *(__s.u.SFieldAssign.f0); const char* v_fnm = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = scat(scat(scat(scat(v_ind, f_gen_field(v_sy, v_obj, v_fnm)), " = "), f_gen_expr(v_sy, v_e)), ";\n"); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = scat(scat(scat(v_ind, "return "), f_gen_expr(v_sy, v_e)), ";\n"); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = f_gen_print(v_sy, v_e, v_ind); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_gen_tail_if(v_sy, v_c, v_b, v_eb, v_ret, v_ind); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = scat(scat(scat(scat(scat(scat(v_ind, "while ("), f_gen_expr(v_sy, v_c)), ") {\n"), f_gen_stmts(v_sy, v_b, scat(v_ind, "    "))), v_ind), "}\n"); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_gen_loop_in(v_sy, v_vnm, v_coll, v_b, v_ind); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_gen_loop_kv(v_sy, v_kn, v_vn, v_coll, v_b, v_ind); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_gen_loop_range(v_sy, v_v, v_lo, v_hi, v_b, v_ind); } else if(__s.tag==12){ __m = scat(v_ind, "break;\n"); } else if(__s.tag==13){ __m = scat(v_ind, "continue;\n"); } __m; });
}

const char* f_gen_tail_if(s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb, const char* v_ret, const char* v_ind) {
    const char* v_out;
    v_out = scat(scat(scat(scat(scat(scat(v_ind, "if ("), f_gen_expr(v_sy, v_c)), ") {\n"), f_gen_fn_body(v_sy, v_b, v_ret, scat(v_ind, "    "))), v_ind), "}");
    if ((arr_Stmt_len(v_eb) > 0)) {
        v_out = scat(scat(scat(scat(v_out, " else {\n"), f_gen_fn_body(v_sy, v_eb, v_ret, scat(v_ind, "    "))), v_ind), "}");
    }
    return scat(v_out, "\n");
}

const char* f_gen_print(s_Syms* v_sy, s_Expr v_e, const char* v_ind) {
    const char* v_t;
    const char* v_suf;
    v_t = f_type_of_expr(v_sy, v_e);
    if ((strcmp(v_t, "str") == 0)) {
        return scat(scat(scat(v_ind, "printf(\"%s\\n\", "), f_gen_expr(v_sy, v_e)), ");\n");
    }
    if ((strcmp(v_t, "f64") == 0)) {
        return scat(scat(scat(v_ind, "printf(\"%g\\n\", (double)("), f_gen_expr(v_sy, v_e)), "));\n");
    }
    if ((strcmp(v_t, "bool") == 0)) {
        if (f_expr_is_cmp(v_e)) {
            return scat(scat(scat(v_ind, "printf(\"%lld\\n\", (long long)("), f_gen_expr(v_sy, v_e)), "));\n");
        }
        return scat(scat(scat(v_ind, "printf(\"%s\\n\", ("), f_gen_expr(v_sy, v_e)), ") ? \"true\" : \"false\");\n");
    }
    if ((strcmp(v_t, "bytes") == 0)) {
        return scat(scat(scat(v_ind, "print_bytes("), f_gen_expr(v_sy, v_e)), "); putchar(10);\n");
    }
    if (map_str_str_has((v_sy)->ctors, v_t)) {
        return scat(scat(scat(scat(scat(v_ind, "print_"), v_t), "("), f_gen_expr(v_sy, v_e)), "); putchar(10);\n");
    }
    if (f_is_array_ann(v_t)) {
        v_suf = f_arr_suffix(f_elem_of_ann(v_t));
        if (((strcmp(v_suf, "i64") == 0) || (strcmp(v_suf, "str") == 0))) {
            return scat(scat(scat(scat(scat(v_ind, "print_arr_"), v_suf), "("), f_gen_expr(v_sy, v_e)), "); putchar(10);\n");
        }
    }
    if ((f_is_map_ann(v_t) && f_is_scalar_ty(f_map_vtype(v_t)))) {
        return scat(scat(scat(scat(v_ind, f_map_cty(v_t)), "_print("), f_gen_expr(v_sy, v_e)), "); putchar(10);\n");
    }
    return scat(scat(scat(v_ind, "printf(\"%lld\\n\", (long long)("), f_gen_expr(v_sy, v_e)), "));\n");
}

const char* f_gen_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_ind) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_gen_decl(v_sy, v_name, v_e, v_ind); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_gen_destructure(v_sy, v_names, v_e, v_ind); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = f_gen_assign(v_sy, v_name, v_e, v_ind); } else if(__s.tag==3){ s_Expr v_obj = *(__s.u.SIdxAssign.f0); s_Expr v_idx = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = f_gen_idx_assign(v_sy, v_obj, v_idx, v_e, v_ind); } else if(__s.tag==4){ s_Expr v_obj = *(__s.u.SFieldAssign.f0); const char* v_fnm = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = scat(scat(scat(scat(v_ind, f_gen_field(v_sy, v_obj, v_fnm)), " = "), f_gen_expr(v_sy, v_e)), ";\n"); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = scat(scat(scat(v_ind, "return "), f_gen_expr(v_sy, v_e)), ";\n"); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = f_gen_print(v_sy, v_e, v_ind); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_gen_if(v_sy, v_c, v_b, v_eb, v_ind); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = scat(scat(scat(scat(scat(scat(v_ind, "while ("), f_gen_expr(v_sy, v_c)), ") {\n"), f_gen_stmts(v_sy, v_b, scat(v_ind, "    "))), v_ind), "}\n"); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_gen_loop_in(v_sy, v_vnm, v_coll, v_b, v_ind); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_gen_loop_kv(v_sy, v_kn, v_vn, v_coll, v_b, v_ind); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_gen_loop_range(v_sy, v_v, v_lo, v_hi, v_b, v_ind); } else if(__s.tag==12){ __m = scat(v_ind, "break;\n"); } else if(__s.tag==13){ __m = scat(v_ind, "continue;\n"); } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = scat(scat(v_ind, f_gen_expr(v_sy, v_e)), ";\n"); } __m; });
}

const char* f_gen_assign(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_ind) {
    const char* v_et;
    v_et = f_type_of_expr(v_sy, v_e);
    if (f_is_array_ann(v_et)) {
        f_set_ty(v_sy, v_name, v_et);
    }
    return scat(scat(scat(scat(scat(v_ind, "v_"), v_name), " = "), f_gen_expr(v_sy, v_e)), ";\n");
}

const char* f_gen_idx_assign(s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, s_Expr v_e, const char* v_ind) {
    const char* v_ot;
    v_ot = f_type_of_expr(v_sy, v_obj);
    if (f_is_array_ann(v_ot)) {
        return scat(scat(scat(scat(scat(scat(scat(v_ind, "("), f_gen_expr(v_sy, v_obj)), ").data["), f_gen_expr(v_sy, v_idx)), "] = "), f_gen_expr(v_sy, v_e)), ";\n");
    }
    if (f_is_map_ann(v_ot)) {
        return scat(scat(scat(scat(scat(scat(scat(scat(v_ind, f_map_cty(v_ot)), "_set("), f_gen_expr(v_sy, v_obj)), ", "), f_gen_expr(v_sy, v_idx)), ", "), f_gen_expr(v_sy, v_e)), ");\n");
    }
    return scat(v_ind, "/* unsupported index-assign */\n");
}

const char* f_coll_elem_type(s_Syms* v_sy, s_Expr v_coll) {
    const char* v_colt;
    v_colt = f_type_of_expr(v_sy, v_coll);
    if (f_is_array_ann(v_colt)) {
        return f_elem_of_ann(v_colt);
    }
    return "i64";
}

const char* f_gen_loop_in(s_Syms* v_sy, const char* v_vnm, s_Expr v_coll, arr_Stmt v_body, const char* v_ind) {
    const char* v_et;
    const char* v_suf;
    const char* v_iv;
    const char* v_cv;
    const char* v_out;
    v_et = f_coll_elem_type(v_sy, v_coll);
    v_suf = f_arr_suffix(v_et);
    v_iv = scat("__i_", v_vnm);
    v_cv = scat("__c_", v_vnm);
    f_set_ty(v_sy, v_vnm, v_et);
    v_out = scat(scat(scat(scat(scat(scat(scat(v_ind, "{ arr_"), v_suf), " "), v_cv), " = "), f_gen_expr(v_sy, v_coll)), ";\n");
    v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_out, v_ind), "for (int64_t "), v_iv), " = 0; "), v_iv), " < arr_"), v_suf), "_len("), v_cv), "); "), v_iv), " = "), v_iv), " + 1) {\n");
    v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_out, v_ind), "    "), f_cty(v_et)), " v_"), v_vnm), " = arr_"), v_suf), "_get("), v_cv), ", "), v_iv), ");\n");
    v_out = scat(v_out, f_gen_stmts(v_sy, v_body, scat(v_ind, "    ")));
    v_out = scat(scat(v_out, v_ind), "} }\n");
    return v_out;
}

const char* f_gen_loop_range(s_Syms* v_sy, const char* v_vnm, s_Expr v_lo, s_Expr v_hi, arr_Stmt v_body, const char* v_ind) {
    const char* v_v;
    const char* v_out;
    f_set_ty(v_sy, v_vnm, "i64");
    v_v = scat("v_", v_vnm);
    v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_ind, "for (int64_t "), v_v), " = "), f_gen_expr(v_sy, v_lo)), "; "), v_v), " < "), f_gen_expr(v_sy, v_hi)), "; "), v_v), " = "), v_v), " + 1) {\n");
    v_out = scat(v_out, f_gen_stmts(v_sy, v_body, scat(v_ind, "    ")));
    v_out = scat(scat(v_out, v_ind), "}\n");
    return v_out;
}

const char* f_hoist_range(s_Syms* v_sy, const char* v_vnm, arr_Stmt v_b, const char* v_ind) {
    f_set_ty(v_sy, v_vnm, "i64");
    return f_hoist_decls(v_sy, v_b, v_ind);
}

int64_t f_collect_range_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_lo, s_Expr v_hi, arr_Stmt v_b) {
    f_collect_lams_expr(v_base, v_sy, v_lo);
    f_collect_lams_expr(v_base, v_sy, v_hi);
    f_collect_lams(v_base, v_sy, v_b);
    return 0;
}

const char* f_gen_loop_kv(s_Syms* v_sy, const char* v_kn, const char* v_vn, s_Expr v_coll, arr_Stmt v_body, const char* v_ind) {
    const char* v_mt2;
    const char* v_mc;
    const char* v_vt;
    const char* v_nv;
    const char* v_mv;
    const char* v_out;
    v_mt2 = f_type_of_expr(v_sy, v_coll);
    v_mc = f_map_cty(v_mt2);
    v_vt = f_map_vtype(v_mt2);
    v_nv = scat("__n_", v_kn);
    v_mv = scat("__m_", v_kn);
    f_set_ty(v_sy, v_kn, f_map_ktype(v_mt2));
    f_set_ty(v_sy, v_vn, v_vt);
    v_out = scat(scat(scat(scat(scat(scat(scat(v_ind, "{ "), v_mc), " "), v_mv), " = "), f_gen_expr(v_sy, v_coll)), ";\n");
    v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_out, v_ind), "for (int64_t "), v_nv), " = 0; "), v_nv), " < "), v_mv), "->cap; "), v_nv), " += 1) {\n");
    v_out = scat(scat(scat(scat(scat(scat(v_out, v_ind), "    if (!"), v_mv), "->occupied["), v_nv), "]) continue;\n");
    v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_out, v_ind), "    "), f_cty(f_map_ktype(v_mt2))), " v_"), v_kn), " = "), v_mv), "->keys["), v_nv), "];\n");
    v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_out, v_ind), "    "), f_cty(v_vt)), " v_"), v_vn), " = "), v_mv), "->values["), v_nv), "];\n");
    v_out = scat(v_out, f_gen_stmts(v_sy, v_body, scat(v_ind, "    ")));
    v_out = scat(scat(v_out, v_ind), "} }\n");
    return v_out;
}

const char* f_cty_ret(const char* v_ret) {
    if ((((int64_t)strlen(v_ret)) == 0)) {
        return "void";
    }
    return f_cty(v_ret);
}

const char* f_gen_sig(s_Func v_f, const char* v_ret) {
    const char* v_out;
    int64_t v_i;
    v_out = scat(scat(scat(f_cty_ret(v_ret), " f_"), (v_f).name), "(");
    if ((arr_str_len((v_f).params) == 0)) {
        v_out = scat(v_out, "void");
    } else {
        v_i = 0;
        while ((v_i < arr_str_len((v_f).params))) {
            if ((v_i > 0)) {
                v_out = scat(v_out, ", ");
            }
            v_out = scat(scat(scat(v_out, f_cty(arr_str_get((v_f).ptypes, v_i))), " v_"), arr_str_get((v_f).params, v_i));
            v_i = (v_i + 1);
        }
    }
    return scat(v_out, ")");
}

const char* f_gen_lam_sig(s_Func v_lf) {
    const char* v_out;
    int64_t v_k;
    const char* v_pt;
    v_out = scat(scat(scat(scat("static ", f_cty((v_lf).ret)), " __lam_"), (v_lf).name), "(void* __env");
    v_k = 0;
    while ((v_k < arr_str_len((v_lf).params))) {
        v_pt = ({ const char* __r; if ((v_k < arr_str_len((v_lf).ptypes))) { __r = arr_str_get((v_lf).ptypes, v_k); } else { __r = "i64"; } __r; });
        v_out = scat(scat(scat(scat(v_out, ", "), f_cty(v_pt)), " v_"), arr_str_get((v_lf).params, v_k));
        v_k = (v_k + 1);
    }
    return scat(v_out, ")");
}

const char* f_gen_env_struct(s_Syms* v_base, s_Func v_lf) {
    arr_str v_caps;
    arr_str v_capt;
    const char* v_out;
    int64_t v_k;
    v_caps = f_split_semi(map_str_str_get((v_base)->evar, scat("@lamcaps.", (v_lf).name)));
    v_capt = f_split_semi(map_str_str_get((v_base)->evar, scat("@lamcapt.", (v_lf).name)));
    if ((arr_str_len(v_caps) == 0)) {
        return "";
    }
    v_out = "typedef struct { ";
    v_k = 0;
    while ((v_k < arr_str_len(v_caps))) {
        v_out = scat(scat(scat(scat(v_out, f_cty(arr_str_get(v_capt, v_k))), " v_"), arr_str_get(v_caps, v_k)), "; ");
        v_k = (v_k + 1);
    }
    return scat(scat(scat(v_out, "} __env_"), (v_lf).name), ";\n");
}

const char* f_gen_lifted_body(s_Syms* v_base, s_Func v_lf) {
    arr_str v_caps;
    arr_str v_capt;
    s_Syms v_sy;
    int64_t v_pj;
    const char* v_pt;
    int64_t v_cj;
    const char* v_out;
    int64_t v_uk;
    v_caps = f_split_semi(map_str_str_get((v_base)->evar, scat("@lamcaps.", (v_lf).name)));
    v_capt = f_split_semi(map_str_str_get((v_base)->evar, scat("@lamcapt.", (v_lf).name)));
    v_sy = mk_Syms((v_base)->vty, (v_base)->fld, (v_base)->ctors, (v_base)->frets, (v_base)->evar, (v_base)->vft, (v_base)->gfns, (v_base)->lams);
    (v_sy).vty = map_str_str_new();
    v_pj = 0;
    while ((v_pj < arr_str_len((v_lf).params))) {
        v_pt = ({ const char* __r; if ((v_pj < arr_str_len((v_lf).ptypes))) { __r = arr_str_get((v_lf).ptypes, v_pj); } else { __r = "i64"; } __r; });
        f_set_ty((&v_sy), arr_str_get((v_lf).params, v_pj), v_pt);
        v_pj = (v_pj + 1);
    }
    v_cj = 0;
    while ((v_cj < arr_str_len(v_caps))) {
        f_set_ty((&v_sy), arr_str_get(v_caps, v_cj), arr_str_get(v_capt, v_cj));
        v_cj = (v_cj + 1);
    }
    v_out = scat(f_gen_lam_sig(v_lf), "{ ");
    if ((arr_str_len(v_caps) > 0)) {
        v_out = scat(scat(scat(scat(scat(v_out, "__env_"), (v_lf).name), "* __e = (__env_"), (v_lf).name), "*)__env; ");
        v_uk = 0;
        while ((v_uk < arr_str_len(v_caps))) {
            v_out = scat(scat(scat(scat(scat(scat(v_out, f_cty(arr_str_get(v_capt, v_uk))), " v_"), arr_str_get(v_caps, v_uk)), " = __e->v_"), arr_str_get(v_caps, v_uk)), "; ");
            v_uk = (v_uk + 1);
        }
    } else {
        v_out = scat(v_out, "(void)__env; ");
    }
    return scat(scat(v_out, f_gen_fn_body((&v_sy), (v_lf).body, (v_lf).ret, "")), "}\n");
}

const char* f_gen_arr_typedef(const char* v_suf, const char* v_et) {
    return scat(scat(scat(scat("typedef struct { int64_t len; int64_t cap; ", v_et), "* data; } arr_"), v_suf), ";\n");
}

const char* f_gen_arr_helpers(const char* v_suf, const char* v_et) {
    const char* v_o;
    v_o = scat(scat(scat(scat(scat(scat("static arr_", v_suf), " arr_"), v_suf), "_new(void){ arr_"), v_suf), " a; a.len=0; a.cap=0; a.data=0; return a; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static arr_"), v_suf), " arr_"), v_suf), "_push(arr_"), v_suf), " a, "), v_et), " x){ if(a.cap<=a.len){ int64_t nc=a.cap?a.cap*2:4; "), v_et), "* nd=("), v_et), "*)GC_MALLOC(nc*sizeof("), v_et), ")); if(a.len) memcpy(nd,a.data,a.len*sizeof("), v_et), ")); a.data=nd; a.cap=nc; } a.data[a.len]=x; a.len++; return a; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(v_o, "static "), v_et), " arr_"), v_suf), "_get(arr_"), v_suf), " a, int64_t i){ return a.data[i]; }\n");
    v_o = scat(scat(scat(scat(scat(v_o, "static int64_t arr_"), v_suf), "_len(arr_"), v_suf), " a){ return a.len; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(v_o, "static arr_"), v_suf), " arr_"), v_suf), "_pop(arr_"), v_suf), " a){ if(a.len>0) a.len-=1; return a; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static arr_"), v_suf), " arr_"), v_suf), "_slice(arr_"), v_suf), " a, int64_t lo, int64_t hi){ arr_"), v_suf), " r=arr_"), v_suf), "_new(); if(lo<0)lo=0; if(hi>a.len)hi=a.len; for(int64_t i=lo;i<hi;i++) r=arr_"), v_suf), "_push(r,a.data[i]); return r; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static arr_"), v_suf), " arr_"), v_suf), "_reverse(arr_"), v_suf), " a){ arr_"), v_suf), " r=arr_"), v_suf), "_new(); for(int64_t i=a.len-1;i>=0;i--) r=arr_"), v_suf), "_push(r,a.data[i]); return r; }\n");
    if ((strcmp(v_suf, "i64") == 0)) {
        v_o = scat(v_o, "static int __cmp_i64(const void* a, const void* b){ int64_t x=*(const int64_t*)a, y=*(const int64_t*)b; return (x>y)-(x<y); }\n");
        v_o = scat(v_o, "static arr_i64 arr_i64_sort(arr_i64 a){ arr_i64 r=arr_i64_reverse(arr_i64_reverse(a)); if(r.len>1) qsort(r.data,r.len,sizeof(int64_t),__cmp_i64); return r; }\n");
        v_o = scat(v_o, "static int64_t arr_i64_contains(arr_i64 a, int64_t x){ for(int64_t i=0;i<a.len;i++) if(a.data[i]==x) return 1; return 0; }\n");
        v_o = scat(v_o, "static int64_t arr_i64_index_of(arr_i64 a, int64_t x){ for(int64_t i=0;i<a.len;i++) if(a.data[i]==x) return i; return -1; }\n");
        v_o = scat(v_o, "static const char* arr_i64_join(arr_i64 a, const char* sep){ if(!sep) sep=\"\"; size_t sl=strlen(sep); size_t cap=(size_t)a.len*22+(a.len>1?sl*(size_t)(a.len-1):0)+1; if(cap<16) cap=16; char* o=(char*)GC_MALLOC(cap); char* w=o; for(int64_t i=0;i<a.len;i++){ if(i>0&&sl){ memcpy(w,sep,sl); w+=sl; } int n=snprintf(w,cap-(size_t)(w-o),\"%lld\",(long long)a.data[i]); if(n>0) w+=n; } *w=0; return o; }\n");
    }
    if ((strcmp(v_suf, "str") == 0)) {
        v_o = scat(v_o, "static int __cmp_str(const void* a, const void* b){ return strcmp(*(const char* const*)a, *(const char* const*)b); }\n");
        v_o = scat(v_o, "static arr_str arr_str_sort(arr_str a){ arr_str r=arr_str_reverse(arr_str_reverse(a)); if(r.len>1) qsort(r.data,r.len,sizeof(const char*),__cmp_str); return r; }\n");
        v_o = scat(v_o, "static int64_t arr_str_contains(arr_str a, const char* x){ for(int64_t i=0;i<a.len;i++) if(strcmp(a.data[i],x)==0) return 1; return 0; }\n");
        v_o = scat(v_o, "static int64_t arr_str_index_of(arr_str a, const char* x){ for(int64_t i=0;i<a.len;i++) if(strcmp(a.data[i],x)==0) return i; return -1; }\n");
        v_o = scat(v_o, "static const char* arr_str_join(arr_str a, const char* sep){ if(!sep) sep=\"\"; size_t sl=strlen(sep); size_t tot=0; for(int64_t i=0;i<a.len;i++) tot+=a.data[i]?strlen(a.data[i]):0; if(a.len>1) tot+=sl*(size_t)(a.len-1); char* o=(char*)GC_MALLOC(tot+1); char* w=o; for(int64_t i=0;i<a.len;i++){ if(i>0&&sl){ memcpy(w,sep,sl); w+=sl; } const char* s=a.data[i]?a.data[i]:\"\"; size_t n=strlen(s); if(n){ memcpy(w,s,n); w+=n; } } *w=0; return o; }\n");
    }
    return v_o;
}

const char* f_pf_spec(const char* v_t) {
    if ((strcmp(v_t, "str") == 0)) {
        return "\\\"%s\\\"";
    }
    if ((strcmp(v_t, "f64") == 0)) {
        return "%g";
    }
    return "%lld";
}

const char* f_pf_arg(const char* v_t, const char* v_slot) {
    if ((strcmp(v_t, "str") == 0)) {
        return scat(scat(scat(scat("(", v_slot), " ? "), v_slot), " : \"\")");
    }
    if ((strcmp(v_t, "f64") == 0)) {
        return scat(scat("(double)(", v_slot), ")");
    }
    return scat(scat("(long long)(", v_slot), ")");
}

int64_t f_is_scalar_ty(const char* v_t) {
    return (((strcmp(v_t, "str") == 0) || (strcmp(v_t, "i64") == 0)) || (strcmp(v_t, "f64") == 0));
}

const char* f_gen_map_print(const char* v_nm, const char* v_kt, const char* v_vt) {
    const char* v_fmt;
    const char* v_ka;
    const char* v_va;
    if (f_is_scalar_ty(v_vt)) {
        v_fmt = scat(scat(f_pf_spec(v_kt), ": "), f_pf_spec(v_vt));
        v_ka = f_pf_arg(v_kt, "m->keys[i]");
        v_va = f_pf_arg(v_vt, "m->values[i]");
        return scat(scat(scat(scat(scat(scat(scat(scat(scat(scat("static void ", v_nm), "_print("), v_nm), " m){ printf(\"{\"); int64_t __f=1; for(int64_t i=0;i<m->cap;i++){ if(!m->occupied[i]) continue; if(!__f) printf(\", \"); __f=0; printf(\""), v_fmt), "\", "), v_ka), ", "), v_va), "); } printf(\"}\"); }\n");
    }
    return scat(scat(scat(scat("static void ", v_nm), "_print("), v_nm), " m){ (void)m; }\n");
}

const char* f_gen_map_fwd(const char* v_nm) {
    const char* v_o;
    v_o = scat(scat(scat(scat("typedef struct ", v_nm), "_s "), v_nm), "_s;\n");
    v_o = scat(scat(scat(scat(scat(v_o, "typedef "), v_nm), "_s* "), v_nm), ";\n");
    return v_o;
}

const char* f_gen_map_node(const char* v_nm, const char* v_kt, const char* v_vt) {
    return scat(scat(scat(scat(scat(scat("struct ", v_nm), "_s { int64_t cap; int64_t len; "), f_cty(v_kt)), "* keys; "), f_cty(v_vt)), "* values; unsigned char* occupied; };\n");
}

const char* f_gen_map_helpers(const char* v_nm, const char* v_kt, const char* v_vt, const char* v_dflt) {
    const char* v_ksuf;
    const char* v_vsuf;
    const char* v_kc;
    const char* v_vc;
    const char* v_keyeq;
    const char* v_o;
    v_ksuf = f_arr_suffix(v_kt);
    v_vsuf = f_arr_suffix(v_vt);
    v_kc = f_cty(v_kt);
    v_vc = f_cty(v_vt);
    v_keyeq = ({ const char* __r; if ((strcmp(v_kt, "str") == 0)) { __r = "strcmp(m->keys[i],k)==0"; } else { __r = "m->keys[i]==k"; } __r; });
    v_o = "";
    if ((strcmp(v_kt, "str") == 0)) {
        v_o = scat(scat(scat(scat(scat(v_o, "static uint64_t "), v_nm), "_hash("), v_kc), " s){ uint64_t h=0xcbf29ce484222325ULL; if(!s) return h; while(*s){ h^=(uint64_t)(unsigned char)*s++; h*=0x100000001b3ULL; } return h; }\n");
    } else {
        v_o = scat(scat(scat(scat(scat(v_o, "static uint64_t "), v_nm), "_hash("), v_kc), " k){ uint64_t x=(uint64_t)(int64_t)k; x^=x>>30; x*=0xbf58476d1ce4e5b9ULL; x^=x>>27; x*=0x94d049bb133111ebULL; x^=x>>31; return x; }\n");
    }
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static "), v_nm), " "), v_nm), "_new(void){ "), v_nm), " m=("), v_nm), ")GC_MALLOC(sizeof("), v_nm), "_s)); m->cap=8; m->len=0; m->keys=("), v_kc), "*)GC_MALLOC(8*sizeof("), v_kc), ")); m->values=("), v_vc), "*)GC_MALLOC(8*sizeof("), v_vc), ")); m->occupied=(unsigned char*)GC_MALLOC(8); memset(m->occupied,0,8); return m; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static void "), v_nm), "_grow("), v_nm), " m){ int64_t oc=m->cap; "), v_kc), "* ok=m->keys; "), v_vc), "* ov=m->values; unsigned char* oo=m->occupied; int64_t nc=oc*2; m->cap=nc; m->keys=("), v_kc), "*)GC_MALLOC(nc*sizeof("), v_kc), ")); m->values=("), v_vc), "*)GC_MALLOC(nc*sizeof("), v_vc), ")); m->occupied=(unsigned char*)GC_MALLOC(nc); memset(m->occupied,0,nc); uint64_t mask=(uint64_t)(nc-1); for(int64_t i=0;i<oc;i++){ if(!oo[i]) continue; uint64_t h="), v_nm), "_hash(ok[i])&mask; for(int64_t p=0;p<nc;p++){ int64_t j=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[j]){ m->keys[j]=ok[i]; m->values[j]=ov[i]; m->occupied[j]=1; break; } } } }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static void "), v_nm), "_set("), v_nm), " m, "), v_kc), " k, "), v_vc), " val){ if(m->len*10>=m->cap*7) "), v_nm), "_grow(m); uint64_t mask=(uint64_t)(m->cap-1); uint64_t h="), v_nm), "_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]){ m->keys[i]=k; m->values[i]=val; m->occupied[i]=1; m->len++; return; } if("), v_keyeq), "){ m->values[i]=val; return; } } }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static "), v_vc), " "), v_nm), "_get("), v_nm), " m, "), v_kc), " k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h="), v_nm), "_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return "), v_dflt), "; if("), v_keyeq), ") return m->values[i]; } return "), v_dflt), "; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static int64_t "), v_nm), "_has("), v_nm), " m, "), v_kc), " k){ uint64_t mask=(uint64_t)(m->cap-1); uint64_t h="), v_nm), "_hash(k)&mask; for(int64_t p=0;p<m->cap;p++){ int64_t i=(int64_t)((h+(uint64_t)p)&mask); if(!m->occupied[i]) return 0; if("), v_keyeq), ") return 1; } return 0; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static arr_"), v_ksuf), " "), v_nm), "_keys("), v_nm), " m){ arr_"), v_ksuf), " r=arr_"), v_ksuf), "_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_"), v_ksuf), "_push(r,m->keys[i]); return r; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static arr_"), v_vsuf), " "), v_nm), "_values("), v_nm), " m){ arr_"), v_vsuf), " r=arr_"), v_vsuf), "_new(); for(int64_t i=0;i<m->cap;i++) if(m->occupied[i]) r=arr_"), v_vsuf), "_push(r,m->values[i]); return r; }\n");
    v_o = scat(scat(scat(scat(scat(v_o, "static int64_t "), v_nm), "_len("), v_nm), " m){ return m->len; }\n");
    v_o = scat(v_o, f_gen_map_print(v_nm, v_kt, v_vt));
    return v_o;
}

const char* f_map_dflt(const char* v_vt) {
    if ((strcmp(v_vt, "str") == 0)) {
        return "\"\"";
    }
    if ((strcmp(v_vt, "i64") == 0)) {
        return "0";
    }
    if ((strcmp(v_vt, "f64") == 0)) {
        return "0";
    }
    return scat(scat("(s_", v_vt), "){0}");
}

const char* f_map_nm(const char* v_kt, const char* v_vt) {
    return scat(scat(scat("map_", f_arr_suffix(v_kt)), "_"), f_arr_suffix(v_vt));
}

const char* f_gen_map_fwds_all(arr_str v_keys, arr_str v_vals) {
    const char* v_out;
    int64_t v_ki;
    int64_t v_vi;
    v_out = "";
    v_ki = 0;
    while ((v_ki < arr_str_len(v_keys))) {
        v_vi = 0;
        while ((v_vi < arr_str_len(v_vals))) {
            v_out = scat(v_out, f_gen_map_fwd(f_map_nm(arr_str_get(v_keys, v_ki), arr_str_get(v_vals, v_vi))));
            v_vi = (v_vi + 1);
        }
        v_ki = (v_ki + 1);
    }
    return v_out;
}

const char* f_gen_map_nodes_all(arr_str v_keys, arr_str v_vals) {
    const char* v_out;
    int64_t v_ki;
    int64_t v_vi;
    v_out = "";
    v_ki = 0;
    while ((v_ki < arr_str_len(v_keys))) {
        v_vi = 0;
        while ((v_vi < arr_str_len(v_vals))) {
            v_out = scat(v_out, f_gen_map_node(f_map_nm(arr_str_get(v_keys, v_ki), arr_str_get(v_vals, v_vi)), arr_str_get(v_keys, v_ki), arr_str_get(v_vals, v_vi)));
            v_vi = (v_vi + 1);
        }
        v_ki = (v_ki + 1);
    }
    return v_out;
}

const char* f_gen_map_helpers_all(arr_str v_keys, arr_str v_vals) {
    const char* v_out;
    int64_t v_ki;
    int64_t v_vi;
    v_out = "";
    v_ki = 0;
    while ((v_ki < arr_str_len(v_keys))) {
        v_vi = 0;
        while ((v_vi < arr_str_len(v_vals))) {
            v_out = scat(v_out, f_gen_map_helpers(f_map_nm(arr_str_get(v_keys, v_ki), arr_str_get(v_vals, v_vi)), arr_str_get(v_keys, v_ki), arr_str_get(v_vals, v_vi), f_map_dflt(arr_str_get(v_vals, v_vi))));
            v_vi = (v_vi + 1);
        }
        v_ki = (v_ki + 1);
    }
    return v_out;
}

const char* f_gen_res(const char* v_suf, const char* v_et) {
    const char* v_o;
    v_o = scat(scat(scat(scat("typedef struct { int tag; ", v_et), " ok; const char* err; } res_"), v_suf), ";\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(scat(scat(v_o, "static res_"), v_suf), " mk_ok_"), v_suf), "("), v_et), " v){ res_"), v_suf), " r; r.tag=0; r.ok=v; r.err=\"\"; return r; }\n");
    v_o = scat(scat(scat(scat(scat(scat(scat(v_o, "static res_"), v_suf), " mk_err_"), v_suf), "(const char* m){ res_"), v_suf), " r; r.tag=1; r.err=m; return r; }\n");
    return v_o;
}

const char* f_gen_struct_fwd(s_StructDef v_sd) {
    return scat(scat(scat(scat("typedef struct s_", (v_sd).name), " s_"), (v_sd).name), ";\n");
}

const char* f_gen_struct_body(s_StructDef v_sd) {
    const char* v_out;
    int64_t v_i;
    v_out = scat(scat("struct s_", (v_sd).name), " { ");
    v_i = 0;
    while ((v_i < arr_str_len((v_sd).fnames))) {
        v_out = scat(scat(scat(scat(v_out, f_cty(arr_str_get((v_sd).ftypes, v_i))), " "), arr_str_get((v_sd).fnames, v_i)), "; ");
        v_i = (v_i + 1);
    }
    return scat(v_out, "};\n");
}

const char* f_gen_struct_printer(s_StructDef v_sd) {
    const char* v_out;
    int64_t v_i;
    int64_t v_first;
    const char* v_ft;
    v_out = scat(scat(scat(scat(scat(scat("static void print_", (v_sd).name), "(s_"), (v_sd).name), " v){ printf(\""), (v_sd).name), "{\");");
    v_i = 0;
    v_first = (1 == 1);
    while ((v_i < arr_str_len((v_sd).fnames))) {
        if ((strcmp(arr_str_get((v_sd).fnames, v_i), "__vt") == 0)) {
            v_i = (v_i + 1);
            continue;
        }
        if ((v_first == (1 != 1))) {
            v_out = scat(v_out, " printf(\", \");");
        }
        v_first = (1 != 1);
        v_out = scat(scat(scat(v_out, " printf(\""), arr_str_get((v_sd).fnames, v_i)), ": \");");
        v_ft = arr_str_get((v_sd).ftypes, v_i);
        if ((strcmp(v_ft, "str") == 0)) {
            v_out = scat(scat(scat(v_out, " printf(\"%s\", v."), arr_str_get((v_sd).fnames, v_i)), ");");
        } else {
            if ((strcmp(v_ft, "bool") == 0)) {
                v_out = scat(scat(scat(v_out, " printf(\"%s\", (v."), arr_str_get((v_sd).fnames, v_i)), ") ? \"true\" : \"false\");");
            } else {
                if ((strcmp(v_ft, "i64") == 0)) {
                    v_out = scat(scat(scat(v_out, " printf(\"%lld\", (long long)(v."), arr_str_get((v_sd).fnames, v_i)), "));");
                } else {
                    v_out = scat(v_out, " printf(\"?\");");
                }
            }
        }
        v_i = (v_i + 1);
    }
    return scat(v_out, " printf(\"}\"); }\n");
}

const char* f_gen_struct_ctor(s_StructDef v_sd) {
    const char* v_out;
    int64_t v_i;
    int64_t v_firstp;
    v_out = scat(scat(scat(scat("static s_", (v_sd).name), " mk_"), (v_sd).name), "(");
    v_i = 0;
    v_firstp = (1 == 1);
    while ((v_i < arr_str_len((v_sd).fnames))) {
        if ((strcmp(arr_str_get((v_sd).fnames, v_i), "__vt") == 0)) {
            v_i = (v_i + 1);
            continue;
        }
        if ((v_firstp == (1 != 1))) {
            v_out = scat(v_out, ", ");
        }
        v_firstp = (1 != 1);
        v_out = scat(scat(scat(v_out, f_cty(arr_str_get((v_sd).ftypes, v_i))), " "), arr_str_get((v_sd).fnames, v_i));
        v_i = (v_i + 1);
    }
    v_out = scat(scat(scat(v_out, "){ s_"), (v_sd).name), " __s; ");
    v_i = 0;
    while ((v_i < arr_str_len((v_sd).fnames))) {
        if ((strcmp(arr_str_get((v_sd).fnames, v_i), "__vt") == 0)) {
            v_out = scat(scat(scat(v_out, "__s.__vt = (void*)&__vtbl_"), (v_sd).name), "; ");
        } else {
            v_out = scat(scat(scat(scat(scat(v_out, "__s."), arr_str_get((v_sd).fnames, v_i)), "="), arr_str_get((v_sd).fnames, v_i)), "; ");
        }
        v_i = (v_i + 1);
    }
    return scat(v_out, "return __s; }\n");
}

const char* f_gen_enum_fwd(s_EnumDef v_ed) {
    return scat(scat(scat(scat("typedef struct s_", (v_ed).name), " s_"), (v_ed).name), ";\n");
}

const char* f_gen_enum_body(s_EnumDef v_ed) {
    const char* v_out;
    int64_t v_vi;
    arr_str v_ftypes;
    int64_t v_fi;
    const char* v_cft;
    v_out = scat(scat("struct s_", (v_ed).name), " { int tag; union {\n");
    v_vi = 0;
    while ((v_vi < arr_str_len((v_ed).vnames))) {
        v_out = scat(v_out, "  struct {");
        v_ftypes = f_split_semi(arr_str_get((v_ed).vftypes, v_vi));
        v_fi = 0;
        while ((v_fi < arr_str_len(v_ftypes))) {
            v_cft = ({ const char* __r; if (f_is_boxed_ft(arr_str_get(v_ftypes, v_fi))) { __r = scat(scat("s_", f_boxed_enum(arr_str_get(v_ftypes, v_fi), (v_ed).name)), "*"); } else { __r = f_cty(arr_str_get(v_ftypes, v_fi)); } __r; });
            v_out = scat(scat(scat(scat(scat(v_out, " "), v_cft), " f"), i2s(v_fi)), ";");
            v_fi = (v_fi + 1);
        }
        v_out = scat(scat(scat(v_out, " } "), arr_str_get((v_ed).vnames, v_vi)), ";\n");
        v_vi = (v_vi + 1);
    }
    return scat(v_out, "} u; };\n");
}

const char* f_gen_enum_ctors(s_EnumDef v_ed) {
    const char* v_out;
    int64_t v_vi;
    const char* v_vn;
    arr_str v_ftypes;
    int64_t v_fi;
    const char* v_cft;
    const char* v_dst;
    const char* v_bt;
    v_out = "";
    v_vi = 0;
    while ((v_vi < arr_str_len((v_ed).vnames))) {
        v_vn = arr_str_get((v_ed).vnames, v_vi);
        v_ftypes = f_split_semi(arr_str_get((v_ed).vftypes, v_vi));
        v_out = scat(scat(scat(scat(scat(v_out, "static s_"), (v_ed).name), " mkv_"), v_vn), "(");
        v_fi = 0;
        while ((v_fi < arr_str_len(v_ftypes))) {
            if ((v_fi > 0)) {
                v_out = scat(v_out, ", ");
            }
            v_cft = ({ const char* __r; if (f_is_boxed_ft(arr_str_get(v_ftypes, v_fi))) { __r = scat("s_", f_boxed_enum(arr_str_get(v_ftypes, v_fi), (v_ed).name)); } else { __r = f_cty(arr_str_get(v_ftypes, v_fi)); } __r; });
            v_out = scat(scat(scat(v_out, v_cft), " f"), i2s(v_fi));
            v_fi = (v_fi + 1);
        }
        v_out = scat(scat(scat(scat(scat(v_out, "){ s_"), (v_ed).name), " v; v.tag="), i2s(v_vi)), "; ");
        v_fi = 0;
        while ((v_fi < arr_str_len(v_ftypes))) {
            v_dst = scat(scat(scat("v.u.", v_vn), ".f"), i2s(v_fi));
            if (f_is_boxed_ft(arr_str_get(v_ftypes, v_fi))) {
                v_bt = f_boxed_enum(arr_str_get(v_ftypes, v_fi), (v_ed).name);
                v_out = scat(scat(scat(scat(scat(scat(scat(scat(scat(scat(v_out, v_dst), "=(s_"), v_bt), "*)GC_MALLOC(sizeof(s_"), v_bt), ")); *"), v_dst), "=f"), i2s(v_fi)), "; ");
            } else {
                v_out = scat(scat(scat(scat(v_out, v_dst), "=f"), i2s(v_fi)), "; ");
            }
            v_fi = (v_fi + 1);
        }
        v_out = scat(v_out, "return v; }\n");
        v_vi = (v_vi + 1);
    }
    return v_out;
}

void f_seed_params(s_Syms* v_sy, s_Func v_f) {
    int64_t v_pj;
    v_pj = 0;
    while ((v_pj < arr_str_len((v_f).params))) {
        f_set_ty(v_sy, arr_str_get((v_f).params, v_pj), arr_str_get((v_f).ptypes, v_pj));
        v_pj = (v_pj + 1);
    }
}

s_Syms f_seed_fn(s_Syms* v_base, s_Func v_f) {
    s_Syms v_sy;
    v_sy = mk_Syms((v_base)->vty, (v_base)->fld, (v_base)->ctors, (v_base)->frets, (v_base)->evar, (v_base)->vft, (v_base)->gfns, (v_base)->lams);
    (v_sy).vty = map_str_str_new();
    f_seed_params((&v_sy), v_f);
    if (f_is_result_ann((v_f).ret)) {
        f_set_ty((&v_sy), "@ret", f_result_inner((v_f).ret));
    }
    return v_sy;
}

int64_t f_register_lam(s_Syms* v_base, s_Syms* v_sy, arr_str v_ps, arr_str v_pts, s_Expr v_b, int64_t v_id) {
    const char* v_idstr;
    int64_t v_sp;
    arr_str v_fvs;
    const char* v_cn;
    const char* v_cts;
    int64_t v_k;
    const char* v_nm;
    const char* v_rty;
    arr_str v_noTp;
    arr_Stmt v_lbody;
    v_idstr = i2s(v_id);
    v_sp = 0;
    while ((v_sp < arr_str_len(v_ps))) {
        if ((v_sp < arr_str_len(v_pts))) {
            f_set_ty(v_sy, arr_str_get(v_ps, v_sp), arr_str_get(v_pts, v_sp));
        }
        v_sp = (v_sp + 1);
    }
    v_fvs = f_free_vars(v_b);
    v_cn = "";
    v_cts = "";
    v_k = 0;
    while ((v_k < arr_str_len(v_fvs))) {
        v_nm = arr_str_get(v_fvs, v_k);
        if ((f_is_param(v_ps, v_nm) == (1 != 1))) {
            if (map_str_str_has((v_sy)->vty, v_nm)) {
                if ((((int64_t)strlen(v_cn)) == 0)) {
                    v_cn = v_nm;
                    v_cts = f_ty_of(v_sy, v_nm);
                } else {
                    v_cn = scat(scat(v_cn, ";"), v_nm);
                    v_cts = scat(scat(v_cts, ";"), f_ty_of(v_sy, v_nm));
                }
            }
        }
        v_k = (v_k + 1);
    }
    map_str_str_set((v_base)->evar, scat("@lamcaps.", v_idstr), v_cn);
    map_str_str_set((v_base)->evar, scat("@lamcapt.", v_idstr), v_cts);
    v_rty = f_type_of_expr(v_sy, v_b);
    v_noTp = ({ arr_str __a = arr_str_new(); __a; });
    v_lbody = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
    v_lbody = arr_Stmt_push(v_lbody, mkv_SReturn(v_b, (0 - 1)));
    (v_base)->lams = arr_Func_push((v_base)->lams, mk_Func(v_idstr, v_ps, v_pts, v_rty, "", v_noTp, v_lbody));
    f_collect_lams_expr(v_base, v_sy, v_b);
    return 0;
}

int64_t f_collect2(s_Syms* v_base, s_Syms* v_sy, s_Expr v_a, s_Expr v_b) {
    f_collect_lams_expr(v_base, v_sy, v_a);
    f_collect_lams_expr(v_base, v_sy, v_b);
    return 0;
}

int64_t f_collect3(s_Syms* v_base, s_Syms* v_sy, s_Expr v_a, s_Expr v_b, s_Expr v_c) {
    f_collect_lams_expr(v_base, v_sy, v_a);
    f_collect_lams_expr(v_base, v_sy, v_b);
    f_collect_lams_expr(v_base, v_sy, v_c);
    return 0;
}

int64_t f_collect_args(s_Syms* v_base, s_Syms* v_sy, arr_Expr v_args) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        f_collect_lams_expr(v_base, v_sy, arr_Expr_get(v_args, v_i));
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_collect_kv(s_Syms* v_base, s_Syms* v_sy, arr_Expr v_keys, arr_Expr v_vals) {
    f_collect_args(v_base, v_sy, v_keys);
    f_collect_args(v_base, v_sy, v_vals);
    return 0;
}

int64_t f_collect_match_e(s_Syms* v_base, s_Syms* v_sy, s_Expr v_sc, arr_Expr v_bodies) {
    f_collect_lams_expr(v_base, v_sy, v_sc);
    f_collect_args(v_base, v_sy, v_bodies);
    return 0;
}

int64_t f_collect_if_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb) {
    f_collect_lams_expr(v_base, v_sy, v_c);
    f_collect_lams(v_base, v_sy, v_b);
    f_collect_lams(v_base, v_sy, v_eb);
    return 0;
}

int64_t f_collect_body_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b) {
    f_collect_lams_expr(v_base, v_sy, v_c);
    f_collect_lams(v_base, v_sy, v_b);
    return 0;
}

int64_t f_collect_coll_s(s_Syms* v_base, s_Syms* v_sy, s_Expr v_coll, arr_Stmt v_b) {
    f_collect_lams_expr(v_base, v_sy, v_coll);
    f_collect_lams(v_base, v_sy, v_b);
    return 0;
}

int64_t f_collect_call(s_Syms* v_base, s_Syms* v_sy, const char* v_fname, arr_Expr v_args) {
    if ((((strcmp(v_fname, "map") == 0) || (strcmp(v_fname, "filter") == 0)) && (arr_Expr_len(v_args) == 2))) {
        f_collect_lams_expr(v_base, v_sy, arr_Expr_get(v_args, 0));
        return 0;
    }
    if (((strcmp(v_fname, "reduce") == 0) && (arr_Expr_len(v_args) == 3))) {
        f_collect_lams_expr(v_base, v_sy, arr_Expr_get(v_args, 0));
        f_collect_lams_expr(v_base, v_sy, arr_Expr_get(v_args, 1));
        return 0;
    }
    f_collect_args(v_base, v_sy, v_args);
    return 0;
}

int64_t f_collect_lams_expr(s_Syms* v_base, s_Syms* v_sy, s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = f_register_lam(v_base, v_sy, v_ps, v_pts, v_b, v_id); } else if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_collect_call(v_base, v_sy, v_fname, v_args); } else if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_collect2(v_base, v_sy, v_l, v_r); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = f_collect_lams_expr(v_base, v_sy, v_x); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_collect_lams_expr(v_base, v_sy, v_obj); } else if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_collect2(v_base, v_sy, v_obj, v_idx); } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = f_collect_args(v_base, v_sy, v_elems); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = f_collect_lams_expr(v_base, v_sy, v_x); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_collect_match_e(v_base, v_sy, v_sc, v_bd); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = f_collect3(v_base, v_sy, v_c, v_t, v_el2); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = f_collect_lams_expr(v_base, v_sy, v_x); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = 0; } else if(__s.tag==1){ const char* v_s = __s.u.Flt.f0; __m = 0; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = 0; } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = 0; } else if(__s.tag==10){ const char* v_m = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_collect_kv(v_base, v_sy, v_mks, v_mvs); } else if(__s.tag==16){ arr_Expr v_elems = __s.u.Tuple.f0; __m = f_collect_args(v_base, v_sy, v_elems); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = f_collect_lams(v_base, v_sy, v_bb); } else if(__s.tag==18){ __m = 0; } __m; });
}

int64_t f_collect_lams_stmt(s_Syms* v_base, s_Syms* v_sy, s_Stmt v_s) {
    return ({ int64_t __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_collect_lams_expr(v_base, v_sy, v_e); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_collect_lams_expr(v_base, v_sy, v_e); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = f_collect_lams_expr(v_base, v_sy, v_e); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = f_collect_lams_expr(v_base, v_sy, v_e); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = f_collect_lams_expr(v_base, v_sy, v_e); } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = f_collect_lams_expr(v_base, v_sy, v_e); } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = f_collect3(v_base, v_sy, v_o, v_i, v_e); } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = f_collect2(v_base, v_sy, v_o, v_e); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_collect_if_s(v_base, v_sy, v_c, v_b, v_eb); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_collect_body_s(v_base, v_sy, v_c, v_b); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_collect_coll_s(v_base, v_sy, v_coll, v_b); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_collect_coll_s(v_base, v_sy, v_coll, v_b); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_collect_range_s(v_base, v_sy, v_lo, v_hi, v_b); } else if(__s.tag==12){ __m = 0; } else if(__s.tag==13){ __m = 0; } __m; });
}

int64_t f_collect_lams(s_Syms* v_base, s_Syms* v_sy, arr_Stmt v_body) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        f_collect_lams_stmt(v_base, v_sy, arr_Stmt_get(v_body, v_i));
        v_i = (v_i + 1);
    }
    return 0;
}

const char* f_infer_ret(s_Func v_f, s_Syms v_base) {
    s_Syms v_sy;
    if ((((int64_t)strlen((v_f).ret)) > 0)) {
        return (v_f).ret;
    }
    v_sy = v_base;
    f_seed_params((&v_sy), v_f);
    return f_first_return_type((&v_sy), (v_f).body);
}

const char* f_first_return_type(s_Syms* v_sy, arr_Stmt v_body) {
    int64_t v_i;
    const char* v_r;
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_r = f_stmt_return_type(v_sy, arr_Stmt_get(v_body, v_i));
        if ((strcmp(v_r, "") != 0)) {
            return v_r;
        }
        v_i = (v_i + 1);
    }
    if ((arr_Stmt_len(v_body) > 0)) {
        return f_last_expr_type(v_sy, arr_Stmt_get(v_body, (arr_Stmt_len(v_body) - 1)));
    }
    return "";
}

int64_t f_expr_is_cmp(s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = ((v_op >= f_OP_LT()) && (v_op <= f_OP_OR())); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = (v_op == f_OP_NOT()); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = (1 != 1); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = (1 != 1); } else if(__s.tag==2){ const char* v_ss = __s.u.Str.f0; __m = (1 != 1); } else if(__s.tag==3){ const char* v_nm = __s.u.Var.f0; __m = (1 != 1); } else if(__s.tag==6){ const char* v_fnm = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = (1 != 1); } else if(__s.tag==7){ s_Expr v_ob = *(__s.u.Field.f0); const char* v_fnm2 = __s.u.Field.f1; __m = (1 != 1); } else if(__s.tag==8){ s_Expr v_ob = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = (1 != 1); } else if(__s.tag==9){ arr_Expr v_els = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = (1 != 1); } else if(__s.tag==10){ const char* v_mty = __s.u.MapLit.f0; arr_Expr v_ks = __s.u.MapLit.f1; arr_Expr v_vs = __s.u.MapLit.f2; __m = (1 != 1); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = (1 != 1); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = (1 != 1); } else if(__s.tag==13){ s_Expr v_cnd = *(__s.u.IfE.f0); s_Expr v_thn = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = (1 != 1); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = (1 != 1); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_bd = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = (1 != 1); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = (1 != 1); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = (1 != 1); } else if(__s.tag==18){ __m = (1 != 1); } __m; });
}

int64_t f_expr_is_call(s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==6){ const char* v_fnm = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = (1 == 1); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = (1 != 1); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = (1 != 1); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = (1 != 1); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = (1 != 1); } else if(__s.tag==2){ const char* v_ss = __s.u.Str.f0; __m = (1 != 1); } else if(__s.tag==3){ const char* v_nm = __s.u.Var.f0; __m = (1 != 1); } else if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = (1 != 1); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = (1 != 1); } else if(__s.tag==7){ s_Expr v_ob = *(__s.u.Field.f0); const char* v_fnm2 = __s.u.Field.f1; __m = (1 != 1); } else if(__s.tag==8){ s_Expr v_ob = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = (1 != 1); } else if(__s.tag==9){ arr_Expr v_els = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = (1 != 1); } else if(__s.tag==10){ const char* v_mty = __s.u.MapLit.f0; arr_Expr v_ks = __s.u.MapLit.f1; arr_Expr v_vs = __s.u.MapLit.f2; __m = (1 != 1); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = (1 != 1); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = (1 != 1); } else if(__s.tag==13){ s_Expr v_cnd = *(__s.u.IfE.f0); s_Expr v_thn = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = (1 != 1); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = (1 != 1); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_bd = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = (1 != 1); } else if(__s.tag==18){ __m = (1 != 1); } __m; });
}

const char* f_call_fname(s_Expr v_e) {
    return ({ const char* __m; s_Expr __s = v_e; if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = v_fname; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = ""; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = ""; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = ""; } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = ""; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = ""; } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = ""; } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = ""; } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = ""; } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = ""; } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = ""; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = ""; } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = ""; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = ""; } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = ""; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = ""; } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = ""; } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = ""; } else if(__s.tag==18){ __m = ""; } __m; });
}

const char* f_tail_sexpr_type(s_Syms* v_sy, s_Expr v_e) {
    const char* v_fnm;
    if (f_expr_is_call(v_e)) {
        v_fnm = f_call_fname(v_e);
        if (map_str_str_has((v_sy)->frets, v_fnm)) {
            return map_str_str_get((v_sy)->frets, v_fnm);
        }
        return "";
    }
    return f_type_of_expr(v_sy, v_e);
}

const char* f_last_expr_type(s_Syms* v_sy, s_Stmt v_s) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = f_tail_sexpr_type(v_sy, v_e); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = f_type_of_expr(v_sy, v_e); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = ""; } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = ""; } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = ""; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = ""; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = ""; } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = ""; } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = ""; } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = ""; } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = ""; } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = ""; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = ""; } else if(__s.tag==12){ __m = ""; } else if(__s.tag==13){ __m = ""; } __m; });
}

const char* f_stmt_return_type(s_Syms* v_sy, s_Stmt v_s) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = f_type_of_expr(v_sy, v_e); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = ""; } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_decl_side(v_sy, v_name, v_e); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_either_return(v_sy, v_b, v_eb); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_first_return_type(v_sy, v_b); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_first_return_type(v_sy, v_b); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_first_return_type(v_sy, v_b); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_first_return_type(v_sy, v_b); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = ""; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = ""; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = ""; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = ""; } else if(__s.tag==12){ __m = ""; } else if(__s.tag==13){ __m = ""; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = ""; } __m; });
}

const char* f_decl_side(s_Syms* v_sy, const char* v_name, s_Expr v_e) {
    f_set_ty(v_sy, v_name, f_type_of_expr(v_sy, v_e));
    return "";
}

const char* f_either_return(s_Syms* v_sy, arr_Stmt v_b, arr_Stmt v_eb) {
    const char* v_r;
    v_r = f_first_return_type(v_sy, v_b);
    if ((strcmp(v_r, "") != 0)) {
        return v_r;
    }
    return f_first_return_type(v_sy, v_eb);
}

int64_t f_has_sub(const char* v_s, const char* v_sub) {
    return (str_index_of(v_s, v_sub) >= 0);
}

const char* f_var_name(s_Expr v_e) {
    return ({ const char* __m; s_Expr __s = v_e; if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = v_n; } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = ""; } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = ""; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = ""; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = ""; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = ""; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = ""; } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = ""; } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = ""; } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = ""; } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = ""; } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = ""; } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = ""; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = ""; } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = ""; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = ""; } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = ""; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = ""; } else if(__s.tag==18){ __m = ""; } __m; });
}

int64_t f_is_empty_maplit(s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = (arr_Expr_len(v_mks) == 0); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = (1 != 1); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = (1 != 1); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = (1 != 1); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = (1 != 1); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = (1 != 1); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = (1 != 1); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = (1 != 1); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = (1 != 1); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = (1 != 1); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = (1 != 1); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = (1 != 1); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = (1 != 1); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = (1 != 1); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = (1 != 1); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = (1 != 1); } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = (1 != 1); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = (1 != 1); } else if(__s.tag==18){ __m = (1 != 1); } __m; });
}

const char* f_map_assign_type(s_Syms* v_sy, arr_Stmt v_body, const char* v_m) {
    int64_t v_i;
    const char* v_t;
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_t = f_map_assign_type_s(v_sy, arr_Stmt_get(v_body, v_i), v_m);
        if ((((int64_t)strlen(v_t)) > 0)) {
            return v_t;
        }
        v_i = (v_i + 1);
    }
    return "";
}

const char* f_scan_loopin(s_Syms* v_sy, const char* v_vnm, s_Expr v_coll, arr_Stmt v_b, const char* v_m) {
    f_set_ty(v_sy, v_vnm, f_coll_elem_type(v_sy, v_coll));
    return f_map_assign_type(v_sy, v_b, v_m);
}

const char* f_scan_loopkv(s_Syms* v_sy, const char* v_kn, const char* v_vn, s_Expr v_coll, arr_Stmt v_b, const char* v_m) {
    const char* v_t;
    v_t = f_type_of_expr(v_sy, v_coll);
    f_set_ty(v_sy, v_kn, f_map_ktype(v_t));
    f_set_ty(v_sy, v_vn, f_map_vtype(v_t));
    return f_map_assign_type(v_sy, v_b, v_m);
}

const char* f_scan_range(s_Syms* v_sy, const char* v_v, arr_Stmt v_b, const char* v_m) {
    f_set_ty(v_sy, v_v, "i64");
    return f_map_assign_type(v_sy, v_b, v_m);
}

const char* f_scan_either(s_Syms* v_sy, arr_Stmt v_b, arr_Stmt v_eb, const char* v_m) {
    const char* v_t;
    v_t = f_map_assign_type(v_sy, v_b, v_m);
    if ((((int64_t)strlen(v_t)) > 0)) {
        return v_t;
    }
    return f_map_assign_type(v_sy, v_eb, v_m);
}

const char* f_idx_assign_type(s_Syms* v_sy, s_Expr v_o, s_Expr v_idx, s_Expr v_e, const char* v_m) {
    if ((strcmp(f_var_name(v_o), v_m) == 0)) {
        return scat(scat(scat(scat("{", f_type_of_expr(v_sy, v_idx)), ":"), f_type_of_expr(v_sy, v_e)), "}");
    }
    return "";
}

const char* f_map_assign_type_s(s_Syms* v_sy, s_Stmt v_s, const char* v_m) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_idx = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = f_idx_assign_type(v_sy, v_o, v_idx, v_e, v_m); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = ""; } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_scan_either(v_sy, v_b, v_eb, v_m); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_map_assign_type(v_sy, v_b, v_m); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_scan_loopin(v_sy, v_vnm, v_coll, v_b, v_m); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_scan_loopkv(v_sy, v_kn, v_vn, v_coll, v_b, v_m); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_scan_range(v_sy, v_v, v_b, v_m); } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = ""; } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = ""; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = ""; } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = ""; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = ""; } else if(__s.tag==12){ __m = ""; } else if(__s.tag==13){ __m = ""; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = ""; } __m; });
}

int64_t f_is_empty_array(s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = ((arr_Expr_len(v_es) == 0) && (strcmp(v_et, "") == 0)); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = (1 != 1); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = (1 != 1); } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = (1 != 1); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = (1 != 1); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = (1 != 1); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = (1 != 1); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = (1 != 1); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = (1 != 1); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = (1 != 1); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = (1 != 1); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = (1 != 1); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = (1 != 1); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = (1 != 1); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = (1 != 1); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = (1 != 1); } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = (1 != 1); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = (1 != 1); } else if(__s.tag==18){ __m = (1 != 1); } __m; });
}

const char* f_push_args_elem(s_Syms* v_sy, const char* v_fname, arr_Expr v_args) {
    if (((strcmp(v_fname, "push") == 0) && (arr_Expr_len(v_args) == 2))) {
        return f_type_of_expr(v_sy, arr_Expr_get(v_args, 1));
    }
    return "";
}

const char* f_push_call_elem(s_Syms* v_sy, s_Expr v_e) {
    return ({ const char* __m; s_Expr __s = v_e; if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_push_args_elem(v_sy, v_fname, v_args); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = ""; } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = ""; } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = ""; } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = ""; } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = ""; } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = ""; } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = ""; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = ""; } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b = *(__s.u.Bin.f2); __m = ""; } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = ""; } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = ""; } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = ""; } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = ""; } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = ""; } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = ""; } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = ""; } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = ""; } else if(__s.tag==18){ __m = ""; } __m; });
}

const char* f_same_name_push(s_Syms* v_sy, const char* v_name, s_Expr v_e, const char* v_a) {
    if ((strcmp(v_name, v_a) != 0)) {
        return "";
    }
    return f_push_call_elem(v_sy, v_e);
}

const char* f_push_assign_elem(s_Syms* v_sy, s_Stmt v_s, const char* v_a) {
    return ({ const char* __m; s_Stmt __s = v_s; if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = f_same_name_push(v_sy, v_name, v_e, v_a); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = ""; } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = ""; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_ix = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = ""; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = ""; } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = ""; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = ""; } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = ""; } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = ""; } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = ""; } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = ""; } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = ""; } else if(__s.tag==12){ __m = ""; } else if(__s.tag==13){ __m = ""; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = ""; } __m; });
}

const char* f_array_elem_from_push(s_Syms* v_sy, arr_Stmt v_body, const char* v_a) {
    int64_t v_i;
    const char* v_t;
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_t = f_push_assign_elem(v_sy, arr_Stmt_get(v_body, v_i), v_a);
        if ((((int64_t)strlen(v_t)) > 0)) {
            return v_t;
        }
        v_i = (v_i + 1);
    }
    return "";
}

s_Stmt f_stamp_decl_e(s_Syms* v_sy, arr_Stmt v_body, const char* v_name, s_Expr v_e, int64_t v_pos) {
    const char* v_t;
    const char* v_et;
    if (f_is_empty_maplit(v_e)) {
        v_t = f_map_assign_type(v_sy, v_body, v_name);
        if ((((int64_t)strlen(v_t)) > 0)) {
            return mkv_SDecl(v_name, mkv_MapLit(v_t, f_no_exprs(), f_no_exprs()), v_pos);
        }
    }
    if (f_is_empty_array(v_e)) {
        v_et = f_array_elem_from_push(v_sy, v_body, v_name);
        if ((((int64_t)strlen(v_et)) > 0)) {
            return mkv_SDecl(v_name, mkv_Array(f_no_exprs(), v_et), v_pos);
        }
    }
    return mkv_SDecl(v_name, v_e, v_pos);
}

s_Stmt f_stamp_decl(s_Syms* v_sy, arr_Stmt v_body, s_Stmt v_s) {
    return ({ s_Stmt __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); int64_t v_pos = __s.u.SDecl.f2; __m = f_stamp_decl_e(v_sy, v_body, v_name, v_e, v_pos); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = v_s; } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = v_s; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_ix = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = v_s; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = v_s; } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = v_s; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = v_s; } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = v_s; } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = v_s; } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = v_s; } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = v_s; } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = v_s; } else if(__s.tag==12){ __m = v_s; } else if(__s.tag==13){ __m = v_s; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = v_s; } __m; });
}

int64_t f_seed_one(s_Syms* v_sy, const char* v_name, s_Expr v_e) {
    f_set_ty(v_sy, v_name, f_type_of_expr(v_sy, v_e));
    return 0;
}

int64_t f_seed_decl_s(s_Syms* v_sy, s_Stmt v_s) {
    return ({ int64_t __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); __m = f_seed_one(v_sy, v_name, v_e); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = 0; } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); __m = 0; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_ix = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); __m = 0; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); __m = 0; } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); __m = 0; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); __m = 0; } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = 0; } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = 0; } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = 0; } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = 0; } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = 0; } else if(__s.tag==12){ __m = 0; } else if(__s.tag==13){ __m = 0; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); __m = 0; } __m; });
}

arr_Stmt f_stamp_empty_maps(s_Syms* v_sy, arr_Stmt v_body) {
    int64_t v_si;
    arr_Stmt v_out;
    int64_t v_i;
    v_si = 0;
    while ((v_si < arr_Stmt_len(v_body))) {
        f_seed_decl_s(v_sy, arr_Stmt_get(v_body, v_si));
        v_si = (v_si + 1);
    }
    v_out = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        v_out = arr_Stmt_push(v_out, f_stamp_decl(v_sy, v_body, arr_Stmt_get(v_body, v_i)));
        v_i = (v_i + 1);
    }
    return v_out;
}

int64_t f_slot_has(const char* v_slots, const char* v_m) {
    arr_str v_parts;
    int64_t v_i;
    v_parts = split(v_slots, ";");
    v_i = 0;
    while ((v_i < arr_str_len(v_parts))) {
        if ((strcmp(arr_str_get(v_parts, v_i), v_m) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

int64_t f_find_func_i(arr_Func v_funcs, const char* v_name) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_Func_len(v_funcs))) {
        if ((strcmp((arr_Func_get(v_funcs, v_i)).name, v_name) == 0)) {
            return v_i;
        }
        v_i = (v_i + 1);
    }
    return (0 - 1);
}

int64_t f_bad_in_arith(const char* v_t) {
    const char* v_c;
    v_c = f_ty_cat(v_t);
    return (((((strcmp(v_c, "str") == 0) || (strcmp(v_c, "arr") == 0)) || (strcmp(v_c, "map") == 0)) || (strcmp(v_c, "agg") == 0)) || (strcmp(v_c, "bytes") == 0));
}

int64_t f_arith_or_bit_nonadd(int64_t v_op) {
    return (((((((((v_op == f_OP_SUB()) || (v_op == f_OP_MUL())) || (v_op == f_OP_DIV())) || (v_op == f_OP_MOD())) || (v_op == f_OP_SHL())) || (v_op == f_OP_SHR())) || (v_op == f_OP_BAND())) || (v_op == f_OP_BXOR())) || (v_op == f_OP_BOR()));
}

int64_t f_is_cmp_op(int64_t v_op) {
    return ((((((v_op == f_OP_EQ()) || (v_op == f_OP_NE())) || (v_op == f_OP_LT())) || (v_op == f_OP_GT())) || (v_op == f_OP_LE())) || (v_op == f_OP_GE()));
}

int64_t f_binop_check(s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r, int64_t v_pos, const char* v_src) {
    const char* v_lt;
    const char* v_rr;
    int64_t v_bothstr;
    if ((v_pos < 0)) {
        return 0;
    }
    v_lt = f_type_confident(v_sy, v_l);
    v_rr = f_type_confident(v_sy, v_r);
    if (((f_confident(v_lt) == (1 != 1)) || (f_confident(v_rr) == (1 != 1)))) {
        return 0;
    }
    if (f_is_cmp_op(v_op)) {
        if (f_incompatible(v_sy, v_lt, v_rr)) {
            f_report_at(v_src, v_pos, scat(scat(scat("type mismatch: comparing ", v_lt), " and "), v_rr));
        }
        return 0;
    }
    if ((v_op == f_OP_ADD())) {
        v_bothstr = ((strcmp(v_lt, "str") == 0) && (strcmp(v_rr, "str") == 0));
        if (((v_bothstr == (1 != 1)) && (f_bad_in_arith(v_lt) || f_bad_in_arith(v_rr)))) {
            f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("type mismatch: operator", f_op_c(v_op)), "on "), v_lt), " and "), v_rr));
        }
        return 0;
    }
    if (f_arith_or_bit_nonadd(v_op)) {
        if ((f_bad_in_arith(v_lt) || f_bad_in_arith(v_rr))) {
            f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("type mismatch: operator", f_op_c(v_op)), "on "), v_lt), " and "), v_rr));
        }
    }
    return 0;
}

int64_t f_callarg_check(arr_Func v_funcs, s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    int64_t v_fi;
    int64_t v_np;
    arr_str v_pts;
    int64_t v_i;
    const char* v_at;
    if ((v_pos < 0)) {
        return 0;
    }
    v_fi = f_find_func_i(v_funcs, v_fname);
    if ((v_fi < 0)) {
        return 0;
    }
    if ((arr_str_len((arr_Func_get(v_funcs, v_fi)).tparams) > 0)) {
        return 0;
    }
    v_np = arr_str_len((arr_Func_get(v_funcs, v_fi)).params);
    if ((arr_Expr_len(v_args) != v_np)) {
        f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("function '", v_fname), "' expects "), i2s(v_np)), " arguments, got "), i2s(arr_Expr_len(v_args))));
        return 0;
    }
    v_pts = (arr_Func_get(v_funcs, v_fi)).ptypes;
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        if ((v_i < arr_str_len(v_pts))) {
            v_at = f_type_confident(v_sy, arr_Expr_get(v_args, v_i));
            if ((f_confident(v_at) && f_incompatible(v_sy, arr_str_get(v_pts, v_i), v_at))) {
                f_report_at(v_src, v_pos, scat(scat(scat(scat(scat(scat(scat("type mismatch: argument ", i2s((v_i + 1))), " to '"), v_fname), "' expects "), arr_str_get(v_pts, v_i)), ", got "), v_at));
            }
        }
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_field_check(s_Syms* v_sy, s_Expr v_obj, const char* v_fname, int64_t v_pos, const char* v_src) {
    const char* v_ot;
    const char* v_sty;
    if ((v_pos < 0)) {
        return 0;
    }
    v_ot = f_type_confident(v_sy, v_obj);
    if ((strcmp(v_ot, "?") == 0)) {
        return 0;
    }
    v_sty = f_under_ptr(v_ot);
    if (f_is_ctor(v_sy, v_sty)) {
        if ((map_str_str_has((v_sy)->fld, f_fkey(v_sty, v_fname)) == (1 != 1))) {
            f_report_at(v_src, v_pos, scat(scat(scat("unknown field '.", v_fname), "' on "), v_sty));
        }
        return 0;
    }
    f_report_at(v_src, v_pos, scat(scat(scat("type error: field '.", v_fname), "' on non-struct value of type "), v_ot));
    return 0;
}

int64_t f_index_check(s_Syms* v_sy, s_Expr v_obj, int64_t v_pos, const char* v_src) {
    const char* v_t;
    if ((v_pos < 0)) {
        return 0;
    }
    v_t = f_type_confident(v_sy, v_obj);
    if ((strcmp(v_t, "?") == 0)) {
        return 0;
    }
    if (f_is_array_ann(v_t)) {
        return 0;
    }
    if (f_is_map_ann(v_t)) {
        return 0;
    }
    if ((strcmp(v_t, "str") == 0)) {
        return 0;
    }
    if ((strcmp(v_t, "bytes") == 0)) {
        return 0;
    }
    f_report_at(v_src, v_pos, scat("type error: cannot index value of type ", v_t));
    return 0;
}

int64_t f_chk2(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_a, s_Expr v_b, int64_t v_pos, const char* v_src) {
    f_check_expr(v_funcs, v_sy, v_a, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_b, v_pos, v_src);
    return 0;
}

int64_t f_chk3(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_a, s_Expr v_b, s_Expr v_c, int64_t v_pos, const char* v_src) {
    f_check_expr(v_funcs, v_sy, v_a, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_b, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_c, v_pos, v_src);
    return 0;
}

int64_t f_chk_args(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        f_check_expr(v_funcs, v_sy, arr_Expr_get(v_args, v_i), v_pos, v_src);
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_chk_kv(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_ks, arr_Expr v_vs, int64_t v_pos, const char* v_src) {
    f_chk_args(v_funcs, v_sy, v_ks, v_pos, v_src);
    f_chk_args(v_funcs, v_sy, v_vs, v_pos, v_src);
    return 0;
}

int64_t f_chk_try(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_x, int64_t v_pos, const char* v_src) {
    if (((v_pos >= 0) && (map_str_str_has((v_sy)->vty, "@ret") == (1 != 1)))) {
        f_report_at(v_src, v_pos, "'?' used outside a function returning !T");
    }
    f_check_expr(v_funcs, v_sy, v_x, v_pos, v_src);
    return 0;
}

int64_t f_homog_check(s_Syms* v_sy, arr_Expr v_elems, const char* v_msg, int64_t v_pos, const char* v_src) {
    const char* v_base;
    int64_t v_i;
    const char* v_t;
    if ((v_pos < 0)) {
        return 0;
    }
    v_base = "?";
    v_i = 0;
    while ((v_i < arr_Expr_len(v_elems))) {
        v_t = f_type_confident(v_sy, arr_Expr_get(v_elems, v_i));
        if (f_confident(v_t)) {
            if ((strcmp(v_base, "?") == 0)) {
                v_base = v_t;
            } else {
                if (f_cat_incompatible(v_base, v_t)) {
                    f_report_at(v_src, v_pos, scat(scat(scat(scat(v_msg, " "), v_base), " and "), v_t));
                }
            }
        }
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_chk_array(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_elems, int64_t v_pos, const char* v_src) {
    f_homog_check(v_sy, v_elems, "array literal mixes", v_pos, v_src);
    f_chk_args(v_funcs, v_sy, v_elems, v_pos, v_src);
    return 0;
}

int64_t f_chk_maplit(arr_Func v_funcs, s_Syms* v_sy, arr_Expr v_ks, arr_Expr v_vs, int64_t v_pos, const char* v_src) {
    f_homog_check(v_sy, v_ks, "map literal mixes key types", v_pos, v_src);
    f_homog_check(v_sy, v_vs, "map literal mixes value types", v_pos, v_src);
    f_chk_args(v_funcs, v_sy, v_ks, v_pos, v_src);
    f_chk_args(v_funcs, v_sy, v_vs, v_pos, v_src);
    return 0;
}

int64_t f_vn_has(arr_str v_vnames, const char* v_v) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_str_len(v_vnames))) {
        if ((strcmp(arr_str_get(v_vnames, v_i), v_v) == 0)) {
            return (1 == 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

int64_t f_match_check(s_Syms* v_sy, s_Expr v_scrut, arr_str v_vnames, arr_str v_vbinds, int64_t v_pos, const char* v_src) {
    const char* v_ty;
    int64_t v_i;
    const char* v_nm;
    int64_t v_nb;
    int64_t v_nf;
    arr_str v_all;
    int64_t v_j;
    if ((v_pos < 0)) {
        return 0;
    }
    v_ty = f_type_confident(v_sy, v_scrut);
    if ((f_confident(v_ty) == (1 != 1))) {
        return 0;
    }
    if ((map_str_str_has((v_sy)->evar, scat("@order.", v_ty)) == (1 != 1))) {
        f_report_at(v_src, v_pos, scat("type error: match on non-enum value of type ", v_ty));
        return 0;
    }
    v_i = 0;
    while ((v_i < arr_str_len(v_vnames))) {
        v_nm = arr_str_get(v_vnames, v_i);
        if (((f_is_variant(v_sy, v_nm) == (1 != 1)) || (strcmp(map_str_str_get((v_sy)->evar, v_nm), v_ty) != 0))) {
            f_report_at(v_src, v_pos, scat(scat(scat("unknown variant '", v_nm), "' in match on "), v_ty));
        }
        v_nb = arr_str_len(f_split_semi(arr_str_get(v_vbinds, v_i)));
        if (map_str_str_has((v_sy)->evar, scat("@vfldn.", v_nm))) {
            v_nf = s2i(map_str_str_get((v_sy)->evar, scat("@vfldn.", v_nm)));
            if ((v_nb > v_nf)) {
                f_report_at(v_src, v_pos, scat(scat(scat(scat(scat(scat("variant '", v_nm), "' binds "), i2s(v_nb)), " but has "), i2s(v_nf)), " field(s)"));
            }
        }
        v_i = (v_i + 1);
    }
    v_all = f_split_semi(map_str_str_get((v_sy)->evar, scat("@order.", v_ty)));
    v_j = 0;
    while ((v_j < arr_str_len(v_all))) {
        if ((f_vn_has(v_vnames, arr_str_get(v_all, v_j)) == (1 != 1))) {
            f_report_at(v_src, v_pos, scat(scat(scat(scat("non-exhaustive match: enum ", v_ty), " missing variant '"), arr_str_get(v_all, v_j)), "'"));
        }
        v_j = (v_j + 1);
    }
    return 0;
}

int64_t f_chk_match(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_sc, arr_str v_vnames, arr_str v_vbinds, arr_Expr v_bd, int64_t v_pos, const char* v_src) {
    f_match_check(v_sy, v_sc, v_vnames, v_vbinds, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_sc, v_pos, v_src);
    f_chk_args(v_funcs, v_sy, v_bd, v_pos, v_src);
    return 0;
}

int64_t f_chk_bin(arr_Func v_funcs, s_Syms* v_sy, int64_t v_op, s_Expr v_l, s_Expr v_r, int64_t v_pos, const char* v_src) {
    f_binop_check(v_sy, v_op, v_l, v_r, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_l, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_r, v_pos, v_src);
    return 0;
}

int64_t f_lambda_arity(s_Expr v_e) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = arr_str_len(v_ps); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = (0 - 1); } else if(__s.tag==1){ const char* v_fs = __s.u.Flt.f0; __m = (0 - 1); } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = (0 - 1); } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = (0 - 1); } else if(__s.tag==4){ int64_t v_o = __s.u.Bin.f0; s_Expr v_a = *(__s.u.Bin.f1); s_Expr v_b2 = *(__s.u.Bin.f2); __m = (0 - 1); } else if(__s.tag==5){ int64_t v_o = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = (0 - 1); } else if(__s.tag==6){ const char* v_f = __s.u.Call.f0; arr_Expr v_a = __s.u.Call.f1; __m = (0 - 1); } else if(__s.tag==7){ s_Expr v_o = *(__s.u.Field.f0); const char* v_f = __s.u.Field.f1; __m = (0 - 1); } else if(__s.tag==8){ s_Expr v_o = *(__s.u.Index.f0); s_Expr v_ix = *(__s.u.Index.f1); __m = (0 - 1); } else if(__s.tag==9){ arr_Expr v_es = __s.u.Array.f0; const char* v_et = __s.u.Array.f1; __m = (0 - 1); } else if(__s.tag==10){ const char* v_ml = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = (0 - 1); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = (0 - 1); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = (0 - 1); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = (0 - 1); } else if(__s.tag==14){ s_Expr v_e2 = *(__s.u.Try.f0); __m = (0 - 1); } else if(__s.tag==16){ arr_Expr v_tes = __s.u.Tuple.f0; __m = (0 - 1); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = (0 - 1); } else if(__s.tag==18){ __m = (0 - 1); } __m; });
}

int64_t f_hof_arity_check(const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    int64_t v_want;
    int64_t v_i;
    int64_t v_np;
    if ((v_pos < 0)) {
        return 0;
    }
    v_want = (0 - 1);
    if (((strcmp(v_fname, "map") == 0) || (strcmp(v_fname, "filter") == 0))) {
        v_want = 1;
    }
    if ((strcmp(v_fname, "reduce") == 0)) {
        v_want = 2;
    }
    if ((v_want < 0)) {
        return 0;
    }
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        v_np = f_lambda_arity(arr_Expr_get(v_args, v_i));
        if (((v_np >= 0) && (v_np != v_want))) {
            f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("'", v_fname), "' callback takes "), i2s(v_want)), " parameter(s), got "), i2s(v_np)));
        }
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_generic_arity_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    int64_t v_gi;
    int64_t v_np;
    if ((v_pos < 0)) {
        return 0;
    }
    v_gi = 0;
    while ((v_gi < arr_Func_len((v_sy)->gfns))) {
        if ((strcmp((arr_Func_get((v_sy)->gfns, v_gi)).name, v_fname) == 0)) {
            v_np = arr_str_len((arr_Func_get((v_sy)->gfns, v_gi)).params);
            if ((arr_Expr_len(v_args) != v_np)) {
                f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("generic function '", v_fname), "' expects "), i2s(v_np)), " arguments, got "), i2s(arr_Expr_len(v_args))));
            }
            return 0;
        }
        v_gi = (v_gi + 1);
    }
    return 0;
}

int64_t f_ok_ret_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    const char* v_want;
    const char* v_got;
    if ((v_pos < 0)) {
        return 0;
    }
    if ((strcmp(v_fname, "ok") != 0)) {
        return 0;
    }
    if ((arr_Expr_len(v_args) != 1)) {
        return 0;
    }
    if ((map_str_str_has((v_sy)->vty, "@ret") == (1 != 1))) {
        return 0;
    }
    v_want = map_str_str_get((v_sy)->vty, "@ret");
    v_got = f_type_confident(v_sy, arr_Expr_get(v_args, 0));
    if (((f_confident(v_want) && f_confident(v_got)) && f_incompatible(v_sy, v_want, v_got))) {
        f_report_at(v_src, v_pos, scat(scat(scat("type mismatch: ok(", v_got), ") but function returns !"), v_want));
    }
    return 0;
}

int64_t f_chk_call(arr_Func v_funcs, s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    f_callarg_check(v_funcs, v_sy, v_fname, v_args, v_pos, v_src);
    f_ctor_arg_check(v_sy, v_fname, v_args, v_pos, v_src);
    f_variant_arg_check(v_sy, v_fname, v_args, v_pos, v_src);
    f_hof_arity_check(v_fname, v_args, v_pos, v_src);
    f_generic_arity_check(v_sy, v_fname, v_args, v_pos, v_src);
    f_ok_ret_check(v_sy, v_fname, v_args, v_pos, v_src);
    f_chk_args(v_funcs, v_sy, v_args, v_pos, v_src);
    return 0;
}

int64_t f_chk_field(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, const char* v_fname, int64_t v_pos, const char* v_src) {
    f_field_check(v_sy, v_obj, v_fname, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_obj, v_pos, v_src);
    return 0;
}

int64_t f_chk_index(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, int64_t v_pos, const char* v_src) {
    f_index_check(v_sy, v_obj, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_obj, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_idx, v_pos, v_src);
    return 0;
}

int64_t f_check_expr(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_e, int64_t v_pos, const char* v_src) {
    return ({ int64_t __m; s_Expr __s = v_e; if(__s.tag==4){ int64_t v_op = __s.u.Bin.f0; s_Expr v_l = *(__s.u.Bin.f1); s_Expr v_r = *(__s.u.Bin.f2); __m = f_chk_bin(v_funcs, v_sy, v_op, v_l, v_r, v_pos, v_src); } else if(__s.tag==6){ const char* v_fname = __s.u.Call.f0; arr_Expr v_args = __s.u.Call.f1; __m = f_chk_call(v_funcs, v_sy, v_fname, v_args, v_pos, v_src); } else if(__s.tag==7){ s_Expr v_obj = *(__s.u.Field.f0); const char* v_fnm = __s.u.Field.f1; __m = f_chk_field(v_funcs, v_sy, v_obj, v_fnm, v_pos, v_src); } else if(__s.tag==8){ s_Expr v_obj = *(__s.u.Index.f0); s_Expr v_idx = *(__s.u.Index.f1); __m = f_chk_index(v_funcs, v_sy, v_obj, v_idx, v_pos, v_src); } else if(__s.tag==5){ int64_t v_op = __s.u.Unary.f0; s_Expr v_x = *(__s.u.Unary.f1); __m = f_check_expr(v_funcs, v_sy, v_x, v_pos, v_src); } else if(__s.tag==11){ s_Expr v_x = *(__s.u.Addr.f0); __m = f_check_expr(v_funcs, v_sy, v_x, v_pos, v_src); } else if(__s.tag==9){ arr_Expr v_elems = __s.u.Array.f0; const char* v_ety = __s.u.Array.f1; __m = f_chk_array(v_funcs, v_sy, v_elems, v_pos, v_src); } else if(__s.tag==10){ const char* v_mty = __s.u.MapLit.f0; arr_Expr v_mks = __s.u.MapLit.f1; arr_Expr v_mvs = __s.u.MapLit.f2; __m = f_chk_maplit(v_funcs, v_sy, v_mks, v_mvs, v_pos, v_src); } else if(__s.tag==12){ s_Expr v_sc = *(__s.u.Match.f0); arr_str v_vn = __s.u.Match.f1; arr_str v_vb = __s.u.Match.f2; arr_Expr v_bd = __s.u.Match.f3; __m = f_chk_match(v_funcs, v_sy, v_sc, v_vn, v_vb, v_bd, v_pos, v_src); } else if(__s.tag==13){ s_Expr v_c = *(__s.u.IfE.f0); s_Expr v_t = *(__s.u.IfE.f1); s_Expr v_el2 = *(__s.u.IfE.f2); __m = f_chk3(v_funcs, v_sy, v_c, v_t, v_el2, v_pos, v_src); } else if(__s.tag==14){ s_Expr v_x = *(__s.u.Try.f0); __m = f_chk_try(v_funcs, v_sy, v_x, v_pos, v_src); } else if(__s.tag==16){ arr_Expr v_elems = __s.u.Tuple.f0; __m = f_chk_args(v_funcs, v_sy, v_elems, v_pos, v_src); } else if(__s.tag==17){ arr_Stmt v_bb = __s.u.BlockE.f0; __m = f_check_stmts(v_funcs, v_sy, v_bb, v_src); } else if(__s.tag==15){ arr_str v_ps = __s.u.Lambda.f0; arr_str v_pts = __s.u.Lambda.f1; s_Expr v_b = *(__s.u.Lambda.f2); int64_t v_id = __s.u.Lambda.f3; __m = f_check_expr(v_funcs, v_sy, v_b, v_pos, v_src); } else if(__s.tag==0){ int64_t v_v = __s.u.Num.f0; __m = 0; } else if(__s.tag==1){ const char* v_s = __s.u.Flt.f0; __m = 0; } else if(__s.tag==2){ const char* v_s = __s.u.Str.f0; __m = 0; } else if(__s.tag==3){ const char* v_n = __s.u.Var.f0; __m = 0; } else if(__s.tag==18){ __m = 0; } __m; });
}

int64_t f_chk_assign(arr_Func v_funcs, s_Syms* v_sy, const char* v_name, s_Expr v_e, int64_t v_pos, const char* v_src) {
    const char* v_lt;
    const char* v_rr;
    if ((v_pos >= 0)) {
        v_lt = f_tcon_var(v_sy, v_name);
        v_rr = f_type_confident(v_sy, v_e);
        if (((f_confident(v_lt) && f_confident(v_rr)) && f_incompatible(v_sy, v_lt, v_rr))) {
            f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("type mismatch: cannot assign ", v_rr), " to '"), v_name), "' of type "), v_lt));
        }
    }
    f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src);
    return 0;
}

int64_t f_chk_return(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_e, int64_t v_pos, const char* v_src) {
    const char* v_want;
    const char* v_got;
    if ((v_pos >= 0)) {
        if (map_str_str_has((v_sy)->vty, "@chkret")) {
            v_want = map_str_str_get((v_sy)->vty, "@chkret");
            v_got = f_type_confident(v_sy, v_e);
            if ((f_confident(v_got) && f_incompatible(v_sy, v_want, v_got))) {
                f_report_at(v_src, v_pos, scat(scat(scat("type mismatch: returning ", v_got), " but function returns "), v_want));
            }
        }
    }
    f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src);
    return 0;
}

int64_t f_chk_field_assign(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, const char* v_fnm, s_Expr v_e, int64_t v_pos, const char* v_src) {
    const char* v_slot;
    const char* v_val;
    f_field_check(v_sy, v_obj, v_fnm, v_pos, v_src);
    if ((v_pos >= 0)) {
        v_slot = f_tcon_field(v_sy, v_obj, v_fnm);
        v_val = f_type_confident(v_sy, v_e);
        if (((f_confident(v_slot) && f_confident(v_val)) && f_incompatible(v_sy, v_slot, v_val))) {
            f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("type mismatch: cannot assign ", v_val), " to field '."), v_fnm), "' of type "), v_slot));
        }
    }
    f_check_expr(v_funcs, v_sy, v_obj, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src);
    return 0;
}

const char* f_idx_slot_type(s_Syms* v_sy, s_Expr v_obj) {
    const char* v_t;
    v_t = f_type_confident(v_sy, v_obj);
    if (f_is_array_ann(v_t)) {
        return f_elem_of_ann(v_t);
    }
    if (f_is_map_ann(v_t)) {
        return f_map_vtype(v_t);
    }
    return "?";
}

int64_t f_chk_idx_assign(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_obj, s_Expr v_idx, s_Expr v_e, int64_t v_pos, const char* v_src) {
    const char* v_slot;
    const char* v_val;
    f_index_check(v_sy, v_obj, v_pos, v_src);
    if ((v_pos >= 0)) {
        v_slot = f_idx_slot_type(v_sy, v_obj);
        v_val = f_type_confident(v_sy, v_e);
        if (((f_confident(v_slot) && f_confident(v_val)) && f_incompatible(v_sy, v_slot, v_val))) {
            f_report_at(v_src, v_pos, scat(scat(scat("type mismatch: cannot assign ", v_val), " to element of type "), v_slot));
        }
    }
    f_check_expr(v_funcs, v_sy, v_obj, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_idx, v_pos, v_src);
    f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src);
    return 0;
}

int64_t f_ctor_arg_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    int64_t v_n;
    int64_t v_i;
    const char* v_k;
    const char* v_want;
    const char* v_got;
    if ((v_pos < 0)) {
        return 0;
    }
    if ((map_str_str_has((v_sy)->evar, scat("@fldn.", v_fname)) == (1 != 1))) {
        return 0;
    }
    v_n = s2i(map_str_str_get((v_sy)->evar, scat("@fldn.", v_fname)));
    if ((arr_Expr_len(v_args) != v_n)) {
        f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("constructor '", v_fname), "' expects "), i2s(v_n)), " fields, got "), i2s(arr_Expr_len(v_args))));
        return 0;
    }
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        v_k = scat(scat(scat("@fld.", v_fname), "."), i2s(v_i));
        if (map_str_str_has((v_sy)->evar, v_k)) {
            v_want = map_str_str_get((v_sy)->evar, v_k);
            v_got = f_type_confident(v_sy, arr_Expr_get(v_args, v_i));
            if (((f_confident(v_want) && f_confident(v_got)) && f_incompatible(v_sy, v_want, v_got))) {
                f_report_at(v_src, v_pos, scat(scat(scat(scat(scat(scat(scat("type mismatch: field ", i2s((v_i + 1))), " of '"), v_fname), "' expects "), v_want), ", got "), v_got));
            }
        }
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_variant_arg_check(s_Syms* v_sy, const char* v_fname, arr_Expr v_args, int64_t v_pos, const char* v_src) {
    int64_t v_n;
    int64_t v_i;
    const char* v_k;
    const char* v_want;
    const char* v_got;
    if ((v_pos < 0)) {
        return 0;
    }
    if ((map_str_str_has((v_sy)->evar, scat("@vfldn.", v_fname)) == (1 != 1))) {
        return 0;
    }
    v_n = s2i(map_str_str_get((v_sy)->evar, scat("@vfldn.", v_fname)));
    if ((arr_Expr_len(v_args) != v_n)) {
        f_report_at(v_src, v_pos, scat(scat(scat(scat(scat("variant '", v_fname), "' expects "), i2s(v_n)), " fields, got "), i2s(arr_Expr_len(v_args))));
        return 0;
    }
    v_i = 0;
    while ((v_i < arr_Expr_len(v_args))) {
        v_k = scat(scat(scat("@vfld.", v_fname), "."), i2s(v_i));
        if (map_str_str_has((v_sy)->evar, v_k)) {
            v_want = map_str_str_get((v_sy)->evar, v_k);
            v_got = f_type_confident(v_sy, arr_Expr_get(v_args, v_i));
            if (((f_confident(v_want) && f_confident(v_got)) && f_cat_incompatible(v_want, v_got))) {
                f_report_at(v_src, v_pos, scat(scat(scat(scat(scat(scat(scat("type mismatch: field ", i2s((v_i + 1))), " of variant '"), v_fname), "' expects "), v_want), ", got "), v_got));
            }
        }
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_check_tail_expr(s_Syms* v_sy, s_Expr v_e, int64_t v_pos, const char* v_want, const char* v_src) {
    const char* v_got;
    if ((v_pos < 0)) {
        return 0;
    }
    v_got = f_type_confident(v_sy, v_e);
    if ((f_confident(v_got) && f_incompatible(v_sy, v_want, v_got))) {
        f_report_at(v_src, v_pos, scat(scat(scat("type mismatch: returning ", v_got), " but function returns "), v_want));
    }
    return 0;
}

int64_t f_check_tail_if(s_Syms* v_sy, arr_Stmt v_b, arr_Stmt v_eb, const char* v_want, const char* v_src) {
    f_check_tail_return(v_sy, v_b, v_want, v_src);
    f_check_tail_return(v_sy, v_eb, v_want, v_src);
    return 0;
}

int64_t f_check_tail_stmt(s_Syms* v_sy, s_Stmt v_s, const char* v_want, const char* v_src) {
    return ({ int64_t __m; s_Stmt __s = v_s; if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); int64_t v_pos = __s.u.SExpr.f1; __m = f_check_tail_expr(v_sy, v_e, v_pos, v_want, v_src); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_check_tail_if(v_sy, v_b, v_eb, v_want, v_src); } else if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); int64_t v_pos = __s.u.SDecl.f2; __m = 0; } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = 0; } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); int64_t v_pos = __s.u.SAssign.f2; __m = 0; } else if(__s.tag==3){ s_Expr v_o = *(__s.u.SIdxAssign.f0); s_Expr v_i = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); int64_t v_pos = __s.u.SIdxAssign.f3; __m = 0; } else if(__s.tag==4){ s_Expr v_o = *(__s.u.SFieldAssign.f0); const char* v_f = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); int64_t v_pos = __s.u.SFieldAssign.f3; __m = 0; } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); int64_t v_pos = __s.u.SReturn.f1; __m = 0; } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); int64_t v_pos = __s.u.SPrint.f1; __m = 0; } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = 0; } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = 0; } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = 0; } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = 0; } else if(__s.tag==12){ __m = 0; } else if(__s.tag==13){ __m = 0; } __m; });
}

int64_t f_check_tail_return(s_Syms* v_sy, arr_Stmt v_body, const char* v_want, const char* v_src) {
    if ((arr_Stmt_len(v_body) == 0)) {
        return 0;
    }
    f_check_tail_stmt(v_sy, arr_Stmt_get(v_body, (arr_Stmt_len(v_body) - 1)), v_want, v_src);
    return 0;
}

int64_t f_chk_if(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, arr_Stmt v_eb, const char* v_src) {
    f_check_expr(v_funcs, v_sy, v_c, (0 - 1), v_src);
    f_check_stmts(v_funcs, v_sy, v_b, v_src);
    f_check_stmts(v_funcs, v_sy, v_eb, v_src);
    return 0;
}

int64_t f_chk_body(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_c, arr_Stmt v_b, const char* v_src) {
    f_check_expr(v_funcs, v_sy, v_c, (0 - 1), v_src);
    f_check_stmts(v_funcs, v_sy, v_b, v_src);
    return 0;
}

int64_t f_chk_coll(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_coll, arr_Stmt v_b, const char* v_src) {
    f_check_expr(v_funcs, v_sy, v_coll, (0 - 1), v_src);
    f_check_stmts(v_funcs, v_sy, v_b, v_src);
    return 0;
}

int64_t f_chk_range(arr_Func v_funcs, s_Syms* v_sy, s_Expr v_lo, s_Expr v_hi, arr_Stmt v_b, const char* v_src) {
    f_check_expr(v_funcs, v_sy, v_lo, (0 - 1), v_src);
    f_check_expr(v_funcs, v_sy, v_hi, (0 - 1), v_src);
    f_check_stmts(v_funcs, v_sy, v_b, v_src);
    return 0;
}

int64_t f_check_stmt(arr_Func v_funcs, s_Syms* v_sy, s_Stmt v_s, const char* v_src) {
    return ({ int64_t __m; s_Stmt __s = v_s; if(__s.tag==0){ const char* v_name = __s.u.SDecl.f0; s_Expr v_e = *(__s.u.SDecl.f1); int64_t v_pos = __s.u.SDecl.f2; __m = f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src); } else if(__s.tag==1){ arr_str v_names = __s.u.SDestructure.f0; s_Expr v_e = *(__s.u.SDestructure.f1); __m = f_check_expr(v_funcs, v_sy, v_e, (0 - 1), v_src); } else if(__s.tag==2){ const char* v_name = __s.u.SAssign.f0; s_Expr v_e = *(__s.u.SAssign.f1); int64_t v_pos = __s.u.SAssign.f2; __m = f_chk_assign(v_funcs, v_sy, v_name, v_e, v_pos, v_src); } else if(__s.tag==3){ s_Expr v_obj = *(__s.u.SIdxAssign.f0); s_Expr v_idx = *(__s.u.SIdxAssign.f1); s_Expr v_e = *(__s.u.SIdxAssign.f2); int64_t v_pos = __s.u.SIdxAssign.f3; __m = f_chk_idx_assign(v_funcs, v_sy, v_obj, v_idx, v_e, v_pos, v_src); } else if(__s.tag==4){ s_Expr v_obj = *(__s.u.SFieldAssign.f0); const char* v_fnm = __s.u.SFieldAssign.f1; s_Expr v_e = *(__s.u.SFieldAssign.f2); int64_t v_pos = __s.u.SFieldAssign.f3; __m = f_chk_field_assign(v_funcs, v_sy, v_obj, v_fnm, v_e, v_pos, v_src); } else if(__s.tag==5){ s_Expr v_e = *(__s.u.SReturn.f0); int64_t v_pos = __s.u.SReturn.f1; __m = f_chk_return(v_funcs, v_sy, v_e, v_pos, v_src); } else if(__s.tag==6){ s_Expr v_e = *(__s.u.SPrint.f0); int64_t v_pos = __s.u.SPrint.f1; __m = f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src); } else if(__s.tag==7){ s_Expr v_c = *(__s.u.SIf.f0); arr_Stmt v_b = __s.u.SIf.f1; arr_Stmt v_eb = __s.u.SIf.f2; __m = f_chk_if(v_funcs, v_sy, v_c, v_b, v_eb, v_src); } else if(__s.tag==8){ s_Expr v_c = *(__s.u.SLoop.f0); arr_Stmt v_b = __s.u.SLoop.f1; __m = f_chk_body(v_funcs, v_sy, v_c, v_b, v_src); } else if(__s.tag==9){ const char* v_vnm = __s.u.SLoopIn.f0; s_Expr v_coll = *(__s.u.SLoopIn.f1); arr_Stmt v_b = __s.u.SLoopIn.f2; __m = f_chk_coll(v_funcs, v_sy, v_coll, v_b, v_src); } else if(__s.tag==10){ const char* v_kn = __s.u.SLoopKV.f0; const char* v_vn = __s.u.SLoopKV.f1; s_Expr v_coll = *(__s.u.SLoopKV.f2); arr_Stmt v_b = __s.u.SLoopKV.f3; __m = f_chk_coll(v_funcs, v_sy, v_coll, v_b, v_src); } else if(__s.tag==11){ const char* v_v = __s.u.SLoopRange.f0; s_Expr v_lo = *(__s.u.SLoopRange.f1); s_Expr v_hi = *(__s.u.SLoopRange.f2); arr_Stmt v_b = __s.u.SLoopRange.f3; __m = f_chk_range(v_funcs, v_sy, v_lo, v_hi, v_b, v_src); } else if(__s.tag==12){ __m = 0; } else if(__s.tag==13){ __m = 0; } else if(__s.tag==14){ s_Expr v_e = *(__s.u.SExpr.f0); int64_t v_pos = __s.u.SExpr.f1; __m = f_check_expr(v_funcs, v_sy, v_e, v_pos, v_src); } __m; });
}

int64_t f_check_stmts(arr_Func v_funcs, s_Syms* v_sy, arr_Stmt v_body, const char* v_src) {
    int64_t v_i;
    v_i = 0;
    while ((v_i < arr_Stmt_len(v_body))) {
        f_check_stmt(v_funcs, v_sy, arr_Stmt_get(v_body, v_i), v_src);
        v_i = (v_i + 1);
    }
    return 0;
}

int64_t f_check_fn(arr_Func v_funcs, s_Syms* v_base, s_Func v_f, const char* v_src) {
    s_Syms v_sy;
    arr_Stmt v_fb;
    const char* v_hd;
    v_sy = f_seed_fn(v_base, v_f);
    v_fb = f_stamp_empty_maps((&v_sy), (v_f).body);
    v_sy = f_seed_fn(v_base, v_f);
    v_hd = f_hoist_decls((&v_sy), v_fb, "    ");
    if ((((int64_t)strlen((v_f).ret)) > 0)) {
        f_set_ty((&v_sy), "@chkret", (v_f).ret);
    }
    f_check_stmts(v_funcs, (&v_sy), v_fb, v_src);
    if ((((int64_t)strlen((v_f).ret)) > 0)) {
        f_check_tail_return((&v_sy), v_fb, (v_f).ret, v_src);
    }
    return 0;
}

int64_t f_check_program(arr_Func v_funcs, s_Syms* v_base, arr_Stmt v_mains, const char* v_src) {
    int64_t v_i;
    s_Syms v_msy;
    arr_Stmt v_mm;
    const char* v_mhd;
    v_i = 0;
    while ((v_i < arr_Func_len(v_funcs))) {
        f_check_fn(v_funcs, v_base, arr_Func_get(v_funcs, v_i), v_src);
        v_i = (v_i + 1);
    }
    v_msy = mk_Syms(map_str_str_new(), (v_base)->fld, (v_base)->ctors, (v_base)->frets, (v_base)->evar, (v_base)->vft, (v_base)->gfns, (v_base)->lams);
    v_mm = f_stamp_empty_maps((&v_msy), v_mains);
    v_msy = mk_Syms(map_str_str_new(), (v_base)->fld, (v_base)->ctors, (v_base)->frets, (v_base)->evar, (v_base)->vft, (v_base)->gfns, (v_base)->lams);
    v_mhd = f_hoist_decls((&v_msy), v_mm, "    ");
    f_check_stmts(v_funcs, (&v_msy), v_mm, v_src);
    return 0;
}

const char* f_compile_to_c(const char* v_src, const char* v_dir) {
    arr_Token v_toks;
    s_P v_p;
    arr_StructDef v_structs;
    arr_EnumDef v_enums;
    arr_Func v_funcs;
    arr_Func v_externs;
    arr_str v_cincs;
    arr_str v_csrcs;
    arr_ClassDef v_classes;
    arr_Stmt v_mains;
    const char* v_fname;
    arr_Func v_gfuncs;
    arr_Func v_rfuncs;
    int64_t v_gi;
    arr_Func v_nolams;
    s_Syms v_base;
    int64_t v_lci;
    s_ClassDef v_cd;
    int64_t v_parent_hasvt;
    int64_t v_own_virt;
    int64_t v_vci;
    int64_t v_hasvt;
    arr_str v_allf;
    arr_str v_allt;
    int64_t v_k;
    int64_t v_pj;
    int64_t v_fi;
    int64_t v_mi;
    s_Func v_m;
    const char* v_vr;
    const char* v_va;
    int64_t v_ai;
    const char* v_slots;
    int64_t v_vm;
    const char* v_mn;
    arr_str v_sp;
    int64_t v_spi;
    int64_t v_si;
    s_StructDef v_sd;
    int64_t v_uidx;
    int64_t v_ei;
    s_EnumDef v_ed;
    const char* v_order;
    int64_t v_vi;
    const char* v_vn;
    const char* v_vfs;
    arr_str v_vparts;
    int64_t v_vpj;
    int64_t v_xi;
    int64_t v_fi2;
    s_Func v_f;
    int64_t v_gj;
    int64_t v_cf;
    s_Syms v_csy;
    const char* v_cdecls;
    s_Syms v_csm;
    const char* v_cmdecls;
    const char* v_out;
    int64_t v_lk;
    const char* v_libline;
    const char* v_cline;
    int64_t v_cs;
    int64_t v_needs_time;
    int64_t v_needs_sock;
    int64_t v_needs_proc;
    int64_t v_needs_thread;
    int64_t v_needs_regex;
    int64_t v_needs_tls;
    int64_t v_needs_pg;
    int64_t v_needs_exedir;
    int64_t v_ci;
    arr_str v_mapkeys;
    arr_str v_mapvals;
    int64_t v_mvs;
    int64_t v_mve;
    int64_t v_vt1;
    arr_str v_tup_seen;
    int64_t v_ti;
    const char* v_rt2;
    const char* v_suf;
    int64_t v_found;
    int64_t v_tk;
    int64_t v_xj;
    int64_t v_i;
    int64_t v_vt2;
    int64_t v_lm;
    s_Syms v_sy;
    arr_Stmt v_fb;
    const char* v_decls;
    s_Syms v_msy;
    arr_Stmt v_mmains;
    const char* v_mdecls;
    const char* v_maincall;
    int64_t v_mk;
    v_toks = f_lex(v_src);
    v_p = mk_P(v_toks, 0, v_src);
    v_structs = ({ arr_StructDef __a = arr_StructDef_new(); __a; });
    v_enums = ({ arr_EnumDef __a = arr_EnumDef_new(); __a; });
    v_funcs = ({ arr_Func __a = arr_Func_new(); __a; });
    v_externs = ({ arr_Func __a = arr_Func_new(); __a; });
    v_cincs = ({ arr_str __a = arr_str_new(); __a; });
    v_csrcs = ({ arr_str __a = arr_str_new(); __a; });
    v_classes = ({ arr_ClassDef __a = arr_ClassDef_new(); __a; });
    v_mains = ({ arr_Stmt __a = arr_Stmt_new(); __a; });
    while ((f_ckind((&v_p)) != f_TK_EOF())) {
        if ((f_ckind((&v_p)) == f_TK_SEMI())) {
            f_adv((&v_p));
            continue;
        }
        if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "st") == 0))) {
            v_structs = arr_StructDef_push(v_structs, f_parse_struct((&v_p)));
        } else {
            if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "en") == 0))) {
                v_enums = arr_EnumDef_push(v_enums, f_parse_enum((&v_p)));
            } else {
                if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "cl") == 0))) {
                    v_classes = arr_ClassDef_push(v_classes, f_parse_class((&v_p)));
                } else {
                    if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "cinc") == 0))) {
                        f_adv((&v_p));
                        v_cincs = arr_str_push(v_cincs, f_ctext((&v_p)));
                        f_adv((&v_p));
                    } else {
                        if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "csrc") == 0))) {
                            f_adv((&v_p));
                            if ((f_ckind((&v_p)) == f_TK_RAWSTR())) {
                                v_fname = scat(scat("__ailinline_", i2s(arr_str_len(v_csrcs))), ".cpp");
                                write_file_c(scat(v_dir, v_fname), f_ctext((&v_p)));
                                f_adv((&v_p));
                                v_csrcs = arr_str_push(v_csrcs, v_fname);
                            } else {
                                v_csrcs = arr_str_push(v_csrcs, f_ctext((&v_p)));
                                f_adv((&v_p));
                            }
                        } else {
                            if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "ex") == 0))) {
                                v_externs = arr_Func_push(v_externs, f_parse_extern((&v_p)));
                            } else {
                                if (((f_ckind((&v_p)) == f_TK_IDENT()) && (strcmp(f_ctext((&v_p)), "fn") == 0))) {
                                    v_funcs = arr_Func_push(v_funcs, f_parse_func((&v_p)));
                                } else {
                                    v_mains = arr_Stmt_push(v_mains, f_parse_stmt((&v_p)));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    v_gfuncs = ({ arr_Func __a = arr_Func_new(); __a; });
    v_rfuncs = ({ arr_Func __a = arr_Func_new(); __a; });
    v_gi = 0;
    while ((v_gi < arr_Func_len(v_funcs))) {
        if ((arr_str_len((arr_Func_get(v_funcs, v_gi)).tparams) > 0)) {
            v_gfuncs = arr_Func_push(v_gfuncs, arr_Func_get(v_funcs, v_gi));
        } else {
            v_rfuncs = arr_Func_push(v_rfuncs, arr_Func_get(v_funcs, v_gi));
        }
        v_gi = (v_gi + 1);
    }
    v_funcs = v_rfuncs;
    v_nolams = ({ arr_Func __a = arr_Func_new(); __a; });
    v_base = mk_Syms(map_str_str_new(), map_str_str_new(), map_str_str_new(), map_str_str_new(), map_str_str_new(), map_str_str_new(), v_gfuncs, v_nolams);
    v_lci = 0;
    while ((v_lci < arr_ClassDef_len(v_classes))) {
        v_cd = arr_ClassDef_get(v_classes, v_lci);
        v_parent_hasvt = ((((int64_t)strlen((v_cd).parent)) > 0) && map_str_str_has((v_base).evar, scat("@class.hasvt.", (v_cd).parent)));
        v_own_virt = (1 != 1);
        v_vci = 0;
        while ((v_vci < arr_str_len((v_cd).vflags))) {
            if ((strcmp(arr_str_get((v_cd).vflags, v_vci), "1") == 0)) {
                v_own_virt = (1 == 1);
            }
            v_vci = (v_vci + 1);
        }
        v_hasvt = (v_own_virt || v_parent_hasvt);
        v_allf = ({ arr_str __a = arr_str_new(); __a; });
        v_allt = ({ arr_str __a = arr_str_new(); __a; });
        if ((v_hasvt && (v_parent_hasvt == (1 != 1)))) {
            v_allf = arr_str_push(v_allf, "__vt");
            v_allt = arr_str_push(v_allt, "@vtp");
        }
        if ((((int64_t)strlen((v_cd).parent)) > 0)) {
            v_k = 0;
            while ((v_k < arr_StructDef_len(v_structs))) {
                if ((strcmp((arr_StructDef_get(v_structs, v_k)).name, (v_cd).parent) == 0)) {
                    v_pj = 0;
                    while ((v_pj < arr_str_len((arr_StructDef_get(v_structs, v_k)).fnames))) {
                        v_allf = arr_str_push(v_allf, arr_str_get((arr_StructDef_get(v_structs, v_k)).fnames, v_pj));
                        v_allt = arr_str_push(v_allt, arr_str_get((arr_StructDef_get(v_structs, v_k)).ftypes, v_pj));
                        v_pj = (v_pj + 1);
                    }
                }
                v_k = (v_k + 1);
            }
        }
        v_fi = 0;
        while ((v_fi < arr_str_len((v_cd).fnames))) {
            v_allf = arr_str_push(v_allf, arr_str_get((v_cd).fnames, v_fi));
            v_allt = arr_str_push(v_allt, arr_str_get((v_cd).ftypes, v_fi));
            v_fi = (v_fi + 1);
        }
        v_structs = arr_StructDef_push(v_structs, mk_StructDef((v_cd).name, v_allf, v_allt));
        map_str_str_set((v_base).evar, scat("@class.", (v_cd).name), "1");
        map_str_str_set((v_base).evar, scat("@class.parent.", (v_cd).name), (v_cd).parent);
        if (v_hasvt) {
            map_str_str_set((v_base).evar, scat("@class.hasvt.", (v_cd).name), "1");
        }
        v_mi = 0;
        while ((v_mi < arr_Func_len((v_cd).methods))) {
            v_m = arr_Func_get((v_cd).methods, v_mi);
            v_funcs = arr_Func_push(v_funcs, mk_Func(scat(scat((v_cd).name, "_"), (v_m).name), (v_m).params, (v_m).ptypes, (v_m).ret, "", (v_m).tparams, (v_m).body));
            map_str_str_set((v_base).evar, scat(scat(scat("@class.defines.", (v_cd).name), "."), (v_m).name), "1");
            map_str_str_set((v_base).evar, scat(scat(scat("@mclass.", (v_cd).name), "_"), (v_m).name), (v_cd).name);
            if ((strcmp(arr_str_get((v_cd).vflags, v_mi), "1") == 0)) {
                v_vr = "void";
                if ((((int64_t)strlen((v_m).ret)) > 0)) {
                    v_vr = f_cty((v_m).ret);
                }
                map_str_str_set((v_base).evar, scat(scat(scat("@class.vret.", (v_cd).name), "."), (v_m).name), v_vr);
                v_va = "";
                v_ai = 1;
                while ((v_ai < arr_str_len((v_m).ptypes))) {
                    v_va = scat(scat(v_va, ", "), f_cty(arr_str_get((v_m).ptypes, v_ai)));
                    v_ai = (v_ai + 1);
                }
                map_str_str_set((v_base).evar, scat(scat(scat("@class.vargs.", (v_cd).name), "."), (v_m).name), v_va);
            }
            v_mi = (v_mi + 1);
        }
        v_slots = "";
        if (v_parent_hasvt) {
            v_slots = map_str_str_get((v_base).evar, scat("@class.vslots.", (v_cd).parent));
        }
        v_vm = 0;
        while ((v_vm < arr_Func_len((v_cd).methods))) {
            if ((strcmp(arr_str_get((v_cd).vflags, v_vm), "1") == 0)) {
                v_mn = (arr_Func_get((v_cd).methods, v_vm)).name;
                if ((f_slot_has(v_slots, v_mn) == (1 != 1))) {
                    if ((((int64_t)strlen(v_slots)) == 0)) {
                        v_slots = v_mn;
                    } else {
                        v_slots = scat(scat(v_slots, ";"), v_mn);
                    }
                }
            }
            v_vm = (v_vm + 1);
        }
        map_str_str_set((v_base).evar, scat("@class.vslots.", (v_cd).name), v_slots);
        v_sp = split(v_slots, ";");
        v_spi = 0;
        while ((v_spi < arr_str_len(v_sp))) {
            if ((((int64_t)strlen(arr_str_get(v_sp, v_spi))) > 0)) {
                map_str_str_set((v_base).evar, scat(scat(scat("@class.virt.", (v_cd).name), "."), arr_str_get(v_sp, v_spi)), "1");
            }
            v_spi = (v_spi + 1);
        }
        v_lci = (v_lci + 1);
    }
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_sd = arr_StructDef_get(v_structs, v_si);
        map_str_str_set((v_base).ctors, (v_sd).name, "1");
        v_fi = 0;
        v_uidx = 0;
        while ((v_fi < arr_str_len((v_sd).fnames))) {
            map_str_str_set((v_base).fld, f_fkey((v_sd).name, arr_str_get((v_sd).fnames, v_fi)), arr_str_get((v_sd).ftypes, v_fi));
            if ((strcmp(arr_str_get((v_sd).fnames, v_fi), "__vt") != 0)) {
                map_str_str_set((v_base).evar, scat(scat(scat("@fld.", (v_sd).name), "."), i2s(v_uidx)), arr_str_get((v_sd).ftypes, v_fi));
                v_uidx = (v_uidx + 1);
            }
            v_fi = (v_fi + 1);
        }
        map_str_str_set((v_base).evar, scat("@fldn.", (v_sd).name), i2s(v_uidx));
        v_si = (v_si + 1);
    }
    v_enums = f_box_cross_enums(v_enums);
    v_externs = f_mark_extern_ctypes(v_externs, v_structs, v_enums);
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_ed = arr_EnumDef_get(v_enums, v_ei);
        v_order = "";
        v_vi = 0;
        while ((v_vi < arr_str_len((v_ed).vnames))) {
            v_vn = arr_str_get((v_ed).vnames, v_vi);
            map_str_str_set((v_base).evar, v_vn, (v_ed).name);
            map_str_str_set((v_base).vft, v_vn, arr_str_get((v_ed).vftypes, v_vi));
            v_vfs = arr_str_get((v_ed).vftypes, v_vi);
            if ((str_contains(v_vfs, "(") == (1 != 1))) {
                if ((((int64_t)strlen(v_vfs)) == 0)) {
                    map_str_str_set((v_base).evar, scat("@vfldn.", v_vn), "0");
                } else {
                    v_vparts = f_split_semi(v_vfs);
                    v_vpj = 0;
                    while ((v_vpj < arr_str_len(v_vparts))) {
                        map_str_str_set((v_base).evar, scat(scat(scat("@vfld.", v_vn), "."), i2s(v_vpj)), arr_str_get(v_vparts, v_vpj));
                        v_vpj = (v_vpj + 1);
                    }
                    map_str_str_set((v_base).evar, scat("@vfldn.", v_vn), i2s(arr_str_len(v_vparts)));
                }
            }
            if ((v_vi == 0)) {
                v_order = v_vn;
            } else {
                v_order = scat(scat(v_order, ";"), v_vn);
            }
            v_vi = (v_vi + 1);
        }
        map_str_str_set((v_base).evar, scat("@order.", (v_ed).name), v_order);
        v_ei = (v_ei + 1);
    }
    v_xi = 0;
    while ((v_xi < arr_Func_len(v_externs))) {
        if ((((int64_t)strlen((arr_Func_get(v_externs, v_xi)).ret)) > 0)) {
            map_str_str_set((v_base).frets, (arr_Func_get(v_externs, v_xi)).name, (arr_Func_get(v_externs, v_xi)).ret);
        }
        v_xi = (v_xi + 1);
    }
    v_fi2 = 0;
    while ((v_fi2 < arr_Func_len(v_funcs))) {
        v_f = arr_Func_get(v_funcs, v_fi2);
        map_str_str_set((v_base).frets, (v_f).name, f_infer_ret(v_f, v_base));
        v_fi2 = (v_fi2 + 1);
    }
    v_gj = 0;
    while ((v_gj < arr_Func_len(v_gfuncs))) {
        map_str_str_set((v_base).evar, scat("@generic.", (arr_Func_get(v_gfuncs, v_gj)).name), "1");
        v_gj = (v_gj + 1);
    }
    v_cf = 0;
    while ((v_cf < arr_Func_len(v_funcs))) {
        v_csy = f_seed_fn((&v_base), arr_Func_get(v_funcs, v_cf));
        v_cdecls = f_hoist_decls((&v_csy), (arr_Func_get(v_funcs, v_cf)).body, "    ");
        f_collect_lams((&v_base), (&v_csy), (arr_Func_get(v_funcs, v_cf)).body);
        v_cf = (v_cf + 1);
    }
    v_csm = mk_Syms((v_base).vty, (v_base).fld, (v_base).ctors, (v_base).frets, (v_base).evar, (v_base).vft, (v_base).gfns, (v_base).lams);
    (v_csm).vty = map_str_str_new();
    v_cmdecls = f_hoist_decls((&v_csm), v_mains, "    ");
    f_collect_lams((&v_base), (&v_csm), v_mains);
    f_check_program(v_funcs, (&v_base), v_mains, v_src);
    v_out = "";
    v_lk = 0;
    v_libline = "";
    while ((v_lk < arr_Func_len(v_externs))) {
        if ((((int64_t)strlen((arr_Func_get(v_externs, v_lk)).lib)) > 0)) {
            v_libline = scat(scat(v_libline, " "), (arr_Func_get(v_externs, v_lk)).lib);
        }
        v_lk = (v_lk + 1);
    }
    if ((((int64_t)strlen(v_libline)) > 0)) {
        v_out = scat(scat("// @links:", v_libline), "\n");
    }
    v_cline = "";
    v_cs = 0;
    while ((v_cs < arr_str_len(v_csrcs))) {
        v_cline = scat(scat(v_cline, " "), arr_str_get(v_csrcs, v_cs));
        v_cs = (v_cs + 1);
    }
    if ((((int64_t)strlen(v_cline)) > 0)) {
        v_out = scat(scat(scat(v_out, "// @csrc:"), v_cline), "\n");
    }
    v_needs_time = ((((f_has_sub(v_src, "now_ms") || f_has_sub(v_src, "now_us")) || f_has_sub(v_src, "mono_ms")) || f_has_sub(v_src, "sleep_ms")) || f_has_sub(v_src, "time_iso"));
    v_needs_sock = (((((f_has_sub(v_src, "tcp_listen") || f_has_sub(v_src, "tcp_connect")) || f_has_sub(v_src, "tcp_accept")) || f_has_sub(v_src, "sock_send")) || f_has_sub(v_src, "sock_recv")) || f_has_sub(v_src, "sock_close"));
    v_needs_proc = (((f_has_sub(v_src, "proc_fork") || f_has_sub(v_src, "proc_getpid")) || f_has_sub(v_src, "proc_no_zombies")) || f_has_sub(v_src, "proc_reap"));
    v_needs_thread = ((((((((f_has_sub(v_src, "thread_spawn") || f_has_sub(v_src, "thread_join")) || f_has_sub(v_src, "mutex_new")) || f_has_sub(v_src, "mutex_lock")) || f_has_sub(v_src, "mutex_unlock")) || f_has_sub(v_src, "chan_new")) || f_has_sub(v_src, "chan_send")) || f_has_sub(v_src, "chan_recv")) || f_has_sub(v_src, "chan_close"));
    v_needs_regex = (f_has_sub(v_src, "regex_match") || f_has_sub(v_src, "regex_find"));
    v_needs_tls = (f_has_sub(v_src, "tls_") || f_has_sub(v_src, "sha1"));
    v_needs_pg = f_has_sub(v_src, "pg_");
    v_needs_exedir = f_has_sub(v_src, "exe_dir");
    if (v_needs_thread) {
        v_out = scat(v_out, "#ifndef _WIN32\n#define GC_THREADS\n#endif\n");
    }
    v_out = scat(v_out, "#include <stdio.h>\n#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdarg.h>\n#include <gc.h>\n");
    if (v_needs_time) {
        v_out = scat(v_out, "#include <time.h>\n");
    }
    if (v_needs_sock) {
        v_out = scat(v_out, "#ifndef _WIN32\n#include <sys/socket.h>\n#include <netinet/in.h>\n#include <arpa/inet.h>\n#include <netdb.h>\n#include <unistd.h>\n#endif\n");
    }
    if (v_needs_proc) {
        v_out = scat(v_out, "#ifndef _WIN32\n#include <unistd.h>\n#include <signal.h>\n#include <sys/wait.h>\n#endif\n");
    }
    if (v_needs_regex) {
        v_out = scat(v_out, "#ifndef _WIN32\n#include <regex.h>\n#endif\n");
    }
    if (v_needs_tls) {
        v_out = scat(v_out, "#ifndef _WIN32\n#include <openssl/ssl.h>\n#include <openssl/err.h>\n#include <openssl/sha.h>\n#endif\n");
    }
    if (v_needs_pg) {
        v_out = scat(v_out, "#ifndef _WIN32\n#include <libpq-fe.h>\n#endif\n");
    }
    if (v_needs_exedir) {
        v_out = scat(v_out, "#ifdef _WIN32\nunsigned long GetModuleFileNameA(void*, char*, unsigned long);\n#elif defined(__APPLE__)\n#include <mach-o/dyld.h>\n#else\n#include <unistd.h>\n#endif\n");
    }
    v_ci = 0;
    while ((v_ci < arr_str_len(v_cincs))) {
        v_out = scat(scat(scat(v_out, "#include \""), arr_str_get(v_cincs, v_ci)), "\"\n");
        v_ci = (v_ci + 1);
    }
    v_out = scat(v_out, "static const char* scat(const char* a, const char* b){ size_t la=strlen(a), lb=strlen(b); char* r=(char*)GC_MALLOC(la+lb+1); memcpy(r,a,la); memcpy(r+la,b,lb); r[la+lb]=0; return r; }\n");
    v_out = scat(v_out, "static const char* i2s(long long v){ char* r=(char*)GC_MALLOC(24); snprintf(r,24,\"%lld\",v); return r; }\n");
    v_out = scat(v_out, "static const char* substr(const char* s, int64_t a, int64_t b){ int64_t n=(int64_t)strlen(s); if(a<0)a=0; if(b>n)b=n; if(b<a)b=a; int64_t L=b-a; char* r=(char*)GC_MALLOC(L+1); memcpy(r,s+a,L); r[L]=0; return r; }\n");
    v_out = scat(v_out, "static int64_t s2i(const char* s){ return (int64_t)strtoll(s,0,10); }\n");
    v_out = scat(v_out, "static const char* f2s(double v){ char* r=(char*)GC_MALLOC(32); snprintf(r,32,\"%g\",v); return r; }\n");
    v_out = scat(v_out, "static double s2f(const char* s){ return strtod(s,0); }\n");
    v_out = scat(v_out, "static const char* ail_u16to8(const unsigned char* p, long n, int be){ char* o=(char*)GC_MALLOC((size_t)(n*2+4)); size_t k=0; long i=0; while(i+1<n){ unsigned cu=be?((unsigned)p[i]<<8|p[i+1]):((unsigned)p[i]|(unsigned)p[i+1]<<8); i+=2; unsigned cp=cu; if(cu>=0xD800&&cu<=0xDBFF&&i+1<n){ unsigned lo=be?((unsigned)p[i]<<8|p[i+1]):((unsigned)p[i]|(unsigned)p[i+1]<<8); if(lo>=0xDC00&&lo<=0xDFFF){ cp=0x10000u+((cu-0xD800u)<<10)+(lo-0xDC00u); i+=2; } } if(cp<0x80){ o[k++]=(char)cp; } else if(cp<0x800){ o[k++]=(char)(0xC0|(cp>>6)); o[k++]=(char)(0x80|(cp&0x3F)); } else if(cp<0x10000){ o[k++]=(char)(0xE0|(cp>>12)); o[k++]=(char)(0x80|((cp>>6)&0x3F)); o[k++]=(char)(0x80|(cp&0x3F)); } else { o[k++]=(char)(0xF0|(cp>>18)); o[k++]=(char)(0x80|((cp>>12)&0x3F)); o[k++]=(char)(0x80|((cp>>6)&0x3F)); o[k++]=(char)(0x80|(cp&0x3F)); } } o[k]=0; return o; }\n");
    v_out = scat(v_out, "static const char* read_file_c(const char* path){ FILE* f=fopen(path,\"rb\"); if(!f) return \"\"; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); unsigned char* b=(unsigned char*)GC_MALLOC(n+1); if(n>0) fread(b,1,n,f); b[n]=0; fclose(f); if(n>=3&&b[0]==0xEF&&b[1]==0xBB&&b[2]==0xBF){ memmove(b,b+3,(size_t)(n-3)+1); return (const char*)b; } if(n>=2&&b[0]==0xFF&&b[1]==0xFE){ return ail_u16to8(b+2,n-2,0); } if(n>=2&&b[0]==0xFE&&b[1]==0xFF){ return ail_u16to8(b+2,n-2,1); } return (const char*)b; }\n");
    v_out = scat(v_out, "static int64_t write_file_c(const char* path, const char* content){ FILE* f=fopen(path,\"wb\"); if(!f) return 0; fwrite(content,1,strlen(content),f); fclose(f); return 1; }\n");
    v_out = scat(v_out, "typedef struct { int64_t len; const uint8_t* data; } ailang_bytes;\n");
    v_out = scat(v_out, "static ailang_bytes str_to_bytes(const char* s){ ailang_bytes r; if(!s){ r.len=0; r.data=(const uint8_t*)\"\"; return r; } size_t n=strlen(s); r.len=(int64_t)n; r.data=(const uint8_t*)s; return r; }\n");
    v_out = scat(v_out, "static const char* bytes_to_str(ailang_bytes b){ char* buf=(char*)GC_MALLOC((size_t)b.len+1); if(b.len>0) memcpy(buf,b.data,(size_t)b.len); buf[b.len]=0; return buf; }\n");
    v_out = scat(v_out, "static int64_t bytes_at(ailang_bytes b, int64_t i){ if(i<0||i>=b.len) return 0; return (int64_t)b.data[i]; }\n");
    v_out = scat(v_out, "static ailang_bytes bytes_slice(ailang_bytes b, int64_t lo, int64_t hi){ if(lo<0) lo=0; if(hi>b.len) hi=b.len; if(lo>hi) lo=hi; int64_t n=hi-lo; ailang_bytes r; r.len=n; uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)(n>0?n:1)); if(n>0) memcpy(buf,b.data+lo,(size_t)n); r.data=buf; return r; }\n");
    v_out = scat(v_out, "static ailang_bytes read_file_bytes(const char* path){ ailang_bytes r; r.len=0; r.data=(const uint8_t*)\"\"; FILE* f=fopen(path,\"rb\"); if(!f) return r; if(fseek(f,0,SEEK_END)!=0){ fclose(f); return r; } long sz=ftell(f); if(sz<0){ fclose(f); return r; } fseek(f,0,SEEK_SET); uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)(sz>0?sz:1)); size_t got=fread(buf,1,(size_t)sz,f); fclose(f); r.len=(int64_t)got; r.data=buf; return r; }\n");
    v_out = scat(v_out, "static int64_t write_file_bytes(const char* path, ailang_bytes b){ FILE* f=fopen(path,\"wb\"); if(!f) return 0; size_t want=(size_t)b.len; size_t got=b.len>0?fwrite(b.data,1,want,f):0; int closed=fclose(f); return (closed==0 && got==want)?1:0; }\n");
    v_out = scat(v_out, "static void print_bytes(ailang_bytes b){ putchar(98); putchar(34); for(int64_t i=0;i<b.len;i++){ unsigned c=b.data[i]; if(c==34){ putchar(92); putchar(34); } else if(c==92){ putchar(92); putchar(92); } else if(c==10){ putchar(92); putchar(110); } else if(c==13){ putchar(92); putchar(114); } else if(c==9){ putchar(92); putchar(116); } else if(c>=32 && c<127){ putchar((int)c); } else { putchar(92); putchar(120); printf(\"%02x\", c); } } putchar(34); }\n");
    v_out = scat(v_out, "static int64_t str_contains(const char* h, const char* n){ if(!h||!n) return 0; return strstr(h,n)!=0; }\n");
    v_out = scat(v_out, "static int64_t starts_with(const char* s, const char* p){ if(!s||!p) return 0; size_t lp=strlen(p); return strncmp(s,p,lp)==0; }\n");
    v_out = scat(v_out, "static int64_t ends_with(const char* s, const char* su){ if(!s||!su) return 0; size_t ls=strlen(s),lu=strlen(su); if(lu>ls) return 0; return memcmp(s+ls-lu,su,lu)==0; }\n");
    v_out = scat(v_out, "static int64_t str_index_of(const char* h, const char* n){ if(!h||!n) return -1; const char* p=strstr(h,n); return p?(int64_t)(p-h):(int64_t)-1; }\n");
    v_out = scat(v_out, "static const char* to_upper(const char* s){ if(!s) return \"\"; size_t n=strlen(s); char* o=(char*)GC_MALLOC(n+1); for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)s[i]; o[i]=(c>=97&&c<=122)?(char)(c-32):(char)c; } o[n]=0; return o; }\n");
    v_out = scat(v_out, "static const char* to_lower(const char* s){ if(!s) return \"\"; size_t n=strlen(s); char* o=(char*)GC_MALLOC(n+1); for(size_t i=0;i<n;i++){ unsigned char c=(unsigned char)s[i]; o[i]=(c>=65&&c<=90)?(char)(c+32):(char)c; } o[n]=0; return o; }\n");
    v_out = scat(v_out, "static const char* trim(const char* s){ if(!s) return \"\"; const char* p=s; while(*p==32||*p==9||*p==10||*p==13) p++; const char* e=s+strlen(s); while(e>p){ char c=e[-1]; if(c!=32&&c!=9&&c!=10&&c!=13) break; e--; } size_t n=(size_t)(e-p); char* o=(char*)GC_MALLOC(n+1); memcpy(o,p,n); o[n]=0; return o; }\n");
    v_out = scat(v_out, "static const char* str_replace(const char* s, const char* a, const char* b){ if(!s||!a||!*a) return s?s:\"\"; if(!b) b=\"\"; size_t la=strlen(a),lb=strlen(b); size_t cnt=0; const char* p=s; while((p=strstr(p,a))!=0){ cnt++; p+=la; } size_t ls=strlen(s); size_t ol=ls+cnt*(lb>=la?lb-la:0)-cnt*(la>lb?la-lb:0); char* o=(char*)GC_MALLOC(ol+1); char* w=o; const char* r=s; while((p=strstr(r,a))!=0){ size_t bf=(size_t)(p-r); memcpy(w,r,bf); w+=bf; memcpy(w,b,lb); w+=lb; r=p+la; } size_t tl=strlen(r); memcpy(w,r,tl); w+=tl; *w=0; return o; }\n");
    v_out = scat(v_out, "static const char* repeat(const char* s, int64_t n){ if(!s||n<=0) return \"\"; size_t one=strlen(s); size_t tot=one*(size_t)n; char* o=(char*)GC_MALLOC(tot+1); for(int64_t i=0;i<n;i++) memcpy(o+(size_t)i*one,s,one); o[tot]=0; return o; }\n");
    v_out = scat(v_out, "static const char* pad_left(const char* s, int64_t w, const char* pad){ if(!s) s=\"\"; if(!pad||!*pad) pad=\" \"; size_t sl=strlen(s); if((int64_t)sl>=w){ char* o=(char*)GC_MALLOC(sl+1); memcpy(o,s,sl+1); return o; } size_t pl=strlen(pad); size_t need=(size_t)w-sl; char* o=(char*)GC_MALLOC((size_t)w+1); size_t i=0; while(i<need){ o[i]=pad[i%pl]; i++; } memcpy(o+need,s,sl); o[w]=0; return o; }\n");
    v_out = scat(v_out, "static const char* pad_right(const char* s, int64_t w, const char* pad){ if(!s) s=\"\"; if(!pad||!*pad) pad=\" \"; size_t sl=strlen(s); if((int64_t)sl>=w){ char* o=(char*)GC_MALLOC(sl+1); memcpy(o,s,sl+1); return o; } size_t pl=strlen(pad); char* o=(char*)GC_MALLOC((size_t)w+1); memcpy(o,s,sl); size_t i=sl; while(i<(size_t)w){ o[i]=pad[(i-sl)%pl]; i++; } o[w]=0; return o; }\n");
    v_out = scat(v_out, "static const char* chr(int64_t i){ char* o=(char*)GC_MALLOC(2); o[0]=(char)(i&255); o[1]=0; return o; }\n");
    v_out = scat(v_out, "static int64_t ord(const char* s){ return (s&&*s)?(int64_t)(unsigned char)s[0]:0; }\n");
    v_out = scat(v_out, "static int64_t str_to_bool(const char* s){ if(!s) return 0; if(strcmp(s,\"true\")==0) return 1; if(strcmp(s,\"True\")==0) return 1; if(strcmp(s,\"TRUE\")==0) return 1; if(strcmp(s,\"1\")==0) return 1; if(strcmp(s,\"yes\")==0) return 1; return 0; }\n");
    v_out = scat(v_out, "static int64_t abs_i64(int64_t n){ return n<0?-n:n; }\n");
    v_out = scat(v_out, "static double abs_f64(double x){ return x<0?-x:x; }\n");
    v_out = scat(v_out, "static int64_t sign(int64_t n){ return (n>0)-(n<0); }\n");
    v_out = scat(v_out, "static int64_t clamp(int64_t n, int64_t lo, int64_t hi){ if(n<lo) return lo; if(n>hi) return hi; return n; }\n");
    v_out = scat(v_out, "static const char* read_line(void){ size_t cap=128,len=0; char* b=(char*)GC_MALLOC(cap); int c; while((c=fgetc(stdin))!=EOF&&c!=10){ if(len+1>=cap){ size_t nc=cap*2; char* nb=(char*)GC_MALLOC(nc); memcpy(nb,b,len); b=nb; cap=nc; } b[len++]=(char)c; } b[len]=0; if(len==0&&c==EOF) return \"\"; return b; }\n");
    v_out = scat(v_out, "static const char* get_env(const char* name){ const char* v=name?getenv(name):0; return v?v:\"\"; }\n");
    v_out = scat(v_out, "static const char* format(const char* fmt, ...){ char b[1024]; va_list ap; va_start(ap,fmt); int n=vsnprintf(b,sizeof b,fmt?fmt:\"\",ap); va_end(ap); if(n<0) n=0; if(n>=(int)sizeof b) n=(int)sizeof b-1; char* o=(char*)GC_MALLOC((size_t)n+1); memcpy(o,b,(size_t)n+1); return o; }\n");
    if (v_needs_exedir) {
        v_out = scat(v_out, "static const char* exe_dir(void){ char buf[4096]; buf[0]=0;\n#ifdef _WIN32\n unsigned long wn=GetModuleFileNameA(0,buf,(unsigned long)sizeof buf); if(wn==0||wn>=sizeof buf) return \"\";\n#elif defined(__APPLE__)\n unsigned int sz=(unsigned int)sizeof buf; if(_NSGetExecutablePath(buf,&sz)!=0) return \"\";\n#else\n long rn=readlink(\"/proc/self/exe\",buf,sizeof buf-1); if(rn<=0) return \"\"; buf[(size_t)rn]=0;\n#endif\n int i=(int)strlen(buf)-1; while(i>=0 && buf[i]!='/' && buf[i]!=92) i--; if(i<0) return \"\"; char* d=(char*)GC_MALLOC((size_t)i+1); memcpy(d,buf,(size_t)i); d[i]=0; return d; }\n");
    }
    if (v_needs_time) {
        v_out = scat(v_out, "static int64_t now_ms(void){ struct timespec ts; if(clock_gettime(CLOCK_REALTIME,&ts)!=0) return 0; return (int64_t)ts.tv_sec*1000+(int64_t)(ts.tv_nsec/1000000); }\n");
        v_out = scat(v_out, "static int64_t now_us(void){ struct timespec ts; if(clock_gettime(CLOCK_REALTIME,&ts)!=0) return 0; return (int64_t)ts.tv_sec*1000000+(int64_t)(ts.tv_nsec/1000); }\n");
        v_out = scat(v_out, "static int64_t mono_ms(void){ struct timespec ts; if(clock_gettime(CLOCK_MONOTONIC,&ts)!=0) return 0; return (int64_t)ts.tv_sec*1000+(int64_t)(ts.tv_nsec/1000000); }\n");
        v_out = scat(v_out, "static void sleep_ms(int64_t ms){ if(ms<=0) return; struct timespec ts; ts.tv_sec=(time_t)(ms/1000); ts.tv_nsec=(long)((ms%1000)*1000000); while(nanosleep(&ts,&ts)==-1){} }\n");
        v_out = scat(v_out, "static const char* time_iso(int64_t ms){ time_t sec=(time_t)(ms/1000); struct tm tmv;\n#ifdef _WIN32\n if(gmtime_s(&tmv,&sec)!=0) return \"\";\n#else\n if(!gmtime_r(&sec,&tmv)) return \"\";\n#endif\n char* buf=(char*)GC_MALLOC(32); size_t n=strftime(buf,32,\"%Y-%m-%dT%H:%M:%SZ\",&tmv); if(n==0){ buf[0]=0; } return buf; }\n");
    }
    if (v_needs_sock) {
        v_out = scat(v_out, "#ifndef _WIN32\n");
        v_out = scat(v_out, "static int64_t tcp_listen(const char* host, int64_t port){ if(!host||port<=0||port>65535) return -1; int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1; int yes=1; setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)); struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)port); if(host[0]==0||strcmp(host,\"0.0.0.0\")==0){ addr.sin_addr.s_addr=htonl(INADDR_ANY); } else if(inet_pton(AF_INET,host,&addr.sin_addr)!=1){ close(fd); return -1; } if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))<0){ close(fd); return -1; } if(listen(fd,128)<0){ close(fd); return -1; } return (int64_t)fd; }\n");
        v_out = scat(v_out, "static int64_t tcp_accept(int64_t fd){ struct sockaddr_in addr; socklen_t len=sizeof(addr); int client=accept((int)fd,(struct sockaddr*)&addr,&len); if(client<0) return -1; return (int64_t)client; }\n");
        v_out = scat(v_out, "static int64_t tcp_connect(const char* host, int64_t port){ if(!host||port<=0||port>65535) return -1; int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0) return -1; struct sockaddr_in addr; memset(&addr,0,sizeof(addr)); addr.sin_family=AF_INET; addr.sin_port=htons((uint16_t)port); if(inet_pton(AF_INET,host,&addr.sin_addr)!=1){ struct addrinfo hints; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; struct addrinfo* res=0; if(getaddrinfo(host,0,&hints,&res)!=0||!res){ close(fd); return -1; } addr.sin_addr=((struct sockaddr_in*)res->ai_addr)->sin_addr; freeaddrinfo(res); } if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0){ close(fd); return -1; } return (int64_t)fd; }\n");
        v_out = scat(v_out, "static int64_t sock_send(int64_t fd, ailang_bytes b){ if(fd<0) return -1; if(b.len==0) return 0; ssize_t n=send((int)fd,b.data,(size_t)b.len,0); return (int64_t)n; }\n");
        v_out = scat(v_out, "static int64_t sock_send_str(int64_t fd, const char* s){ if(fd<0||!s) return -1; size_t len=strlen(s); if(len==0) return 0; ssize_t n=send((int)fd,s,len,0); return (int64_t)n; }\n");
        v_out = scat(v_out, "static ailang_bytes sock_recv(int64_t fd, int64_t max){ ailang_bytes r; r.len=0; r.data=(const uint8_t*)\"\"; if(fd<0||max<=0) return r; uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)max); ssize_t n=recv((int)fd,buf,(size_t)max,0); if(n<=0) return r; r.len=(int64_t)n; r.data=buf; return r; }\n");
        v_out = scat(v_out, "static int64_t sock_close(int64_t fd){ if(fd<0) return 0; return (int64_t)close((int)fd); }\n");
        v_out = scat(v_out, "#endif\n");
    }
    if (v_needs_proc) {
        v_out = scat(v_out, "#ifndef _WIN32\n");
        v_out = scat(v_out, "static int64_t proc_fork(void){ pid_t p=fork(); return (int64_t)p; }\n");
        v_out = scat(v_out, "static int64_t proc_getpid(void){ return (int64_t)getpid(); }\n");
        v_out = scat(v_out, "static void proc_no_zombies(void){ struct sigaction sa; memset(&sa,0,sizeof(sa)); sa.sa_handler=SIG_IGN; sigemptyset(&sa.sa_mask); sa.sa_flags=SA_NOCLDWAIT; sigaction(SIGCHLD,&sa,0); }\n");
        v_out = scat(v_out, "static int64_t proc_reap(void){ int64_t n=0; while(waitpid(-1,0,WNOHANG)>0) n++; return n; }\n");
        v_out = scat(v_out, "#endif\n");
    }
    if (v_needs_regex) {
        v_out = scat(v_out, "#ifndef _WIN32\n");
        v_out = scat(v_out, "static int64_t regex_match(const char* pat, const char* text){ if(!pat||!text) return 0; regex_t re; if(regcomp(&re,pat,REG_EXTENDED|REG_NOSUB)!=0) return 0; int rc=regexec(&re,text,0,0,0); regfree(&re); return rc==0; }\n");
        v_out = scat(v_out, "static const char* regex_find(const char* pat, const char* text){ if(!pat||!text) return \"\"; regex_t re; if(regcomp(&re,pat,REG_EXTENDED)!=0) return \"\"; regmatch_t m; int rc=regexec(&re,text,1,&m,0); regfree(&re); if(rc!=0||m.rm_so<0) return \"\"; size_t len=(size_t)(m.rm_eo-m.rm_so); char* o=(char*)GC_MALLOC(len+1); memcpy(o,text+m.rm_so,len); o[len]=0; return o; }\n");
        v_out = scat(v_out, "#endif\n");
    }
    if (v_needs_tls) {
        v_out = scat(v_out, "#ifndef _WIN32\n");
        v_out = scat(v_out, "static int ailang_tls_init_done_=0;\n");
        v_out = scat(v_out, "static void ailang_tls_init_(void){ if(ailang_tls_init_done_) return; SSL_library_init(); SSL_load_error_strings(); OpenSSL_add_ssl_algorithms(); ailang_tls_init_done_=1; }\n");
        v_out = scat(v_out, "static int64_t tls_server_ctx(const char* cert, const char* key){ ailang_tls_init_(); SSL_CTX* ctx=SSL_CTX_new(TLS_server_method()); if(!ctx) return -1; if(!cert||SSL_CTX_use_certificate_file(ctx,cert,SSL_FILETYPE_PEM)<=0){ SSL_CTX_free(ctx); return -1; } if(!key||SSL_CTX_use_PrivateKey_file(ctx,key,SSL_FILETYPE_PEM)<=0){ SSL_CTX_free(ctx); return -1; } return (int64_t)(intptr_t)ctx; }\n");
        v_out = scat(v_out, "static int64_t tls_client_ctx(void){ ailang_tls_init_(); SSL_CTX* ctx=SSL_CTX_new(TLS_client_method()); if(!ctx) return -1; return (int64_t)(intptr_t)ctx; }\n");
        v_out = scat(v_out, "static void tls_free_ctx(int64_t ctx){ if(ctx>0) SSL_CTX_free((SSL_CTX*)(intptr_t)ctx); }\n");
        v_out = scat(v_out, "static int64_t tls_accept(int64_t ctx, int64_t fd){ if(ctx<=0||fd<0) return -1; SSL* ssl=SSL_new((SSL_CTX*)(intptr_t)ctx); if(!ssl) return -1; SSL_set_fd(ssl,(int)fd); if(SSL_accept(ssl)<=0){ SSL_free(ssl); return -1; } return (int64_t)(intptr_t)ssl; }\n");
        v_out = scat(v_out, "static int64_t tls_connect_fd(int64_t ctx, int64_t fd){ if(ctx<=0||fd<0) return -1; SSL* ssl=SSL_new((SSL_CTX*)(intptr_t)ctx); if(!ssl) return -1; SSL_set_fd(ssl,(int)fd); if(SSL_connect(ssl)<=0){ SSL_free(ssl); return -1; } return (int64_t)(intptr_t)ssl; }\n");
        v_out = scat(v_out, "static int64_t tls_send(int64_t ssl, ailang_bytes b){ if(ssl<=0) return -1; if(b.len==0) return 0; int n=SSL_write((SSL*)(intptr_t)ssl,b.data,(int)b.len); return (int64_t)n; }\n");
        v_out = scat(v_out, "static int64_t tls_send_str(int64_t ssl, const char* s){ if(ssl<=0||!s) return -1; size_t len=strlen(s); if(len==0) return 0; int n=SSL_write((SSL*)(intptr_t)ssl,s,(int)len); return (int64_t)n; }\n");
        v_out = scat(v_out, "static ailang_bytes tls_recv(int64_t ssl, int64_t max){ ailang_bytes r; r.len=0; r.data=(const uint8_t*)\"\"; if(ssl<=0||max<=0) return r; uint8_t* buf=(uint8_t*)GC_MALLOC((size_t)max); int n=SSL_read((SSL*)(intptr_t)ssl,buf,(int)max); if(n<=0) return r; r.len=(int64_t)n; r.data=buf; return r; }\n");
        v_out = scat(v_out, "static void tls_close(int64_t ssl){ if(ssl>0){ SSL_shutdown((SSL*)(intptr_t)ssl); SSL_free((SSL*)(intptr_t)ssl); } }\n");
        v_out = scat(v_out, "static const char* tls_error(void){ unsigned long e=ERR_peek_error(); if(e==0) return \"\"; char* buf=(char*)GC_MALLOC(256); ERR_error_string_n(e,buf,256); return buf; }\n");
        v_out = scat(v_out, "static ailang_bytes sha1(const char* s){ ailang_bytes r; uint8_t* buf=(uint8_t*)GC_MALLOC(20); SHA1((const unsigned char*)(s?s:\"\"), s?strlen(s):0, buf); r.len=20; r.data=buf; return r; }\n");
        v_out = scat(v_out, "#endif\n");
    }
    if (v_needs_pg) {
        v_out = scat(v_out, "#ifndef _WIN32\n");
        v_out = scat(v_out, "static int64_t pg_connect(const char* conninfo){ PGconn* c=PQconnectdb(conninfo?conninfo:\"\"); return (int64_t)(intptr_t)c; }\n");
        v_out = scat(v_out, "static int64_t pg_status(int64_t conn){ if(conn==0) return -1; return (int64_t)PQstatus((PGconn*)(intptr_t)conn); }\n");
        v_out = scat(v_out, "static const char* pg_error(int64_t conn){ if(conn==0) return \"(null connection)\"; const char* msg=PQerrorMessage((PGconn*)(intptr_t)conn); return msg?msg:\"\"; }\n");
        v_out = scat(v_out, "static void pg_close(int64_t conn){ if(conn!=0) PQfinish((PGconn*)(intptr_t)conn); }\n");
        v_out = scat(v_out, "static int64_t pg_exec(int64_t conn, const char* sql){ if(conn==0||!sql) return 0; PGresult* r=PQexec((PGconn*)(intptr_t)conn,sql); return (int64_t)(intptr_t)r; }\n");
        v_out = scat(v_out, "static int64_t pg_ok(int64_t res){ if(res==0) return 0; int s=PQresultStatus((PGresult*)(intptr_t)res); return s==PGRES_COMMAND_OK||s==PGRES_TUPLES_OK; }\n");
        v_out = scat(v_out, "static const char* pg_result_error(int64_t res){ if(res==0) return \"(null result)\"; const char* m=PQresultErrorMessage((PGresult*)(intptr_t)res); return m?m:\"\"; }\n");
        v_out = scat(v_out, "static void pg_clear(int64_t res){ if(res!=0) PQclear((PGresult*)(intptr_t)res); }\n");
        v_out = scat(v_out, "static int64_t pg_nrows(int64_t res){ if(res==0) return 0; return (int64_t)PQntuples((PGresult*)(intptr_t)res); }\n");
        v_out = scat(v_out, "static int64_t pg_ncols(int64_t res){ if(res==0) return 0; return (int64_t)PQnfields((PGresult*)(intptr_t)res); }\n");
        v_out = scat(v_out, "static const char* pg_value(int64_t res, int64_t row, int64_t col){ if(res==0) return \"\"; const char* v=PQgetvalue((PGresult*)(intptr_t)res,(int)row,(int)col); if(!v) return \"\"; size_t n=strlen(v); char* o=(char*)GC_MALLOC(n+1); memcpy(o,v,n+1); return o; }\n");
        v_out = scat(v_out, "static int64_t pg_isnull(int64_t res, int64_t row, int64_t col){ if(res==0) return 0; return PQgetisnull((PGresult*)(intptr_t)res,(int)row,(int)col)!=0; }\n");
        v_out = scat(v_out, "static const char* pg_col_name(int64_t res, int64_t col){ if(res==0) return \"\"; const char* nm=PQfname((PGresult*)(intptr_t)res,(int)col); if(!nm) return \"\"; size_t l=strlen(nm); char* o=(char*)GC_MALLOC(l+1); memcpy(o,nm,l+1); return o; }\n");
        v_out = scat(v_out, "static int64_t pg_affected(int64_t res){ if(res==0) return 0; const char* s=PQcmdTuples((PGresult*)(intptr_t)res); if(!s||!*s) return 0; return (int64_t)atoll(s); }\n");
        v_out = scat(v_out, "static const char* pg_escape(int64_t conn, const char* s){ if(conn==0||!s) return \"''\"; char* esc=PQescapeLiteral((PGconn*)(intptr_t)conn,s,strlen(s)); if(!esc) return \"''\"; size_t n=strlen(esc); char* o=(char*)GC_MALLOC(n+1); memcpy(o,esc,n+1); PQfreemem(esc); return o; }\n");
        v_out = scat(v_out, "#endif\n");
    }
    v_out = scat(v_out, "static int g_argc=0; static char** g_argv=0;\n");
    if (((arr_Func_len((v_base).lams) > 0) || v_needs_thread)) {
        v_out = scat(v_out, "typedef struct { void* fn; void* env; } closure_t;\n");
    }
    if (v_needs_thread) {
        v_out = scat(v_out, "#ifndef _WIN32\n");
        v_out = scat(v_out, "typedef struct { pthread_t th; closure_t clo; } ail_thread_t;\n");
        v_out = scat(v_out, "static void* ail_thread_tramp(void* p){ closure_t* c=(closure_t*)p; return (void*)(intptr_t)((int64_t(*)(void*))c->fn)(c->env); }\n");
        v_out = scat(v_out, "static int64_t thread_spawn(closure_t clo){ ail_thread_t* t=(ail_thread_t*)GC_MALLOC(sizeof(ail_thread_t)); t->clo=clo; if(pthread_create(&t->th,0,ail_thread_tramp,&t->clo)!=0) return 0; return (int64_t)(intptr_t)t; }\n");
        v_out = scat(v_out, "static int64_t thread_join(int64_t h){ if(h==0) return 0; ail_thread_t* t=(ail_thread_t*)(intptr_t)h; void* rv=0; pthread_join(t->th,&rv); return (int64_t)(intptr_t)rv; }\n");
        v_out = scat(v_out, "static int64_t mutex_new(void){ pthread_mutex_t* m=(pthread_mutex_t*)GC_MALLOC(sizeof(pthread_mutex_t)); pthread_mutex_init(m,0); return (int64_t)(intptr_t)m; }\n");
        v_out = scat(v_out, "static int64_t mutex_lock(int64_t h){ if(h) pthread_mutex_lock((pthread_mutex_t*)(intptr_t)h); return 0; }\n");
        v_out = scat(v_out, "static int64_t mutex_unlock(int64_t h){ if(h) pthread_mutex_unlock((pthread_mutex_t*)(intptr_t)h); return 0; }\n");
        v_out = scat(v_out, "typedef struct { pthread_mutex_t m; pthread_cond_t ne, nf; int64_t* buf; int64_t cap, head, tail, cnt; int closed; } ail_chan_t;\n");
        v_out = scat(v_out, "static int64_t chan_new(int64_t cap){ if(cap<1) cap=1; ail_chan_t* c=(ail_chan_t*)GC_MALLOC(sizeof(ail_chan_t)); pthread_mutex_init(&c->m,0); pthread_cond_init(&c->ne,0); pthread_cond_init(&c->nf,0); c->buf=(int64_t*)GC_MALLOC(sizeof(int64_t)*(size_t)cap); c->cap=cap; c->head=0; c->tail=0; c->cnt=0; c->closed=0; return (int64_t)(intptr_t)c; }\n");
        v_out = scat(v_out, "static int64_t chan_send(int64_t h, int64_t v){ ail_chan_t* c=(ail_chan_t*)(intptr_t)h; if(!c) return 0; pthread_mutex_lock(&c->m); while(c->cnt==c->cap && !c->closed) pthread_cond_wait(&c->nf,&c->m); if(c->closed){ pthread_mutex_unlock(&c->m); return 0; } c->buf[c->tail]=v; c->tail=(c->tail+1)%c->cap; c->cnt++; pthread_cond_signal(&c->ne); pthread_mutex_unlock(&c->m); return 1; }\n");
        v_out = scat(v_out, "static int64_t chan_recv(int64_t h){ ail_chan_t* c=(ail_chan_t*)(intptr_t)h; if(!c) return 0; pthread_mutex_lock(&c->m); while(c->cnt==0 && !c->closed) pthread_cond_wait(&c->ne,&c->m); if(c->cnt==0 && c->closed){ pthread_mutex_unlock(&c->m); return 0; } int64_t v=c->buf[c->head]; c->head=(c->head+1)%c->cap; c->cnt--; pthread_cond_signal(&c->nf); pthread_mutex_unlock(&c->m); return v; }\n");
        v_out = scat(v_out, "static int64_t chan_close(int64_t h){ ail_chan_t* c=(ail_chan_t*)(intptr_t)h; if(!c) return 0; pthread_mutex_lock(&c->m); c->closed=1; pthread_cond_broadcast(&c->ne); pthread_cond_broadcast(&c->nf); pthread_mutex_unlock(&c->m); return 0; }\n");
        v_out = scat(v_out, "#endif\n");
    }
    v_out = scat(v_out, "\n");
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_struct_fwd(arr_StructDef_get(v_structs, v_si)));
        v_si = (v_si + 1);
    }
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_out = scat(v_out, f_gen_enum_fwd(arr_EnumDef_get(v_enums, v_ei)));
        v_ei = (v_ei + 1);
    }
    v_out = scat(v_out, "\n");
    v_out = scat(v_out, f_gen_arr_typedef("i64", "int64_t"));
    v_out = scat(v_out, f_gen_arr_typedef("str", "const char*"));
    v_out = scat(v_out, f_gen_arr_typedef("f64", "double"));
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_arr_typedef((arr_StructDef_get(v_structs, v_si)).name, scat("s_", (arr_StructDef_get(v_structs, v_si)).name)));
        v_si = (v_si + 1);
    }
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_out = scat(v_out, f_gen_arr_typedef((arr_EnumDef_get(v_enums, v_ei)).name, scat("s_", (arr_EnumDef_get(v_enums, v_ei)).name)));
        v_ei = (v_ei + 1);
    }
    v_mapkeys = ({ arr_str __a = arr_str_new(); __a = arr_str_push(__a, "str"); __a = arr_str_push(__a, "i64"); __a = arr_str_push(__a, "f64"); __a; });
    v_mapvals = ({ arr_str __a = arr_str_new(); __a = arr_str_push(__a, "i64"); __a = arr_str_push(__a, "str"); __a = arr_str_push(__a, "f64"); __a; });
    v_mvs = 0;
    while ((v_mvs < arr_StructDef_len(v_structs))) {
        v_mapvals = arr_str_push(v_mapvals, (arr_StructDef_get(v_structs, v_mvs)).name);
        v_mvs = (v_mvs + 1);
    }
    v_mve = 0;
    while ((v_mve < arr_EnumDef_len(v_enums))) {
        v_mapvals = arr_str_push(v_mapvals, (arr_EnumDef_get(v_enums, v_mve)).name);
        v_mve = (v_mve + 1);
    }
    v_out = scat(v_out, f_gen_map_fwds_all(v_mapkeys, v_mapvals));
    v_out = scat(v_out, "\n");
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_struct_body(arr_StructDef_get(v_structs, v_si)));
        v_si = (v_si + 1);
    }
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_out = scat(v_out, f_gen_enum_body(arr_EnumDef_get(v_enums, v_ei)));
        v_ei = (v_ei + 1);
    }
    v_out = scat(v_out, f_gen_map_nodes_all(v_mapkeys, v_mapvals));
    v_out = scat(v_out, "\n");
    v_vt1 = 0;
    while ((v_vt1 < arr_ClassDef_len(v_classes))) {
        if (map_str_str_has((v_base).evar, scat("@class.hasvt.", (arr_ClassDef_get(v_classes, v_vt1)).name))) {
            v_out = scat(v_out, f_gen_vtable_typedef((&v_base), (arr_ClassDef_get(v_classes, v_vt1)).name));
            v_out = scat(scat(scat(scat(scat(v_out, "extern const vt_"), (arr_ClassDef_get(v_classes, v_vt1)).name), " __vtbl_"), (arr_ClassDef_get(v_classes, v_vt1)).name), ";\n");
        }
        v_vt1 = (v_vt1 + 1);
    }
    v_out = scat(v_out, f_gen_arr_helpers("i64", "int64_t"));
    v_out = scat(v_out, f_gen_arr_helpers("str", "const char*"));
    v_out = scat(v_out, f_gen_arr_helpers("f64", "double"));
    v_out = scat(v_out, "static void print_arr_i64(arr_i64 a){ printf(\"[\"); for(int64_t i=0;i<a.len;i++){ if(i>0) printf(\", \"); printf(\"%lld\",(long long)a.data[i]); } printf(\"]\"); }\n");
    v_out = scat(v_out, "static void print_arr_str(arr_str a){ printf(\"[\"); for(int64_t i=0;i<a.len;i++){ if(i>0) printf(\", \"); putchar(34); printf(\"%s\", a.data[i] ? a.data[i] : \"\"); putchar(34); } printf(\"]\"); }\n");
    v_out = scat(v_out, "static arr_str ailang_args(void){ arr_str a = arr_str_new(); for(int i=1;i<g_argc;i++) a = arr_str_push(a, g_argv[i]); return a; }\n");
    v_out = scat(v_out, "static arr_str split(const char* s, const char* sep){ arr_str a=arr_str_new(); if(!s) s=\"\"; if(!sep||!*sep){ return arr_str_push(a,s); } size_t ls=strlen(sep); const char* r=s; const char* p; while((p=strstr(r,sep))!=0){ size_t n=(size_t)(p-r); char* pc=(char*)GC_MALLOC(n+1); memcpy(pc,r,n); pc[n]=0; a=arr_str_push(a,pc); r=p+ls; } size_t n=strlen(r); char* pc=(char*)GC_MALLOC(n+1); memcpy(pc,r,n+1); a=arr_str_push(a,pc); return a; }\n");
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_arr_helpers((arr_StructDef_get(v_structs, v_si)).name, scat("s_", (arr_StructDef_get(v_structs, v_si)).name)));
        v_si = (v_si + 1);
    }
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_out = scat(v_out, f_gen_arr_helpers((arr_EnumDef_get(v_enums, v_ei)).name, scat("s_", (arr_EnumDef_get(v_enums, v_ei)).name)));
        v_ei = (v_ei + 1);
    }
    v_out = scat(v_out, f_gen_map_helpers_all(v_mapkeys, v_mapvals));
    v_out = scat(v_out, f_gen_res("i64", "int64_t"));
    v_out = scat(v_out, f_gen_res("str", "const char*"));
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_res((arr_StructDef_get(v_structs, v_si)).name, scat("s_", (arr_StructDef_get(v_structs, v_si)).name)));
        v_si = (v_si + 1);
    }
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_out = scat(v_out, f_gen_res((arr_EnumDef_get(v_enums, v_ei)).name, scat("s_", (arr_EnumDef_get(v_enums, v_ei)).name)));
        v_ei = (v_ei + 1);
    }
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_struct_ctor(arr_StructDef_get(v_structs, v_si)));
        v_si = (v_si + 1);
    }
    v_si = 0;
    while ((v_si < arr_StructDef_len(v_structs))) {
        v_out = scat(v_out, f_gen_struct_printer(arr_StructDef_get(v_structs, v_si)));
        v_si = (v_si + 1);
    }
    v_ei = 0;
    while ((v_ei < arr_EnumDef_len(v_enums))) {
        v_out = scat(v_out, f_gen_enum_ctors(arr_EnumDef_get(v_enums, v_ei)));
        v_ei = (v_ei + 1);
    }
    v_out = scat(v_out, "\n");
    v_tup_seen = ({ arr_str __a = arr_str_new(); __a; });
    v_ti = 0;
    while ((v_ti < arr_Func_len(v_funcs))) {
        v_rt2 = map_str_str_get((v_base).frets, (arr_Func_get(v_funcs, v_ti)).name);
        if (f_is_tuple_ann(v_rt2)) {
            v_suf = f_tuple_suffix(v_rt2);
            v_found = (1 != 1);
            v_tk = 0;
            while ((v_tk < arr_str_len(v_tup_seen))) {
                if ((strcmp(arr_str_get(v_tup_seen, v_tk), v_suf) == 0)) {
                    v_found = (1 == 1);
                }
                v_tk = (v_tk + 1);
            }
            if ((v_found == (1 != 1))) {
                v_tup_seen = arr_str_push(v_tup_seen, v_suf);
                v_out = scat(v_out, f_gen_tuple_typedef(v_rt2));
            }
        }
        v_ti = (v_ti + 1);
    }
    v_xj = 0;
    while ((v_xj < arr_Func_len(v_externs))) {
        v_out = scat(v_out, f_gen_extern(arr_Func_get(v_externs, v_xj)));
        v_xj = (v_xj + 1);
    }
    if ((arr_Func_len(v_externs) > 0)) {
        v_out = scat(v_out, "\n");
    }
    v_i = 0;
    while ((v_i < arr_Func_len(v_funcs))) {
        v_out = scat(scat(v_out, f_gen_sig(arr_Func_get(v_funcs, v_i), map_str_str_get((v_base).frets, (arr_Func_get(v_funcs, v_i)).name))), ";\n");
        v_i = (v_i + 1);
    }
    v_out = scat(v_out, "\n");
    v_vt2 = 0;
    while ((v_vt2 < arr_ClassDef_len(v_classes))) {
        if (map_str_str_has((v_base).evar, scat("@class.hasvt.", (arr_ClassDef_get(v_classes, v_vt2)).name))) {
            v_out = scat(v_out, f_gen_vtable_instance((&v_base), (arr_ClassDef_get(v_classes, v_vt2)).name));
        }
        v_vt2 = (v_vt2 + 1);
    }
    if ((arr_ClassDef_len(v_classes) > 0)) {
        v_out = scat(v_out, "\n");
    }
    if ((arr_Func_len((v_base).lams) > 0)) {
        v_lm = 0;
        while ((v_lm < arr_Func_len((v_base).lams))) {
            v_out = scat(v_out, f_gen_env_struct((&v_base), arr_Func_get((v_base).lams, v_lm)));
            v_lm = (v_lm + 1);
        }
        v_lm = 0;
        while ((v_lm < arr_Func_len((v_base).lams))) {
            v_out = scat(scat(v_out, f_gen_lam_sig(arr_Func_get((v_base).lams, v_lm))), ";\n");
            v_lm = (v_lm + 1);
        }
        v_lm = 0;
        while ((v_lm < arr_Func_len((v_base).lams))) {
            v_out = scat(v_out, f_gen_lifted_body((&v_base), arr_Func_get((v_base).lams, v_lm)));
            v_lm = (v_lm + 1);
        }
        v_out = scat(v_out, "\n");
    }
    v_i = 0;
    while ((v_i < arr_Func_len(v_funcs))) {
        v_f = arr_Func_get(v_funcs, v_i);
        v_sy = f_seed_fn((&v_base), v_f);
        v_fb = f_stamp_empty_maps((&v_sy), (v_f).body);
        v_sy = f_seed_fn((&v_base), v_f);
        v_decls = f_hoist_decls((&v_sy), v_fb, "    ");
        if (map_str_str_has((v_sy).evar, scat("@mclass.", (v_f).name))) {
            map_str_str_set((v_sy).evar, "@curclass", map_str_str_get((v_sy).evar, scat("@mclass.", (v_f).name)));
        } else {
            map_str_str_set((v_sy).evar, "@curclass", "");
        }
        v_out = scat(scat(scat(scat(scat(v_out, f_gen_sig(v_f, map_str_str_get((v_base).frets, (v_f).name))), " {\n"), v_decls), f_gen_fn_body((&v_sy), v_fb, map_str_str_get((v_base).frets, (v_f).name), "    ")), "}\n\n");
        v_i = (v_i + 1);
    }
    v_msy = mk_Syms(map_str_str_new(), (v_base).fld, (v_base).ctors, (v_base).frets, (v_base).evar, (v_base).vft, (v_base).gfns, (v_base).lams);
    v_mmains = f_stamp_empty_maps((&v_msy), v_mains);
    v_msy = mk_Syms(map_str_str_new(), (v_base).fld, (v_base).ctors, (v_base).frets, (v_base).evar, (v_base).vft, (v_base).gfns, (v_base).lams);
    v_mdecls = f_hoist_decls((&v_msy), v_mmains, "    ");
    v_maincall = "";
    v_mk = 0;
    while ((v_mk < arr_Func_len(v_funcs))) {
        if (((strcmp((arr_Func_get(v_funcs, v_mk)).name, "main") == 0) && (arr_str_len((arr_Func_get(v_funcs, v_mk)).params) == 0))) {
            v_maincall = "    f_main();\n";
        }
        v_mk = (v_mk + 1);
    }
    v_out = scat(scat(scat(scat(scat(v_out, "int main(int argc, char** argv){\n    GC_INIT();\n    g_argc=argc; g_argv=argv;\n"), v_mdecls), f_gen_stmts((&v_msy), v_mmains, "    ")), v_maincall), "    return 0;\n}\n");
    return v_out;
}

const char* f_link_flags(const char* v_cprog) {
    const char* v_pre;
    int64_t v_np;
    int64_t v_e;
    const char* v_libs;
    const char* v_out;
    int64_t v_start;
    int64_t v_i;
    v_pre = "// @links:";
    v_np = ((int64_t)strlen(v_pre));
    if ((((int64_t)strlen(v_cprog)) < v_np)) {
        return "";
    }
    if ((strcmp(substr(v_cprog, 0, v_np), v_pre) != 0)) {
        return "";
    }
    v_e = v_np;
    while ((v_e < ((int64_t)strlen(v_cprog)))) {
        if ((((int64_t)(unsigned char)(v_cprog)[v_e]) == 10)) {
            break;
        }
        v_e = (v_e + 1);
    }
    v_libs = substr(v_cprog, v_np, v_e);
    v_out = "";
    v_start = (0 - 1);
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_libs)))) {
        if ((((int64_t)(unsigned char)(v_libs)[v_i]) == 32)) {
            if ((v_start >= 0)) {
                v_out = scat(scat(v_out, " -l"), substr(v_libs, v_start, v_i));
                v_start = (0 - 1);
            }
        } else {
            if ((v_start < 0)) {
                v_start = v_i;
            }
        }
        v_i = (v_i + 1);
    }
    if ((v_start >= 0)) {
        v_out = scat(scat(v_out, " -l"), substr(v_libs, v_start, ((int64_t)strlen(v_libs))));
    }
    return v_out;
}

arr_str f_csrc_list(const char* v_cprog) {
    const char* v_pre;
    int64_t v_np;
    arr_str v_out;
    int64_t v_ls;
    int64_t v_i;
    const char* v_seg;
    int64_t v_start;
    int64_t v_j;
    v_pre = "// @csrc:";
    v_np = ((int64_t)strlen(v_pre));
    v_out = ({ arr_str __a = arr_str_new(); __a; });
    v_ls = 0;
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_cprog)))) {
        if ((((int64_t)(unsigned char)(v_cprog)[v_i]) == 10)) {
            if ((((v_i - v_ls) >= v_np) && (strcmp(substr(v_cprog, v_ls, (v_ls + v_np)), v_pre) == 0))) {
                v_seg = substr(v_cprog, (v_ls + v_np), v_i);
                v_start = (0 - 1);
                v_j = 0;
                while ((v_j < ((int64_t)strlen(v_seg)))) {
                    if ((((int64_t)(unsigned char)(v_seg)[v_j]) == 32)) {
                        if ((v_start >= 0)) {
                            v_out = arr_str_push(v_out, substr(v_seg, v_start, v_j));
                            v_start = (0 - 1);
                        }
                    } else {
                        if ((v_start < 0)) {
                            v_start = v_j;
                        }
                    }
                    v_j = (v_j + 1);
                }
                if ((v_start >= 0)) {
                    v_out = arr_str_push(v_out, substr(v_seg, v_start, ((int64_t)strlen(v_seg))));
                }
                return v_out;
            }
            v_ls = (v_i + 1);
        }
        v_i = (v_i + 1);
    }
    return v_out;
}

const char* f_dirname(const char* v_path) {
    int64_t v_last;
    int64_t v_i;
    v_last = (0 - 1);
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_path)))) {
        if (((((int64_t)(unsigned char)(v_path)[v_i]) == 47) || (((int64_t)(unsigned char)(v_path)[v_i]) == 92))) {
            v_last = v_i;
        }
        v_i = (v_i + 1);
    }
    if ((v_last < 0)) {
        return "";
    }
    return substr(v_path, 0, (v_last + 1));
}

const char* f_import_path(const char* v_line) {
    int64_t v_i;
    int64_t v_j;
    int64_t v_k;
    v_i = 0;
    while ((v_i < ((int64_t)strlen(v_line)))) {
        if ((((int64_t)(unsigned char)(v_line)[v_i]) != 32)) {
            break;
        }
        v_i = (v_i + 1);
    }
    if (((v_i + 3) > ((int64_t)strlen(v_line)))) {
        return "";
    }
    if ((((int64_t)(unsigned char)(v_line)[v_i]) != 105)) {
        return "";
    }
    if ((((int64_t)(unsigned char)(v_line)[(v_i + 1)]) != 109)) {
        return "";
    }
    if ((((int64_t)(unsigned char)(v_line)[(v_i + 2)]) != 32)) {
        return "";
    }
    v_j = (v_i + 3);
    while ((v_j < ((int64_t)strlen(v_line)))) {
        if ((((int64_t)(unsigned char)(v_line)[v_j]) == 34)) {
            break;
        }
        v_j = (v_j + 1);
    }
    if ((v_j >= ((int64_t)strlen(v_line)))) {
        return "";
    }
    v_k = (v_j + 1);
    while ((v_k < ((int64_t)strlen(v_line)))) {
        if ((((int64_t)(unsigned char)(v_line)[v_k]) == 34)) {
            break;
        }
        v_k = (v_k + 1);
    }
    if ((v_k >= ((int64_t)strlen(v_line)))) {
        return "";
    }
    return substr(v_line, (v_j + 1), v_k);
}

int64_t f_has_import(const char* v_src) {
    int64_t v_i;
    int64_t v_atstart;
    int64_t v_c;
    v_i = 0;
    v_atstart = (1 == 1);
    while ((v_i < ((int64_t)strlen(v_src)))) {
        v_c = ((int64_t)(unsigned char)(v_src)[v_i]);
        if ((v_c == 10)) {
            v_atstart = (1 == 1);
            v_i = (v_i + 1);
            continue;
        }
        if ((v_atstart && (v_c == 32))) {
            v_i = (v_i + 1);
            continue;
        }
        if (v_atstart) {
            if (((((v_c == 105) && ((v_i + 2) < ((int64_t)strlen(v_src)))) && (((int64_t)(unsigned char)(v_src)[(v_i + 1)]) == 109)) && (((int64_t)(unsigned char)(v_src)[(v_i + 2)]) == 32))) {
                return (1 == 1);
            }
            v_atstart = (1 != 1);
        }
        v_i = (v_i + 1);
    }
    return (1 != 1);
}

int64_t f_is_math_export(const char* v_nm) {
    if ((((((strcmp(v_nm, "sqrt") == 0) || (strcmp(v_nm, "pow") == 0)) || (strcmp(v_nm, "sin") == 0)) || (strcmp(v_nm, "cos") == 0)) || (strcmp(v_nm, "tan") == 0))) {
        return (1 == 1);
    }
    if (((((strcmp(v_nm, "log") == 0) || (strcmp(v_nm, "log2") == 0)) || (strcmp(v_nm, "log10") == 0)) || (strcmp(v_nm, "exp") == 0))) {
        return (1 == 1);
    }
    if (((((strcmp(v_nm, "floor") == 0) || (strcmp(v_nm, "ceil") == 0)) || (strcmp(v_nm, "rand") == 0)) || (strcmp(v_nm, "srand") == 0))) {
        return (1 == 1);
    }
    if (((((strcmp(v_nm, "min") == 0) || (strcmp(v_nm, "max") == 0)) || (strcmp(v_nm, "ipow") == 0)) || (strcmp(v_nm, "gcd") == 0))) {
        return (1 == 1);
    }
    return (1 != 1);
}

const char* f_auto_imports(const char* v_src) {
    arr_Token v_toks;
    map_str_str v_defined;
    int64_t v_i;
    int64_t v_need_math;
    int64_t v_need_sock;
    const char* v_nm;
    const char* v_out;
    v_toks = f_lex(v_src);
    v_defined = map_str_str_new();
    v_i = 0;
    while (((v_i + 1) < arr_Token_len(v_toks))) {
        if (((((arr_Token_get(v_toks, v_i)).kind == f_TK_IDENT()) && (strcmp((arr_Token_get(v_toks, v_i)).text, "fn") == 0)) && ((arr_Token_get(v_toks, (v_i + 1))).kind == f_TK_IDENT()))) {
            map_str_str_set(v_defined, (arr_Token_get(v_toks, (v_i + 1))).text, "1");
        }
        v_i = (v_i + 1);
    }
    v_need_math = (1 != 1);
    v_need_sock = (1 != 1);
    v_i = 0;
    while ((v_i < arr_Token_len(v_toks))) {
        if ((((arr_Token_get(v_toks, v_i)).kind == f_TK_IDENT()) && (map_str_str_has(v_defined, (arr_Token_get(v_toks, v_i)).text) == (1 != 1)))) {
            v_nm = (arr_Token_get(v_toks, v_i)).text;
            if (f_is_math_export(v_nm)) {
                v_need_math = (1 == 1);
            }
            if ((strcmp(v_nm, "env_int") == 0)) {
                v_need_sock = (1 == 1);
            }
        }
        v_i = (v_i + 1);
    }
    v_out = "";
    if (v_need_math) {
        v_out = scat(v_out, "im \"std/math.ail\"\n");
    }
    if (v_need_sock) {
        v_out = scat(v_out, "im \"std/sock.ail\"\n");
    }
    return v_out;
}

const char* f_resolve_imports(const char* v_src, const char* v_dir, map_str_str v_seen) {
    const char* v_out;
    int64_t v_i;
    int64_t v_lstart;
    const char* v_line;
    const char* v_imp;
    const char* v_full;
    const char* v_body;
    const char* v_bdir;
    const char* v_root;
    const char* v_gfull;
    const char* v_gbody;
    const char* v_ed;
    const char* v_efull;
    const char* v_ebody;
    if ((f_has_import(v_src) == (1 != 1))) {
        return v_src;
    }
    v_out = "";
    v_i = 0;
    v_lstart = 0;
    while ((v_i <= ((int64_t)strlen(v_src)))) {
        if (((v_i == ((int64_t)strlen(v_src))) || (((int64_t)(unsigned char)(v_src)[v_i]) == 10))) {
            v_line = substr(v_src, v_lstart, v_i);
            v_imp = f_import_path(v_line);
            if ((((int64_t)strlen(v_imp)) > 0)) {
                v_full = scat(v_dir, v_imp);
                if ((map_str_str_has(v_seen, v_full) == (1 != 1))) {
                    map_str_str_set(v_seen, v_full, "1");
                    v_body = read_file_c(v_full);
                    v_bdir = f_dirname(v_full);
                    if ((((int64_t)strlen(v_body)) == 0)) {
                        v_root = get_env("AILANG_STD");
                        if ((((int64_t)strlen(v_root)) > 0)) {
                            v_gfull = scat(scat(v_root, "/"), v_imp);
                            v_gbody = read_file_c(v_gfull);
                            if ((((int64_t)strlen(v_gbody)) > 0)) {
                                v_body = v_gbody;
                                v_bdir = f_dirname(v_gfull);
                            }
                        }
                    }
                    if ((((int64_t)strlen(v_body)) == 0)) {
                        v_ed = exe_dir();
                        if ((((int64_t)strlen(v_ed)) > 0)) {
                            v_efull = scat(scat(v_ed, "/"), v_imp);
                            v_ebody = read_file_c(v_efull);
                            if ((((int64_t)strlen(v_ebody)) > 0)) {
                                v_body = v_ebody;
                                v_bdir = f_dirname(v_efull);
                            }
                        }
                    }
                    if ((((int64_t)strlen(v_body)) == 0)) {
                        printf("%s\n", scat(scat("error: cannot resolve import \"", v_imp), "\" — not found beside the source, in AILANG_STD, or next to ailc"));
                        exit((int)(1));
                    }
                    v_out = scat(scat(v_out, f_resolve_imports(v_body, v_bdir, v_seen)), "\n");
                }
            } else {
                v_out = scat(scat(v_out, v_line), "\n");
            }
            v_lstart = (v_i + 1);
        }
        v_i = (v_i + 1);
    }
    return v_out;
}

int main(int argc, char** argv){
    GC_INIT();
    g_argc=argc; g_argv=argv;
    arr_str v_av;
    int64_t v_keepc;
    arr_str v_pos;
    int64_t v_fi;
    const char* v_a;
    int64_t v_ai;
    const char* v_input;
    const char* v_outbin;
    int64_t v_ni;
    int64_t v_ob;
    const char* v_src;
    map_str_str v_seen;
    const char* v_cprog;
    const char* v_cpath;
    int64_t v_win;
    const char* v_outexe;
    arr_str v_shims0;
    arr_str v_shims;
    int64_t v_ri;
    int64_t v_rc;
    const char* v_cmd;
    const char* v_extra;
    const char* v_shimline;
    int64_t v_si;
    int64_t v_ci2;
    v_av = ailang_args();
    v_keepc = (1 != 1);
    v_pos = ({ arr_str __a = arr_str_new(); __a; });
    v_fi = 0;
    while ((v_fi < arr_str_len(v_av))) {
        v_a = arr_str_get(v_av, v_fi);
        if (((strcmp(v_a, "--keep-c") == 0) || (strcmp(v_a, "-k") == 0))) {
            v_keepc = (1 == 1);
        } else {
            v_pos = arr_str_push(v_pos, v_a);
        }
        v_fi = (v_fi + 1);
    }
    v_ai = 0;
    if (((arr_str_len(v_pos) > 0) && (strcmp(arr_str_get(v_pos, 0), "compile") == 0))) {
        v_ai = 1;
    }
    if ((arr_str_len(v_pos) <= v_ai)) {
        printf("%s\n", "usage: ailc [--keep-c] [compile] <input.ail> [output-binary]");
        exit((int)(1));
    }
    v_input = arr_str_get(v_pos, v_ai);
    v_outbin = "";
    if ((arr_str_len(v_pos) > (v_ai + 1))) {
        v_outbin = arr_str_get(v_pos, (v_ai + 1));
    } else {
        v_ni = ((int64_t)strlen(v_input));
        if (((v_ni >= 4) && (strcmp(substr(v_input, (v_ni - 4), v_ni), ".ail") == 0))) {
            v_outbin = substr(v_input, 0, (v_ni - 4));
        } else {
            v_outbin = scat(v_input, ".out");
        }
    }
    if ((strcmp(v_outbin, v_input) == 0)) {
        printf("%s\n", scat("error: output would overwrite the input file: ", v_input));
        exit((int)(1));
    }
    v_ob = ((int64_t)strlen(v_outbin));
    if (((v_ob >= 4) && (strcmp(substr(v_outbin, (v_ob - 4), v_ob), ".ail") == 0))) {
        printf("%s\n", scat("error: refusing to write the compiled binary over a .ail source file: ", v_outbin));
        exit((int)(1));
    }
    v_src = read_file_c(v_input);
    v_src = scat(f_auto_imports(v_src), v_src);
    if (f_has_import(v_src)) {
        v_seen = map_str_str_new();
        v_src = f_resolve_imports(v_src, f_dirname(v_input), v_seen);
    }
    v_cprog = f_compile_to_c(v_src, f_dirname(v_input));
    v_cpath = scat(v_outbin, ".c");
    write_file_c(v_cpath, v_cprog);
    v_win = (strcmp(get_env("OS"), "Windows_NT") == 0);
    v_outexe = v_outbin;
    v_shims0 = f_csrc_list(v_cprog);
    v_shims = ({ arr_str __a = arr_str_new(); __a; });
    v_ri = 0;
    while ((v_ri < arr_str_len(v_shims0))) {
        v_shims = arr_str_push(v_shims, scat(f_dirname(v_input), arr_str_get(v_shims0, v_ri)));
        v_ri = (v_ri + 1);
    }
    v_rc = 0;
    if (v_win) {
        v_outexe = scat(v_outbin, ".exe");
        if ((arr_str_len(v_shims) == 0)) {
            v_cmd = scat(scat(scat(scat(scat(scat("clang -O2 -DGC_NOT_DLL \"", v_cpath), "\""), f_link_flags(v_cprog)), " -static -lgc -lm -o \""), v_outexe), "\"");
            v_rc = f_system(v_cmd);
        } else {
            printf("%s\n", "error: C++ interop (csrc) is not supported on Windows in this release; build on macOS/Linux");
            exit((int)(1));
        }
    } else {
        v_extra = " $(pkg-config --cflags --libs bdw-gc 2>/dev/null || echo -lgc)";
        if ((f_has_sub(v_cprog, "SSL_") || f_has_sub(v_cprog, "SHA1("))) {
            v_extra = scat(v_extra, " $(pkg-config --cflags --libs openssl 2>/dev/null || echo -I$(brew --prefix openssl@3)/include -L$(brew --prefix openssl@3)/lib -lssl -lcrypto)");
        }
        if ((f_has_sub(v_cprog, "PQconnectdb") || f_has_sub(v_cprog, "PQexec"))) {
            v_extra = scat(v_extra, " $(pkg-config --cflags --libs libpq 2>/dev/null || echo -I$(brew --prefix libpq)/include -L$(brew --prefix libpq)/lib -lpq)");
        }
        if (f_has_sub(v_cprog, "pthread_")) {
            v_extra = scat(v_extra, " -lpthread");
        }
        v_extra = scat(scat(v_extra, f_link_flags(v_cprog)), " -lm");
        if ((arr_str_len(v_shims) == 0)) {
            v_cmd = scat(scat(scat(scat("clang -O2 ", v_cpath), v_extra), " -o "), v_outbin);
            v_rc = f_system(v_cmd);
        } else {
            v_shimline = "";
            v_si = 0;
            while ((v_si < arr_str_len(v_shims))) {
                v_shimline = scat(scat(v_shimline, " "), arr_str_get(v_shims, v_si));
                v_si = (v_si + 1);
            }
            v_cmd = scat(scat(scat(scat(scat(scat("clang++ -O2 -x c ", v_cpath), " -x c++"), v_shimline), v_extra), " -o "), v_outbin);
            v_rc = f_system(v_cmd);
        }
    }
    if ((v_rc != 0)) {
        printf("%s\n", "clang failed");
        exit((int)(1));
    }
    if ((v_keepc == (1 != 1))) {
        if (v_win) {
            f_system(scat(scat("del /f /q \"", v_cpath), "\""));
        } else {
            f_system(scat("rm -f ", v_cpath));
        }
        v_ci2 = 0;
        while ((v_ci2 < arr_str_len(v_shims))) {
            if (f_has_sub(arr_str_get(v_shims, v_ci2), "__ailinline_")) {
                f_system(scat("rm -f ", arr_str_get(v_shims, v_ci2)));
            }
            v_ci2 = (v_ci2 + 1);
        }
    }
    printf("%s\n", scat(scat(scat("compiled ", v_input), " -> "), v_outexe));
    return 0;
}
