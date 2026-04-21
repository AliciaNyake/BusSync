#ifndef DATABASE_H
#define DATABASE_H

#include "sqlite3.h"

#define DB_PATH "../backend/db/bussync.db"

int db_init(sqlite3 **db);
void db_close(sqlite3 *db);

int db_register_user(sqlite3 *db, const char *email, const char *password);
int db_login_user(sqlite3 *db, const char *email, const char *password, char *token_out, int token_sz);
int db_get_email_from_token(sqlite3 *db, const char *token, char *email_out, int email_sz);

int db_get_or_create_trip(sqlite3 *db, const char *from_city, const char *to_city, const char *date, int *trip_id_out);
int db_list_trips(sqlite3 *db, const char *from_city, const char *to_city, char *json_out, int json_sz);

int db_get_seats(sqlite3 *db, int trip_id, char *json_out, int json_sz);
int db_book(sqlite3 *db, const char *token, int trip_id, int seat_no, char *res_id_out, int res_id_sz);

#endif