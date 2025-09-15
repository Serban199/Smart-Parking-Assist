#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define PORT 2728

int main() {
    int sd;
    struct sockaddr_in server;
    char msg[100], response[256];
    socklen_t server_len = sizeof(server);

    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Eroare la crearea socket-ului");
        return 1;
    }

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(PORT);

    while (1) {
        printf("Comanda: ");
        fgets(msg, sizeof(msg), stdin);
        msg[strcspn(msg, "\n")] = 0;

        if (sendto(sd, msg, strlen(msg)+1, 0, (struct sockaddr *)&server, server_len) < 0) {
            perror("Eroare la sendto");
            continue;
        }

        bzero(response, sizeof(response));
        if (recvfrom(sd, response, sizeof(response), 0, (struct sockaddr *)&server, &server_len) < 0) {
            perror("Eroare la recvfrom");
            continue;
        }

        printf("Server: %s\n", response);

        if (strncmp(msg, "quit", 4) == 0) break;
    }

    close(sd);
    return 0;
}
