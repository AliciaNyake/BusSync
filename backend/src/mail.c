#include "mail.h"
#include <stdio.h>
#include <time.h>

int send_confirmation_email(const char *to, const char *res_id, int trip_id, int seat_no) {
    FILE *f = fopen("../backend/db/emails.log", "a");
    if (!f) return 0;

    time_t now = time(NULL);

    fprintf(f, "=== EMAIL CONFIRMATION ===\n");
    fprintf(f, "To: %s\n", to);
    fprintf(f, "Reservation: %s\n", res_id);
    fprintf(f, "Trip: %d\n", trip_id);
    fprintf(f, "Seat: %d\n", seat_no);
    fprintf(f, "Time: %lld\n\n", (long long)now);

    fclose(f);
    return 1;
}