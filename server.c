#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <pthread.h>

#define PORT 2728
#define NRLOCURI 10

extern int errno;
sqlite3 *db;
int locurideparcare[NRLOCURI];
int sd; // socket descriptor
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void insertcar(int pozitie, const char *numar_inmatriculare) {
    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE parcare SET ocupat = 1, nr_inmatriculare = '%s' WHERE loc = %d;",
             numar_inmatriculare, pozitie);

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        printf("Eroare la pregătirea interogării: %s\n", sqlite3_errmsg(db));
        return;
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        printf("Eroare la executarea interogării: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
}

void removecar(int pozitie) {
    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE parcare SET ocupat = 0, nr_inmatriculare = NULL WHERE loc = %d;", pozitie);

    sqlite3_exec(db, sql, 0, 0, 0);
}

void get_parking_status(char *response) {
    strcpy(response, "");
    for (int i = 0; i < NRLOCURI; i++) {
        char buffer[64];
        pthread_mutex_lock(&mutex);
        snprintf(buffer, sizeof(buffer), "Loc %d: %s\n", i,
                 locurideparcare[i] ? "ocupat" : "liber");
        pthread_mutex_unlock(&mutex);
        strcat(response, buffer);
    }
}

void *thread_handler(void *arg) {
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);
    char msg[100], response[256];

    memcpy(&client, arg, sizeof(client));

    bzero(msg, sizeof(msg));
    bzero(response, sizeof(response));

    if (recvfrom(sd, msg, sizeof(msg), 0, (struct sockaddr *)&client, &client_len) <= 0) {
        perror("[server-thread] Eroare la recvfrom().\n");
        pthread_exit(NULL);
    }

    printf("[server-thread] Mesaj primit: %s\n", msg);

    if (strncmp(msg, "introducere masina:", 18) == 0) {
        char nrmat[13];
        int loc_gasit = -1;

        pthread_mutex_lock(&mutex);
        for (int i = 0; i < NRLOCURI; i++) {
            if (locurideparcare[i] == 0) {
                loc_gasit = i;
                locurideparcare[i] = 1;
                break;
            }
        }
        pthread_mutex_unlock(&mutex);

        if (loc_gasit != -1) {
            snprintf(nrmat, sizeof(nrmat), "%s", msg + 18);
            insertcar(loc_gasit, nrmat);
            snprintf(response, sizeof(response),
                     "Mașina cu numărul %s a fost parcata pe locul %d.\n", nrmat, loc_gasit);
        } else {
            snprintf(response, sizeof(response), "Toate locurile sunt ocupate.\n");
        }
    } else if (strncmp(msg, "scoatere masina:", 16) == 0) {
        char nrmat[13];
        int gasit = -1;
        snprintf(nrmat, sizeof(nrmat), "%s", msg + 16);

        pthread_mutex_lock(&mutex);
        for (int i = 0; i < NRLOCURI; i++) {
            if (locurideparcare[i] == 1) {
                char sql[128], db_nr[16];
                sqlite3_stmt *stmt;
                snprintf(sql, sizeof(sql), "SELECT nr_inmatriculare FROM parcare WHERE loc=%d;", i);
                sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const unsigned char *nr = sqlite3_column_text(stmt, 0);
                    if (nr && strcmp((const char*)nr, nrmat) == 0) {
                        gasit = i;
                        locurideparcare[i] = 0;
                        removecar(i);
                        break;
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
        pthread_mutex_unlock(&mutex);

        if (gasit != -1) {
            snprintf(response, sizeof(response), "Mașina %s a fost scoasă de pe locul %d.\n", nrmat, gasit);
        } else {
            snprintf(response, sizeof(response), "Mașina %s nu a fost găsită.\n", nrmat);
        }

    } else if (strncmp(msg, "previzualizare locuri", 21) == 0) {
        get_parking_status(response);
    } else if (strncmp(msg, "quit", 4) == 0) {
        snprintf(response, sizeof(response), "Client deconectat.\n");
    } else {
        snprintf(response, sizeof(response), "Comandă necunoscută.\n");
    }

    if (sendto(sd, response, sizeof(response), 0, (struct sockaddr *)&client, client_len) <= 0) {
        perror("[server-thread] Eroare la sendto().\n");
    }

    pthread_exit(NULL);
}

int main() {
    struct sockaddr_in server, client;

    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("[server] Eroare la socket().\n");
        return errno;
    }

    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1) {
        perror("[server] Eroare la bind().\n");
        return errno;
    }

    if (sqlite3_open("Parcare.db", &db) != SQLITE_OK) {
        printf("Eroare la deschiderea bazei de date.\n");
        return 1;
    }

    const char *sql_create_table =
        "CREATE TABLE IF NOT EXISTS parcare ("
        "id INTEGER PRIMARY KEY,"
        "loc INTEGER NOT NULL,"
        "nr_inmatriculare TEXT,"
        "ocupat INTEGER NOT NULL DEFAULT 0);";

    if (sqlite3_exec(db, sql_create_table, 0, 0, 0) != SQLITE_OK) {
        printf("Eroare la crearea tabelei: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    for (int i = 0; i < NRLOCURI; i++)
        locurideparcare[i] = 0;

    printf("[server] Serverul este gata. Ascult la portul %d...\n", PORT);

    while (1) {
        socklen_t client_len = sizeof(client);
        bzero(&client, sizeof(client));

        char temp[1];
        recvfrom(sd, temp, 1, MSG_PEEK, (struct sockaddr *)&client, &client_len); // detectăm clientul

        pthread_t thread;
        if (pthread_create(&thread, NULL, thread_handler, &client) != 0) {
            perror("[server] Eroare la pthread_create().\n");
            continue;
        }
        pthread_detach(thread);
    }

    sqlite3_close(db);
    close(sd);
    return 0;
}
