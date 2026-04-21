#ifndef MAIL_H
#define MAIL_H

int send_confirmation_email(const char *to_email, const char *reservation_id, int trip_id, int seat_no);

#endif