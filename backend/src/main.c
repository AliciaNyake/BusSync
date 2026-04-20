#include "http.h"
#include <stdio.h>

int main(void) {
    printf("Starting BusSync backend...\n");
    if (!http_run(8080)) {
        printf("Server failed to start.\n");
        return 1;
    }
    return 0;
}