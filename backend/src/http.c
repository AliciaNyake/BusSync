#include "http.h"                  // Déclaration http_run
#include "database.h"              // Fonctions DB
#include "mail.h"                  // Email confirmation

#include <stdio.h>                 // printf
#include <stdlib.h>                // atoi
#include <string.h>                // strstr, strcpy, strlen

#ifdef _WIN32
  #include <winsock2.h>            // sockets Windows
  #include <ws2tcpip.h>            // inet_pton, etc.
  typedef SOCKET sock_t;           // type socket Windows
  #define CLOSESOCK closesocket    // fermeture socket Windows
#else
  #include <unistd.h>              // close
  #include <arpa/inet.h>           // sockaddr_in
  #include <netinet/in.h>          // INADDR_ANY
  #include <sys/socket.h>          // socket, bind, listen, accept
  typedef int sock_t;              // type socket Linux
  #define INVALID_SOCKET (-1)      // valeur invalide
  #define SOCKET_ERROR   (-1)      // erreur socket
  #define CLOSESOCK close          // fermeture socket Linux
#endif

#define REQ_BUF 20000              // buffer réception HTTP

// ------- petites fonctions utilitaires très simples -------

// Envoie tout le texte au client (même si send() n’envoie pas tout d’un coup)
static void send_all(sock_t c, const char *data) {
  size_t len = strlen(data);                           // taille du message
  size_t sent = 0;                                    // combien on a envoyé
  while (sent < len) {                                // tant qu’il reste
    int n = (int)send(c, data + sent, (int)(len - sent), 0); // envoie
    if (n <= 0) break;                                // stop si problème
    sent += (size_t)n;                                // avance
  }
}

// Réponse JSON standard + CORS (pour le navigateur)
static void respond_json(sock_t c, int status, const char *json) {
  char header[1024];                                                   // headers HTTP
  const char *status_text = (status==200)?"OK":(status==400)?"Bad Request":(status==401)?"Unauthorized":(status==404)?"Not Found":"OK";
  int body_len = (int)strlen(json);                                    // taille body

  snprintf(header, sizeof(header),
    "HTTP/1.1 %d %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %d\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
    "Connection: close\r\n"
    "\r\n",
    status, status_text, body_len
  );

  send_all(c, header);                                                 // envoie headers
  send_all(c, json);                                                   // envoie body
}

// Récupère le body HTTP (texte après \r\n\r\n)
static const char* find_body(const char *req) {
  const char *p = strstr(req, "\r\n\r\n");                             // cherche séparation
  return p ? p + 4 : NULL;                                             // retourne body
}

// Extrait une valeur JSON très simple: "key":"value"
// ⚠️ Ce n’est PAS un parseur JSON complet (mais suffisant pour ce projet)
static int json_get_string(const char *body, const char *key, char *out, int out_sz) {
  char pattern[128];                                                   // pattern "key":
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);                   // ex: "email"
  const char *p = strstr(body, pattern);                               // trouve la clé
  if (!p) return 0;                                                    // clé absente
  p = strchr(p, ':');                                                  // cherche :
  if (!p) return 0;                                                    // pas de :
  p++;                                                                 // après :
  while (*p==' ' ) p++;                                                // skip espaces
  if (*p!='\"') return 0;                                              // attend "
  p++;                                                                 // après "
  const char *q = strchr(p, '\"');                                     // fin du champ
  if (!q) return 0;                                                    // pas trouvé
  int n = (int)(q - p);                                                // longueur
  if (n >= out_sz) n = out_sz - 1;                                     // limite
  memcpy(out, p, (size_t)n);                                           // copie
  out[n] = '\0';                                                       // fin string
  return 1;                                                            // ok
}

// Extrait une valeur JSON très simple: "key":123
static int json_get_int(const char *body, const char *key, int *out) {
  char pattern[128];                                                   // pattern "key"
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);                   // ex: "trip_id"
  const char *p = strstr(body, pattern);                               // trouve clé
  if (!p) return 0;                                                    // absent
  p = strchr(p, ':');                                                  // cherche :
  if (!p) return 0;                                                    // pas de :
  p++;                                                                 // après :
  while (*p==' ' ) p++;                                                // skip espaces
  *out = atoi(p);                                                      // convertit
  return 1;                                                            // ok
}

// Lit un header HTTP simple, ex: Authorization: Bearer xxx
static int get_header(const char *req, const char *name, char *out, int out_sz) {
  const char *p = req;                                                 // début
  size_t nlen = strlen(name);                                          // taille name
  while ((p = strstr(p, "\r\n")) != NULL) {                            // ligne suivante
    p += 2;                                                            // après \r\n
    if (strncmp(p, name, nlen) == 0 && p[nlen] == ':') {               // si header match
      p += nlen + 1;                                                   // après "name:"
      while (*p == ' ') p++;                                           // skip espaces
      const char *e = strstr(p, "\r\n");                               // fin ligne
      if (!e) return 0;                                                // pas trouvé
      int len = (int)(e - p);                                          // longueur value
      if (len >= out_sz) len = out_sz - 1;                             // limite
      memcpy(out, p, (size_t)len);                                     // copie
      out[len] = '\0';                                                 // fin
      return 1;                                                        // ok
    }
  }
  return 0;                                                            // pas trouvé
}

// Parse query string très simple: /api/trips?from=Paris&to=Lyon
static int get_query_param(const char *path, const char *key, char *out, int out_sz) {
  const char *q = strchr(path, '?');                                   // début query
  if (!q) return 0;                                                    // pas de query
  q++;                                                                 // après ?
  char pattern[64];                                                    // pattern key=
  snprintf(pattern, sizeof(pattern), "%s=", key);                      // ex: from=
  const char *p = strstr(q, pattern);                                  // trouve key=
  if (!p) return 0;                                                    // absent
  p += strlen(pattern);                                                // après =
  const char *e = strchr(p, '&');                                      // fin param
  if (!e) e = path + strlen(path);                                     // sinon fin string
  int len = (int)(e - p);                                              // longueur
  if (len >= out_sz) len = out_sz - 1;                                 // limite
  memcpy(out, p, (size_t)len);                                         // copie brute
  out[len] = '\0';                                                     // fin
  return 1;                                                            // ok
}

// Retire la query (tout après ?)
static void strip_query(char *path) {
  char *q = strchr(path, '?');                                         // cherche ?
  if (q) *q = '\0';                                                    // coupe
}

// ------- gestion d’une requête HTTP -------

static void handle_request(sock_t c, const char *req) {
  char method[16] = {0};                                               // GET/POST/OPTIONS
  char raw_path[512] = {0};                                            // chemin avec query

  if (sscanf(req, "%15s %511s", method, raw_path) != 2) {              // parse ligne 1
    respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"bad request\"}");
    return;
  }

  // CORS preflight
  if (strcmp(method, "OPTIONS") == 0) {                                // navigateur envoie OPTIONS
    respond_json(c, 200, "{\"status\":\"OK\"}");                        // réponse simple
    return;
  }

  // On ouvre la DB pour cette requête (simple et robuste)
  sqlite3 *db = NULL;                                                  // handle DB
  if (!db_init(&db)) {                                                 // ouvre/crée tables
    respond_json(c, 500, "{\"status\":\"ERROR\",\"msg\":\"db init failed\"}");
    return;
  }

  // ---- ROUTE: GET /api/health ----
  if (strcmp(method, "GET") == 0 && strcmp(raw_path, "/api/health") == 0) {
    respond_json(c, 200, "{\"status\":\"OK\"}");
    db_close(db);
    return;
  }

  // ---- ROUTE: POST /api/register ----
  if (strcmp(method, "POST") == 0 && strcmp(raw_path, "/api/register") == 0) {
    const char *body = find_body(req);                                 // body JSON
    if (!body) { respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"missing body\"}"); db_close(db); return; }

    char email[256], pass[256];                                        // champs
    if (!json_get_string(body, "email", email, sizeof(email)) ||
        !json_get_string(body, "password", pass, sizeof(pass))) {
      respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"email/password required\"}");
      db_close(db);
      return;
    }

    if (!db_register_user(db, email, pass)) {                          // tente inscription
      respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"user exists or invalid\"}");
      db_close(db);
      return;
    }

    respond_json(c, 200, "{\"status\":\"OK\",\"msg\":\"registered\"}");
    db_close(db);
    return;
  }

  // ---- ROUTE: POST /api/login ----
  if (strcmp(method, "POST") == 0 && strcmp(raw_path, "/api/login") == 0) {
    const char *body = find_body(req);                                 // body JSON
    if (!body) { respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"missing body\"}"); db_close(db); return; }

    char email[256], pass[256], token[64];                             // champs
    if (!json_get_string(body, "email", email, sizeof(email)) ||
        !json_get_string(body, "password", pass, sizeof(pass))) {
      respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"email/password required\"}");
      db_close(db);
      return;
    }

    if (!db_login_user(db, email, pass, token, sizeof(token))) {       // login
      respond_json(c, 401, "{\"status\":\"ERROR\",\"msg\":\"bad credentials\"}");
      db_close(db);
      return;
    }

    char out[256];                                                     // réponse JSON
    snprintf(out, sizeof(out), "{\"status\":\"OK\",\"token\":\"%s\"}", token);
    respond_json(c, 200, out);
    db_close(db);
    return;
  }

  // ---- ROUTE: GET /api/trips?from=...&to=... ----
  if (strcmp(method, "GET") == 0 && strncmp(raw_path, "/api/trips", 9) == 0) {
    // Si c’est exactement /api/trips (avec query)
    if (strncmp(raw_path, "/api/trips", 9) == 0) {
      char from[128] = {0}, to[128] = {0};                             // villes
      if (!get_query_param(raw_path, "from", from, sizeof(from)) ||
          !get_query_param(raw_path, "to", to, sizeof(to))) {
        respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"use ?from=...&to=...\"}");
        db_close(db);
        return;
      }

      char json[12000];                                                // réponse trips
      if (!db_list_trips(db, from, to, json, sizeof(json))) {           // DB
        respond_json(c, 500, "{\"status\":\"ERROR\",\"msg\":\"db trips failed\"}");
        db_close(db);
        return;
      }

      respond_json(c, 200, json);
      db_close(db);
      return;
    }
  }

  // ---- ROUTE: GET /api/trips/<id>/seats ----
  {
    // On travaille sur un path sans query
    char path_only[512];
    snprintf(path_only, sizeof(path_only), "%s", raw_path);
    strip_query(path_only);

    int trip_id = -1;
    if (strcmp(method, "GET") == 0 && sscanf(path_only, "/api/trips/%d/seats", &trip_id) == 1) {
      char json[12000];
      if (!db_get_seats(db, trip_id, json, sizeof(json))) {
        respond_json(c, 404, "{\"status\":\"ERROR\",\"msg\":\"trip not found\"}");
        db_close(db);
        return;
      }
      respond_json(c, 200, json);
      db_close(db);
      return;
    }
  }

  // ---- ROUTE: POST /api/book ----
  if (strcmp(method, "POST") == 0 && strcmp(raw_path, "/api/book") == 0) {
    // 1) Lire Authorization header
    char auth[512] = {0};
    if (!get_header(req, "Authorization", auth, sizeof(auth))) {
      respond_json(c, 401, "{\"status\":\"ERROR\",\"msg\":\"missing Authorization\"}");
      db_close(db);
      return;
    }

    // 2) Extraire token "Bearer X"
    const char *prefix = "Bearer ";
    if (strncmp(auth, prefix, strlen(prefix)) != 0) {
      respond_json(c, 401, "{\"status\":\"ERROR\",\"msg\":\"use Bearer token\"}");
      db_close(db);
      return;
    }
    const char *token = auth + strlen(prefix);

    // 3) Lire body JSON
    const char *body = find_body(req);
    if (!body) { respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"missing body\"}"); db_close(db); return; }

    int trip_id = 0, seat_no = 0;
    if (!json_get_int(body, "trip_id", &trip_id) || !json_get_int(body, "seat_no", &seat_no)) {
      respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"trip_id & seat_no required\"}");
      db_close(db);
      return;
    }

    // 4) Book en DB (transaction: empêche double réservation)
    char res_id[128];
    if (!db_book(db, token, trip_id, seat_no, res_id, sizeof(res_id))) {
      respond_json(c, 400, "{\"status\":\"ERROR\",\"msg\":\"seat already taken or bad token\"}");
      db_close(db);
      return;
    }

    // 5) Récup email depuis token (pour confirmer)
    char email[256];
    if (!db_get_email_from_token(db, token, email, sizeof(email))) {
      respond_json(c, 500, "{\"status\":\"ERROR\",\"msg\":\"email lookup failed\"}");
      db_close(db);
      return;
    }

    // 6) Envoi “email” (mock log) -> backend/db/emails.log
    send_confirmation_email(email, res_id, trip_id, seat_no);

    // 7) Réponse OK
    char out[512];
    snprintf(out, sizeof(out),
      "{\"status\":\"OK\",\"reservation_id\":\"%s\",\"email_sent\":true}", res_id);
    respond_json(c, 200, out);

    db_close(db);
    return;
  }

  // Si aucune route ne correspond
  respond_json(c, 404, "{\"status\":\"ERROR\",\"msg\":\"route not found\"}");
  db_close(db);
}

// Lance le serveur HTTP
int http_run(int port) {
#ifdef _WIN32
  WSADATA wsa;                                                        // init winsock
  if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return 0;                 // si fail
#endif

  sock_t s = socket(AF_INET, SOCK_STREAM, 0);                         // crée socket
  if (s == INVALID_SOCKET) return 0;                                  // fail

  int opt = 1;                                                        // reuse addr
#ifdef _WIN32
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

  struct sockaddr_in addr;                                            // adresse serveur
  memset(&addr, 0, sizeof(addr));                                     // clean
  addr.sin_family = AF_INET;                                          // IPv4
  addr.sin_port = htons((unsigned short)port);                        // port
  addr.sin_addr.s_addr = htonl(INADDR_ANY);                           // 0.0.0.0

  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) return 0; // bind
  if (listen(s, 16) == SOCKET_ERROR) return 0;                        // listen

  printf("BusSync API running on http://localhost:%d\n", port);       // info

  for (;;) {                                                          // boucle infinie
    struct sockaddr_in client;                                        // client addr
#ifdef _WIN32
    int clen = (int)sizeof(client);
#else
    socklen_t clen = (socklen_t)sizeof(client);
#endif
    sock_t c = accept(s, (struct sockaddr*)&client, &clen);           // accepte client
    if (c == INVALID_SOCKET) continue;                                // ignore si fail

    char req[REQ_BUF + 1];                                            // buffer request
    int n = (int)recv(c, req, REQ_BUF, 0);                             // lit request
    if (n <= 0) { CLOSESOCK(c); continue; }                            // si rien
    req[n] = '\0';                                                    // termine string

    handle_request(c, req);                                           // traite la requête
    CLOSESOCK(c);                                                     // ferme connexion
  }

  CLOSESOCK(s);                                                       // ferme socket serveur
#ifdef _WIN32
  WSACleanup();                                                       // cleanup winsock
#endif
  return 1;                                                           // ok
}
