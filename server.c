/*
 * Kids Shopping Website - Backend in C (no external libraries)
 * Build (Windows / MinGW): gcc server.c -o server.exe -lws2_32
 * Build (Linux / macOS)  : gcc server.c -o server
 * Run from the project folder, then open http://localhost:8080
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #define CLOSESOCK close
#endif

#define DEFAULT_PORT    8080
#define DEFAULT_ADMIN_PASSWORD "admin123"
#define DEFAULT_ADMIN_TOKEN    "kids-admin-token-2026"
#define PRODUCTS_FILE   "data/products.txt"
#define ORDERS_FILE     "data/orders.txt"
#define MAX_PRODUCTS    500
#define MAX_ORDERS      2000
#define REQ_MAX         16384

typedef struct {
    int id; char name[64]; char cat[32]; double price; int stock; char icon[16];
} Product;

typedef struct {
    int id; char name[64]; char phone[24]; char addr[160]; char items[1024];
    double total; char status[16]; char date[24];
} Order;

static const char *admin_pw = DEFAULT_ADMIN_PASSWORD;      /* override with env ADMIN_PASSWORD */
static const char *admin_token = DEFAULT_ADMIN_TOKEN;      /* override with env ADMIN_TOKEN */

static Product prods[MAX_PRODUCTS]; static int np = 0, next_pid = 1;
static Order   orders[MAX_ORDERS];  static int no = 0, next_oid = 1001;

/* ---------- small helpers ---------- */
typedef struct { char *d; size_t len, cap; } Buf;

static void buf_add(Buf *b, const char *fmt, ...) {
    char tmp[4096]; va_list ap;
    va_start(ap, fmt); int n = vsnprintf(tmp, sizeof tmp, fmt, ap); va_end(ap);
    if (n < 0) return;
    if (n >= (int)sizeof tmp) n = sizeof tmp - 1;
    if (b->len + n + 1 > b->cap) { b->cap = (b->len + n + 1) * 2; b->d = realloc(b->d, b->cap); }
    memcpy(b->d + b->len, tmp, n); b->len += n; b->d[b->len] = 0;
}

static void clean(char *s) {            /* remove characters that would break the data files */
    for (; *s; s++) if (*s == '|' || *s == '\r' || *s == '\n') *s = ' ';
}
static void trim(char *s) {
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = 0;
}
static void json_escape(const char *s, char *out, size_t cap) {
    size_t k = 0;
    for (; *s && k + 7 < cap; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') { out[k++] = '\\'; out[k++] = c; }
        else if (c < 32) { out[k++] = ' '; }
        else out[k++] = c;
    }
    out[k] = 0;
}
static int hexv(char c) { return isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10); }
static void url_decode(const char *src, size_t n, char *out, size_t cap) {
    size_t k = 0;
    for (size_t i = 0; i < n && k + 1 < cap; i++) {
        if (src[i] == '%' && i + 2 < n + 0 + 1 && isxdigit((unsigned char)src[i+1]) && isxdigit((unsigned char)src[i+2])) {
            out[k++] = (char)(hexv(src[i+1]) * 16 + hexv(src[i+2])); i += 2;
        } else if (src[i] == '+') out[k++] = ' ';
        else out[k++] = src[i];
    }
    out[k] = 0;
}
static int get_param(const char *body, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key); const char *p = body;
    while (*p) {
        const char *end = strchr(p, '&');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
            url_decode(p + kl + 1, len - kl - 1, out, cap); return 1;
        }
        if (!end) break;
        p = end + 1;
    }
    out[0] = 0; return 0;
}
static int header_value(const char *req, const char *name, char *out, size_t cap) {
    size_t nl = strlen(name); const char *p = strstr(req, "\r\n");
    while (p) {
        p += 2;
        if (p[0] == '\r' || p[0] == 0) break;
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)name[i])) i++;
        if (i == nl && p[nl] == ':') {
            const char *v = p + nl + 1; while (*v == ' ') v++;
            size_t k = 0; while (*v && *v != '\r' && k + 1 < cap) out[k++] = *v++;
            out[k] = 0; return 1;
        }
        p = strstr(p, "\r\n");
    }
    out[0] = 0; return 0;
}

/* ---------- storage ---------- */
static void load_products(void) {
    FILE *f = fopen(PRODUCTS_FILE, "r"); char line[512];
    np = 0; next_pid = 1;
    if (!f) return;
    while (fgets(line, sizeof line, f) && np < MAX_PRODUCTS) {
        Product p; memset(&p, 0, sizeof p);
        if (sscanf(line, "%d|%63[^|]|%31[^|]|%lf|%d|%15[^|\n]", &p.id, p.name, p.cat, &p.price, &p.stock, p.icon) == 6) {
            trim(p.icon); prods[np++] = p; if (p.id >= next_pid) next_pid = p.id + 1;
        }
    }
    fclose(f);
}
static void save_products(void) {
    FILE *f = fopen(PRODUCTS_FILE, "w"); if (!f) return;
    for (int i = 0; i < np; i++)
        fprintf(f, "%d|%s|%s|%.2f|%d|%s\n", prods[i].id, prods[i].name, prods[i].cat, prods[i].price, prods[i].stock, prods[i].icon);
    fclose(f);
}
static void load_orders(void) {
    FILE *f = fopen(ORDERS_FILE, "r"); char line[2048];
    no = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f) && no < MAX_ORDERS) {
        Order o; memset(&o, 0, sizeof o);
        if (sscanf(line, "%d|%63[^|]|%23[^|]|%159[^|]|%1023[^|]|%lf|%15[^|]|%23[^|\n]",
                   &o.id, o.name, o.phone, o.addr, o.items, &o.total, o.status, o.date) == 8) {
            trim(o.date); orders[no++] = o; if (o.id >= next_oid) next_oid = o.id + 1;
        }
    }
    fclose(f);
}
static void save_orders(void) {
    FILE *f = fopen(ORDERS_FILE, "w"); if (!f) return;
    for (int i = 0; i < no; i++)
        fprintf(f, "%d|%s|%s|%s|%s|%.2f|%s|%s\n", orders[i].id, orders[i].name, orders[i].phone,
                orders[i].addr, orders[i].items, orders[i].total, orders[i].status, orders[i].date);
    fclose(f);
}
static int find_product(int id) {
    for (int i = 0; i < np; i++) if (prods[i].id == id) return i;
    return -1;
}

/* ---------- HTTP output ---------- */
static void send_all(sock_t s, const char *d, size_t n) {
    while (n > 0) { int r = send(s, d, (int)n, 0); if (r <= 0) return; d += r; n -= r; }
}
static void respond(sock_t s, int code, const char *ctype, const char *body, size_t len) {
    const char *st = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" :
                     code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
    char h[256];
    int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\n\r\n",
                     code, st, ctype, (unsigned long)len);
    send_all(s, h, n); send_all(s, body, len);
}
static void send_json(sock_t s, int code, const char *json) {
    respond(s, code, "application/json; charset=utf-8", json, strlen(json));
}
static void send_error(sock_t s, int code, const char *msg) {
    char e[256]; char m[200]; json_escape(msg, m, sizeof m);
    snprintf(e, sizeof e, "{\"error\":\"%s\"}", m); send_json(s, code, e);
}

static const char *mime(const char *path) {
    const char *e = strrchr(path, '.'); if (!e) return "application/octet-stream";
    if (!strcmp(e, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(e, ".css"))  return "text/css; charset=utf-8";
    if (!strcmp(e, ".js"))   return "application/javascript; charset=utf-8";
    if (!strcmp(e, ".svg"))  return "image/svg+xml";
    if (!strcmp(e, ".png"))  return "image/png";
    if (!strcmp(e, ".jpg") || !strcmp(e, ".jpeg")) return "image/jpeg";
    return "application/octet-stream";
}
static void serve_static(sock_t s, const char *path) {
    if (strstr(path, "..")) { send_error(s, 403, "Forbidden"); return; }
    char fp[300];
    snprintf(fp, sizeof fp, "public%s%s", path, strcmp(path, "/") == 0 ? "index.html" : "");
    FILE *f = fopen(fp, "rb");
    if (!f) { send_error(s, 404, "Page not found"); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *data = malloc(sz + 1);
    size_t got = fread(data, 1, sz, f); fclose(f);
    respond(s, 200, mime(fp), data, got);
    free(data);
}

/* ---------- API handlers ---------- */
static void api_products(sock_t s) {
    Buf b = {0}; char n[140], c[70], ic[40];
    buf_add(&b, "[");
    for (int i = 0; i < np; i++) {
        json_escape(prods[i].name, n, sizeof n); json_escape(prods[i].cat, c, sizeof c); json_escape(prods[i].icon, ic, sizeof ic);
        buf_add(&b, "%s{\"id\":%d,\"name\":\"%s\",\"category\":\"%s\",\"price\":%.2f,\"stock\":%d,\"icon\":\"%s\"}",
                i ? "," : "", prods[i].id, n, c, prods[i].price, prods[i].stock, ic);
    }
    buf_add(&b, "]");
    send_json(s, 200, b.d); free(b.d);
}

static void api_place_order(sock_t s, const char *body) {
    Order o; memset(&o, 0, sizeof o);
    char items[512];
    get_param(body, "name", o.name, sizeof o.name);
    get_param(body, "phone", o.phone, sizeof o.phone);
    get_param(body, "address", o.addr, sizeof o.addr);
    get_param(body, "items", items, sizeof items);
    clean(o.name); clean(o.phone); clean(o.addr); trim(o.name); trim(o.phone); trim(o.addr);
    if (!o.name[0] || !o.phone[0] || !o.addr[0]) { send_error(s, 400, "Please fill in name, phone and address."); return; }
    if (no >= MAX_ORDERS) { send_error(s, 400, "Order limit reached."); return; }

    int idx[50], qty[50], n = 0; char *p = items;
    while (*p && n < 50) {
        int id, q, used = 0;
        if (sscanf(p, "%d:%d%n", &id, &q, &used) < 2) break;
        p += used; if (*p == ',') p++;
        int k = find_product(id);
        if (k < 0 || q <= 0) { send_error(s, 400, "Invalid item in cart."); return; }
        if (prods[k].stock < q) {
            char m[160]; snprintf(m, sizeof m, "Only %d left of %s.", prods[k].stock, prods[k].name);
            send_error(s, 400, m); return;
        }
        idx[n] = k; qty[n] = q; n++;
    }
    if (n == 0) { send_error(s, 400, "Your cart is empty."); return; }

    size_t off = 0;
    for (int i = 0; i < n; i++) {
        Product *pr = &prods[idx[i]];
        pr->stock -= qty[i]; o.total += pr->price * qty[i];
        if (off < sizeof o.items - 1)
            off += snprintf(o.items + off, sizeof o.items - off, "%s%s x%d", i ? "; " : "", pr->name, qty[i]);
    }
    o.id = next_oid++;
    strcpy(o.status, "Pending");
    time_t t = time(NULL); strftime(o.date, sizeof o.date, "%Y-%m-%d %H:%M", localtime(&t));
    orders[no++] = o;
    save_products(); save_orders();
    char r[128]; snprintf(r, sizeof r, "{\"orderId\":%d,\"total\":%.2f}", o.id, o.total);
    send_json(s, 200, r);
}

static void api_orders(sock_t s) {
    Buf b = {0}; char nm[140], ph[60], ad[340], it[2100];
    buf_add(&b, "[");
    for (int i = no - 1; i >= 0; i--) {
        json_escape(orders[i].name, nm, sizeof nm); json_escape(orders[i].phone, ph, sizeof ph);
        json_escape(orders[i].addr, ad, sizeof ad); json_escape(orders[i].items, it, sizeof it);
        buf_add(&b, "%s{\"id\":%d,\"name\":\"%s\",\"phone\":\"%s\",\"address\":\"%s\",\"items\":\"%s\",\"total\":%.2f,\"status\":\"%s\",\"date\":\"%s\"}",
                i == no - 1 ? "" : ",", orders[i].id, nm, ph, ad, it, orders[i].total, orders[i].status, orders[i].date);
    }
    buf_add(&b, "]");
    send_json(s, 200, b.d); free(b.d);
}

static void api_order_status(sock_t s, const char *body) {
    char id_s[16], st[16];
    get_param(body, "id", id_s, sizeof id_s); get_param(body, "status", st, sizeof st);
    if (strcmp(st, "Pending") && strcmp(st, "Shipped") && strcmp(st, "Delivered") && strcmp(st, "Cancelled")) {
        send_error(s, 400, "Invalid status."); return;
    }
    int id = atoi(id_s);
    for (int i = 0; i < no; i++) if (orders[i].id == id) {
        strcpy(orders[i].status, st); save_orders(); send_json(s, 200, "{\"ok\":true}"); return;
    }
    send_error(s, 404, "Order not found.");
}

static void api_product_save(sock_t s, const char *body) {
    char id_s[16], pr_s[32], st_s[16];
    Product p; memset(&p, 0, sizeof p);
    get_param(body, "id", id_s, sizeof id_s);
    get_param(body, "name", p.name, sizeof p.name);
    get_param(body, "category", p.cat, sizeof p.cat);
    get_param(body, "price", pr_s, sizeof pr_s);
    get_param(body, "stock", st_s, sizeof st_s);
    get_param(body, "icon", p.icon, sizeof p.icon);
    clean(p.name); clean(p.cat); clean(p.icon); trim(p.name); trim(p.cat); trim(p.icon);
    p.price = atof(pr_s); p.stock = atoi(st_s); p.id = atoi(id_s);
    if (!p.name[0] || !p.cat[0] || p.price < 0 || p.stock < 0) { send_error(s, 400, "Enter a name, category, price and stock."); return; }
    if (!p.icon[0]) strcpy(p.icon, "🎁");
    if (p.id == 0) {
        if (np >= MAX_PRODUCTS) { send_error(s, 400, "Product limit reached."); return; }
        p.id = next_pid++; prods[np++] = p;
    } else {
        int k = find_product(p.id);
        if (k < 0) { send_error(s, 404, "Product not found."); return; }
        prods[k] = p;
    }
    save_products(); send_json(s, 200, "{\"ok\":true}");
}

static void api_product_delete(sock_t s, const char *body) {
    char id_s[16]; get_param(body, "id", id_s, sizeof id_s);
    int k = find_product(atoi(id_s));
    if (k < 0) { send_error(s, 404, "Product not found."); return; }
    for (int i = k; i < np - 1; i++) prods[i] = prods[i + 1];
    np--; save_products(); send_json(s, 200, "{\"ok\":true}");
}

/* ---------- request loop ---------- */
static int admin_ok(const char *req) {
    char t[128]; header_value(req, "x-token", t, sizeof t);
    return strcmp(t, admin_token) == 0;
}

static void handle(sock_t s) {
    static char req[REQ_MAX];
    int len = 0, hdr_end = -1, cl = -1;
    while (len < REQ_MAX - 1) {
        int r = recv(s, req + len, REQ_MAX - 1 - len, 0);
        if (r <= 0) break;
        len += r; req[len] = 0;
        if (hdr_end < 0) { char *e = strstr(req, "\r\n\r\n"); if (e) hdr_end = (int)(e - req) + 4; }
        if (hdr_end >= 0) {
            if (cl < 0) { char v[24]; cl = header_value(req, "content-length", v, sizeof v) ? atoi(v) : 0; }
            if (len >= hdr_end + cl) break;
        }
    }
    if (hdr_end < 0) return;
    const char *body = req + hdr_end;

    char method[8], path[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) return;
    char *q = strchr(path, '?'); if (q) *q = 0;
    int get = !strcmp(method, "GET"), post = !strcmp(method, "POST");
    printf("%s %s\n", method, path);

    if (get && !strcmp(path, "/api/products"))        api_products(s);
    else if (post && !strcmp(path, "/api/order"))     api_place_order(s, body);
    else if (post && !strcmp(path, "/api/admin/login")) {
        char pw[64]; get_param(body, "password", pw, sizeof pw);
        if (!strcmp(pw, admin_pw)) {
            char r[200]; snprintf(r, sizeof r, "{\"token\":\"%s\"}", admin_token);
            send_json(s, 200, r);
        } else send_error(s, 401, "Wrong password.");
    }
    else if (!strncmp(path, "/api/admin/", 11)) {
        if (!admin_ok(req)) { send_error(s, 401, "Please sign in as admin."); return; }
        if (get && !strcmp(path, "/api/admin/orders"))              api_orders(s);
        else if (post && !strcmp(path, "/api/admin/order-status"))  api_order_status(s, body);
        else if (post && !strcmp(path, "/api/admin/product-save"))  api_product_save(s, body);
        else if (post && !strcmp(path, "/api/admin/product-delete")) api_product_delete(s, body);
        else send_error(s, 404, "Unknown API.");
    }
    else if (get && strncmp(path, "/api/", 5) != 0)    serve_static(s, path);
    else send_error(s, 404, "Unknown API.");
}

int main(void) {
#ifdef _WIN32
    WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 1; }
#endif
    const char *e;
    int port = DEFAULT_PORT;
    if ((e = getenv("PORT")) && atoi(e) > 0) port = atoi(e);        /* Render sets PORT */
    if ((e = getenv("ADMIN_PASSWORD")) && *e) admin_pw = e;
    if ((e = getenv("ADMIN_TOKEN")) && *e) admin_token = e;
    load_products(); load_orders();
    sock_t srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { printf("Cannot create socket\n"); return 1; }
    int opt = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof opt);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)port);
    if (bind(srv, (struct sockaddr *)&a, sizeof a) < 0 || listen(srv, 16) < 0) {
        printf("Cannot listen on port %d (already in use?)\n", port); return 1;
    }
    printf("Kids Shop running on port %d -> http://localhost:%d  (admin page: /admin.html)\n", port, port);
    fflush(stdout);
    printf("Loaded %d products, %d orders\n", np, no);
    for (;;) {
        sock_t c = accept(srv, NULL, NULL);
        if (c == INVALID_SOCKET) continue;
#ifdef _WIN32
        DWORD tmo = 5000; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
#else
        struct timeval tmo = {5, 0}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tmo, sizeof tmo);
#endif
        handle(c); CLOSESOCK(c);
    }
}
