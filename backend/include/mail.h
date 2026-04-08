#ifndef MAIL_H                                  // Anti double inclusion
#define MAIL_H

int send_confirmation_email(const char *to,      // Email destinataire
                            const char *res_id,  // ID réservation
                            int trip_id,         // Trajet
                            int seat_no);        // Siège

#endif
