#include "http.h"           // http_run
#include <stdio.h>          // printf

int main(void) {            // point d’entrée
  int port = 8080;          // port API
  printf("Starting BusSync backend...\n");     // log simple
  if (!http_run(port)) {    // démarre serveur
    printf("Server failed to start.\n");       // erreur
    return 1;               // code erreur
  }
  return 0;                 // ok
}