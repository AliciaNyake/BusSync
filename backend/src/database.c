#include "database.h"                                              // Nos fonctions DB
#include <stdio.h>                                                 // printf
#include <string.h>                                                // strcpy, strlen
#include <stdlib.h>                                                // rand
#include <time.h>                                                  // time

static int exec_sql(sqlite3 *db, const char *sql) {                // Petit helper pour exécuter du SQL
  char *err = NULL;                                                // Message d’erreur SQLite
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);               // Exécute
  if (rc != SQLITE_OK) {                                           // Si erreur
    fprintf(stderr, "SQL error: %s\n", err ? err : "unknown");     // Affiche
    sqlite3_free(err);                                             // Libère
    return 0;                                                      // Échec
  }
  return 1;                                                        // OK
}

static void make_token(char *out, int out_sz) {                    // Fabrique un token simple
  const char *chars = "abcdefghijklmnopqrstuvwxyz0123456789";      // Alphabet simple
  int n = (int)strlen(chars);                                      // Taille alphabet
  srand((unsigned)time(NULL));                                     // Seed random
  if (out_sz < 32) return;                                         // Sécurité
  for (int i = 0; i < out_sz - 1; i++) {                           // Remplit le token
    out[i] = chars[rand() % n];                                    // Un caractère aléatoire
  }
  out[out_sz - 1] = '\0';                                          // Fin de chaîne
}

int db_init(sqlite3 **db) {                                        // Initialise DB
  if (sqlite3_open(DB_PATH, db) != SQLITE_OK) {                    // Ouvre / crée le fichier .db
    fprintf(stderr, "Cannot open DB: %s\n", sqlite3_errmsg(*db));  // Erreur
    return 0;                                                       // Échec
  }

  if (!exec_sql(*db, "PRAGMA foreign_keys = ON;")) return 0;       // Active clés étrangères

  // Table users (⚠️ mot de passe en clair = OK pour démo, PAS en prod)
  if (!exec_sql(*db,
    "CREATE TABLE IF NOT EXISTS users ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " email TEXT UNIQUE NOT NULL,"
    " password TEXT NOT NULL"
    ");"
  )) return 0;

  // Table sessions (token de connexion)
  if (!exec_sql(*db,
    "CREATE TABLE IF NOT EXISTS sessions ("
    " token TEXT PRIMARY KEY,"
    " user_id INTEGER NOT NULL,"
    " created_at TEXT NOT NULL DEFAULT (datetime('now')),"
    " FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
    ");"
  )) return 0;

  // Table trips : accepte n’importe quelles villes (donc “partout”)
  if (!exec_sql(*db,
    "CREATE TABLE IF NOT EXISTS trips ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " from_city TEXT NOT NULL,"
    " to_city TEXT NOT NULL,"
    " date TEXT NOT NULL,"
    " price REAL NOT NULL,"
    " seat_count INTEGER NOT NULL"
    ");"
  )) return 0;

  // Table seats
  if (!exec_sql(*db,
    "CREATE TABLE IF NOT EXISTS seats ("
    " trip_id INTEGER NOT NULL,"
    " seat_no INTEGER NOT NULL,"
    " is_booked INTEGER NOT NULL DEFAULT 0,"
    " PRIMARY KEY(trip_id, seat_no),"
    " FOREIGN KEY(trip_id) REFERENCES trips(id) ON DELETE CASCADE"
    ");"
  )) return 0;

  // Table reservations
  if (!exec_sql(*db,
    "CREATE TABLE IF NOT EXISTS reservations ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " user_id INTEGER NOT NULL,"
    " trip_id INTEGER NOT NULL,"
    " seat_no INTEGER NOT NULL,"
    " res_id TEXT UNIQUE NOT NULL,"
    " created_at TEXT NOT NULL DEFAULT (datetime('now')),"
    " FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,"
    " FOREIGN KEY(trip_id) REFERENCES trips(id) ON DELETE CASCADE"
    ");"
  )) return 1;                                                     // OK
}

void db_close(sqlite3 *db) {                                       // Ferme DB
  if (db) sqlite3_close(db);                                       // Ferme si non NULL
}

int db_register_user(sqlite3 *db, const char *email, const char *password) {
  const char *sql = "INSERT INTO users(email,password) VALUES(?,?);"; // SQL inscription
  sqlite3_stmt *st = NULL;                                         // Statement
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0; // Prépare
  sqlite3_bind_text(st, 1, email, -1, SQLITE_TRANSIENT);           // Bind email
  sqlite3_bind_text(st, 2, password, -1, SQLITE_TRANSIENT);        // Bind password
  int rc = sqlite3_step(st);                                       // Exécute
  sqlite3_finalize(st);                                            // Libère
  return rc == SQLITE_DONE;                                        // True si OK
}

int db_login_user(sqlite3 *db, const char *email, const char *password, char *token_out, int token_sz) {
  // Cherche user
  const char *sql = "SELECT id FROM users WHERE email=? AND password=?;"; // SQL login
  sqlite3_stmt *st = NULL;                                         // Statement
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0; // Prépare
  sqlite3_bind_text(st, 1, email, -1, SQLITE_TRANSIENT);           // Bind email
  sqlite3_bind_text(st, 2, password, -1, SQLITE_TRANSIENT);        // Bind password

  int rc = sqlite3_step(st);                                       // Exécute
  if (rc != SQLITE_ROW) {                                          // Si pas trouvé
    sqlite3_finalize(st);                                          // Libère
    return 0;                                                      // Échec
  }
  int user_id = sqlite3_column_int(st, 0);                         // ID user
  sqlite3_finalize(st);                                            // Libère

  // Crée token et stocke en DB
  make_token(token_out, token_sz);                                 // Token aléatoire

  const char *sql2 = "INSERT INTO sessions(token,user_id) VALUES(?,?);"; // Insert session
  sqlite3_stmt *st2 = NULL;                                        // Statement
  if (sqlite3_prepare_v2(db, sql2, -1, &st2, NULL) != SQLITE_OK) return 0; // Prépare
  sqlite3_bind_text(st2, 1, token_out, -1, SQLITE_TRANSIENT);      // Bind token
  sqlite3_bind_int(st2, 2, user_id);                               // Bind user_id
  int rc2 = sqlite3_step(st2);                                     // Exécute
  sqlite3_finalize(st2);                                           // Libère
  return rc2 == SQLITE_DONE;                                       // OK
}

static int ensure_seats(sqlite3 *db, int trip_id, int seat_count) {
  // Insère seats 1..seat_count si pas existants
  const char *sql = "INSERT OR IGNORE INTO seats(trip_id,seat_no,is_booked) VALUES(?,?,0);";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;

  for (int s = 1; s <= seat_count; s++) {
    sqlite3_bind_int(st, 1, trip_id);
    sqlite3_bind_int(st, 2, s);
    if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); return 0; }
    sqlite3_reset(st);
  }
  sqlite3_finalize(st);
  return 1;
}

int db_get_or_create_trip(sqlite3 *db, const char *from_city, const char *to_city, const char *date, int *trip_id_out) {
  // 1) Cherche un trip existant
  const char *sql = "SELECT id, seat_count FROM trips WHERE from_city=? AND to_city=? AND date=? LIMIT 1;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_text(st, 1, from_city, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, to_city, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 3, date, -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(st);
  if (rc == SQLITE_ROW) {
    int id = sqlite3_column_int(st, 0);
    int seat_count = sqlite3_column_int(st, 1);
    sqlite3_finalize(st);
    *trip_id_out = id;
    return ensure_seats(db, id, seat_count);
  }
  sqlite3_finalize(st);

  // 2) Si pas trouvé : on crée un trip “demo” -> comme ça bus peut aller partout
  const char *sql2 = "INSERT INTO trips(from_city,to_city,date,price,seat_count) VALUES(?,?,?,?,?);";
  sqlite3_stmt *st2 = NULL;
  if (sqlite3_prepare_v2(db, sql2, -1, &st2, NULL) != SQLITE_OK) return 0;

  double price = 10.0 + (double)(strlen(from_city) + strlen(to_city)); // Prix “demo” simple
  int seat_count = 50;                                                  // 50 sièges

  sqlite3_bind_text(st2, 1, from_city, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st2, 2, to_city, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st2, 3, date, -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(st2, 4, price);
  sqlite3_bind_int(st2, 5, seat_count);

  if (sqlite3_step(st2) != SQLITE_DONE) { sqlite3_finalize(st2); return 0; }
  sqlite3_finalize(st2);

  int new_id = (int)sqlite3_last_insert_rowid(db);
  *trip_id_out = new_id;
  return ensure_seats(db, new_id, seat_count);
}

int db_list_trips(sqlite3 *db, const char *from_city, const char *to_city, char *json_out, int json_sz) {
  int trip_id = -1;
  if (!db_get_or_create_trip(db, from_city, to_city, "2026-02-01", &trip_id)) return 0;

  const char *sql =
    "SELECT id, from_city, to_city, date, price, seat_count FROM trips WHERE from_city=? AND to_city=?;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_text(st, 1, from_city, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, to_city, -1, SQLITE_TRANSIENT);

  snprintf(json_out, json_sz, "{\"status\":\"OK\",\"trips\":[");
  int first = 1;

  while (sqlite3_step(st) == SQLITE_ROW) {
    if (!first) strncat(json_out, ",", (size_t)json_sz);
    first = 0;

    char row[512];
    snprintf(row, sizeof(row),
      "{\"id\":%d,\"from\":\"%s\",\"to\":\"%s\",\"date\":\"%s\",\"price\":%.2f,\"seat_count\":%d}",
      sqlite3_column_int(st, 0),
      (const char*)sqlite3_column_text(st, 1),
      (const char*)sqlite3_column_text(st, 2),
      (const char*)sqlite3_column_text(st, 3),
      sqlite3_column_double(st, 4),
      sqlite3_column_int(st, 5)
    );
    strncat(json_out, row, (size_t)json_sz);
  }

  sqlite3_finalize(st);
  strncat(json_out, "]}", (size_t)json_sz);
  return 1;
}

int db_get_seats(sqlite3 *db, int trip_id, char *json_out, int json_sz) {
  const char *sql = "SELECT seat_no, is_booked FROM seats WHERE trip_id=? ORDER BY seat_no;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_int(st, 1, trip_id);

  snprintf(json_out, json_sz, "{\"status\":\"OK\",\"trip_id\":%d,\"seats\":\"", trip_id);

  while (sqlite3_step(st) == SQLITE_ROW) {
    int booked = sqlite3_column_int(st, 1);
    strncat(json_out, booked ? "1" : "0", (size_t)json_sz);
  }

  sqlite3_finalize(st);
  strncat(json_out, "\"}", (size_t)json_sz);
  return 1;
}

static int token_to_user(sqlite3 *db, const char *token, int *user_id_out, char *email_out, int email_sz) {
  const char *sql = "SELECT u.id, u.email FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token=?;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
  sqlite3_bind_text(st, 1, token, -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(st);
  if (rc != SQLITE_ROW) { sqlite3_finalize(st); return 0; }

  *user_id_out = sqlite3_column_int(st, 0);
  snprintf(email_out, email_sz, "%s", (const char*)sqlite3_column_text(st, 1));

  sqlite3_finalize(st);
  return 1;
}

int db_book(sqlite3 *db, const char *token, int trip_id, int seat_no, char *res_id_out, int res_id_sz) {
  int user_id = -1;
  char email[256];
  if (!token_to_user(db, token, &user_id, email, sizeof(email))) return 0;

  // ID réservation simple
  snprintf(res_id_out, res_id_sz, "R-%d-%d-%ld", trip_id, seat_no, (long)time(NULL));

  // Transaction : empêche 2 personnes de prendre le même siège
  exec_sql(db, "BEGIN TRANSACTION;");

  // 1) réserver le siège uniquement si libre
  const char *sql1 = "UPDATE seats SET is_booked=1 WHERE trip_id=? AND seat_no=? AND is_booked=0;";
  sqlite3_stmt *st1 = NULL;
  if (sqlite3_prepare_v2(db, sql1, -1, &st1, NULL) != SQLITE_OK) { exec_sql(db,"ROLLBACK;"); return 0; }
  sqlite3_bind_int(st1, 1, trip_id);
  sqlite3_bind_int(st1, 2, seat_no);

  if (sqlite3_step(st1) != SQLITE_DONE) { sqlite3_finalize(st1); exec_sql(db,"ROLLBACK;"); return 0; }
  int changed = sqlite3_changes(db);  // Combien de lignes modifiées (0 = siège déjà pris)
  sqlite3_finalize(st1);

  if (changed == 0) { exec_sql(db, "ROLLBACK;"); return 0; }

  // 2) enregistrer la réservation
  const char *sql2 = "INSERT INTO reservations(user_id,trip_id,seat_no,res_id) VALUES(?,?,?,?);";
  sqlite3_stmt *st2 = NULL;
  if (sqlite3_prepare_v2(db, sql2, -1, &st2, NULL) != SQLITE_OK) { exec_sql(db,"ROLLBACK;"); return 0; }
  sqlite3_bind_int(st2, 1, user_id);
  sqlite3_bind_int(st2, 2, trip_id);
  sqlite3_bind_int(st2, 3, seat_no);
  sqlite3_bind_text(st2, 4, res_id_out, -1, SQLITE_TRANSIENT);

  if (sqlite3_step(st2) != SQLITE_DONE) { sqlite3_finalize(st2); exec_sql(db,"ROLLBACK;"); return 0; }
  sqlite3_finalize(st2);

  exec_sql(db, "COMMIT;");
  return 1;
}
int db_get_email_from_token(sqlite3 *db, const char *token, char *email_out, int email_sz) {   // Fonction simple
  const char *sql =                                                                       // SQL: token -> email
    "SELECT u.email FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token=?;";

  sqlite3_stmt *st = NULL;                                                                // Statement
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;                  // Prépare

  sqlite3_bind_text(st, 1, token, -1, SQLITE_TRANSIENT);                                  // Bind token

  int rc = sqlite3_step(st);                                                              // Exécute
  if (rc != SQLITE_ROW) {                                                                 // Si pas trouvé
    sqlite3_finalize(st);                                                                 // Libère
    return 0;                                                                             // Échec
  }

  snprintf(email_out, email_sz, "%s", (const char*)sqlite3_column_text(st, 0));            // Copie email
  sqlite3_finalize(st);                                                                   // Libère
  return 1;                                                                               // OK
}
