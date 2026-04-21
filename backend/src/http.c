#include "http.h"
#include "database.h"
#include "mail.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define CLOSESOCK close
#endif

#define REQ_BUF 16384

static void send_all(sock_t c, const char *data) {
    int total = 0;
    int len = (int)strlen(data);

    while (total < len) {
        int n = send(c, data + total, len - total, 0);
        if (n <= 0) return;
        total += n;
    }
}

static void respond_json(sock_t c, const char *json) {
    char response[16384];
    int body_len = (int)strlen(json);

    snprintf(response, sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, json
    );

    send_all(c, response);
}

static const char *find_body(const char *buffer) {
    const char *p = strstr(buffer, "\r\n\r\n");
    if (!p) return NULL;
    return p + 4;
}

static int get_content_length(const char *buffer) {
    const char *p = strstr(buffer, "Content-Length:");
    if (!p) return 0;

    p += strlen("Content-Length:");
    while (*p == ' ' || *p == '\t') p++;

    return atoi(p);
}

static int json_get_string(const char *body, const char *key, char *out, int out_sz) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(body, pattern);
    if (!p) return 0;

    p += strlen(pattern);

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;

    const char *q = p;
    while (*q && *q != '"') q++;
    if (*q != '"') return 0;

    int len = (int)(q - p);
    if (len >= out_sz) len = out_sz - 1;

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

static int json_get_int(const char *body, const char *key, int *value_out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(body, pattern);
    if (!p) return 0;

    p += strlen(pattern);

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return 0;
    p++;

    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    *value_out = atoi(p);
    return 1;
}

static int get_query_param(const char *buffer, const char *name, char *out, int out_sz) {
    const char *line_end = strstr(buffer, " HTTP/");
    if (!line_end) return 0;

    char first_line[1024];
    int len = (int)(line_end - buffer);
    if (len >= (int)sizeof(first_line)) len = (int)sizeof(first_line) - 1;

    memcpy(first_line, buffer, len);
    first_line[len] = '\0';

    const char *q = strchr(first_line, '?');
    if (!q) return 0;
    q++;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=", name);

    const char *p = strstr(q, pattern);
    if (!p) return 0;
    p += strlen(pattern);

    const char *end = strchr(p, '&');
    if (!end) end = first_line + strlen(first_line);

    int value_len = (int)(end - p);
    if (value_len >= out_sz) value_len = out_sz - 1;

    memcpy(out, p, value_len);
    out[value_len] = '\0';
    return 1;
}

static int get_header_value(const char *buffer, const char *header_name, char *out, int out_sz) {
    const char *p = strstr(buffer, header_name);
    if (!p) return 0;

    p += strlen(header_name);

    while (*p == ' ' || *p == ':') p++;

    const char *end = strstr(p, "\r\n");
    if (!end) return 0;

    int len = (int)(end - p);
    if (len >= out_sz) len = out_sz - 1;

    memcpy(out, p, len);
    out[len] = '\0';
    return 1;
}

int http_run(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 0;
    }
#endif

    sock_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        printf("socket failed\n");
        return 0;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("bind failed\n");
        return 0;
    }

    if (listen(server_fd, 10) == SOCKET_ERROR) {
        printf("listen failed\n");
        return 0;
    }

    printf("BusSync API running on http://localhost:%d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif

        sock_t client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == INVALID_SOCKET) continue;

        char buffer[REQ_BUF + 1];
        int total = 0;
        int n = 0;
        int content_length = 0;
        int headers_done = 0;

        memset(buffer, 0, sizeof(buffer));

        while ((n = recv(client_fd, buffer + total, REQ_BUF - total, 0)) > 0) {
            total += n;
            if (total >= REQ_BUF) break;

            buffer[total] = '\0';

            if (!headers_done) {
                const char *body = find_body(buffer);
                if (body) {
                    headers_done = 1;
                    content_length = get_content_length(buffer);

                    int body_bytes = total - (int)(body - buffer);
                    if (body_bytes >= content_length) {
                        break;
                    }
                }
            } else {
                const char *body = find_body(buffer);
                int body_bytes = total - (int)(body - buffer);
                if (body_bytes >= content_length) {
                    break;
                }
            }
        }

        buffer[total] = '\0';

        if (total > 0) {
            if (strstr(buffer, "GET /api/health") != NULL) {
                respond_json(client_fd, "{\"status\":\"OK\"}");
            }

            else if (strstr(buffer, "GET /api/trips") != NULL) {
                sqlite3 *db;

                if (!db_init(&db)) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"db failed\"}");
                    CLOSESOCK(client_fd);
                    continue;
                }

                char from_city[128];
                char to_city[128];

                if (!get_query_param(buffer, "from", from_city, sizeof(from_city)) ||
                    !get_query_param(buffer, "to", to_city, sizeof(to_city))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"use ?from=...&to=...\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                char json[8192];
                if (!db_list_trips(db, from_city, to_city, json, sizeof(json))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"query failed\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                respond_json(client_fd, json);
                db_close(db);
            }

            else if (strstr(buffer, "POST /api/register") != NULL) {
                sqlite3 *db;

                if (!db_init(&db)) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"db failed\"}");
                    CLOSESOCK(client_fd);
                    continue;
                }

                const char *body = find_body(buffer);
                if (!body) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"missing body\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                char email[256];
                char password[256];

                if (!json_get_string(body, "email", email, sizeof(email)) ||
                    !json_get_string(body, "password", password, sizeof(password))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"email/password required\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                if (!db_register_user(db, email, password)) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"user exists or invalid\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                respond_json(client_fd, "{\"status\":\"OK\",\"msg\":\"registered\"}");
                db_close(db);
            }

            else if (strstr(buffer, "POST /api/login") != NULL) {
                sqlite3 *db;

                if (!db_init(&db)) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"db failed\"}");
                    CLOSESOCK(client_fd);
                    continue;
                }

                const char *body = find_body(buffer);
                if (!body) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"missing body\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                char email[256];
                char password[256];
                char token[128];

                if (!json_get_string(body, "email", email, sizeof(email)) ||
                    !json_get_string(body, "password", password, sizeof(password))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"email/password required\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                if (!db_login_user(db, email, password, token, sizeof(token))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"bad credentials\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                char out[256];
                snprintf(out, sizeof(out), "{\"status\":\"OK\",\"token\":\"%s\"}", token);
                respond_json(client_fd, out);
                db_close(db);
            }

            else if (strstr(buffer, "POST /api/book") != NULL) {
                sqlite3 *db;

                if (!db_init(&db)) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"db failed\"}");
                    CLOSESOCK(client_fd);
                    continue;
                }

                char auth[512];
                if (!get_header_value(buffer, "Authorization", auth, sizeof(auth))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"missing Authorization header\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                const char *prefix = "Bearer ";
                if (strncmp(auth, prefix, strlen(prefix)) != 0) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"invalid Authorization format\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                const char *token = auth + strlen(prefix);

                const char *body = find_body(buffer);
                if (!body) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"missing body\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                int trip_id = 0;
                int seat_no = 0;

                if (!json_get_int(body, "trip_id", &trip_id) ||
                    !json_get_int(body, "seat_no", &seat_no)) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"trip_id/seat_no required\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                char res_id[128];
                if (!db_book(db, token, trip_id, seat_no, res_id, sizeof(res_id))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"booking failed\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                char email[256];
                if (!db_get_email_from_token(db, token, email, sizeof(email))) {
                    respond_json(client_fd, "{\"status\":\"ERROR\",\"msg\":\"email lookup failed\"}");
                    db_close(db);
                    CLOSESOCK(client_fd);
                    continue;
                }

                send_confirmation_email(email, res_id, trip_id, seat_no);

                char out[256];
                snprintf(out, sizeof(out),
                    "{\"status\":\"OK\",\"reservation_id\":\"%s\",\"email_sent\":true}",
                    res_id);

                respond_json(client_fd, out);
                db_close(db);
            }

            else {
                respond_json(client_fd, "{\"status\":\"RUNNING\"}");
            }
        }

        CLOSESOCK(client_fd);
    }

    return 1;
}