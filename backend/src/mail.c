#include "mail.h"                                   // Header mail
#include <stdio.h>                                  // printf, fopen
#include <time.h>                                   // time

int send_confirmation_email(const char *to,          // Email destinataire
                            const char *res_id,      // ID réservation
                            int trip_id,             // Trajet
                            int seat_no) {           // Siège
  FILE *f = fopen("backend/db/emails.log", "a");     // Ouvre le fichier log en ajout
  if (!f) return 0;                                  // Si impossible -> échec

  time_t now = time(NULL);                           // Heure actuelle
  fprintf(f, "=== EMAIL CONFIRMATION ===\n");        // Titre
  fprintf(f, "To: %s\n", to);                        // Destinataire
  fprintf(f, "Reservation: %s\n", res_id);           // ID réservation
  fprintf(f, "Trip: %d\n", trip_id);                 // Trajet
  fprintf(f, "Seat: %d\n", seat_no);                 // Siège
  fprintf(f, "Time: %lld\n\n", (long long)now);      // Timestamp
  fclose(f);                                         // Ferme le fichier
  return 1;                                          // Succès
}