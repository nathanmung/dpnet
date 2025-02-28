#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

/* Le relais reçoit le message et l'envoie à un autre relais ou serveur.
Attention à bien préciser le port d'écoute, et l'IP et port d'envoie */

int main() {
    int server_sock, client_sock, relay_sock;
    struct sockaddr_in server_addr, relay_addr;
    char buffer[1024] = {0};

    // 1. Créer la socket serveur (écoute le client)
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        printf("Erreur : socket serveur non créée\n");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(12345);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Erreur : bind échoué\n");
        return 1;
    }
    listen(server_sock, 5);
    printf("Serveur-relais A en écoute sur 12345...\n");

    // 2. Accepter le client
    client_sock = accept(server_sock, NULL, NULL);
    if (client_sock < 0) {
        printf("Erreur : accept échoué\n");
        return 1;
    }

    // 3. Recevoir le message du client
    read(client_sock, buffer, 1024);
    printf("Reçu du client : %s\n", buffer);

    // 4. Créer la socket pour relayer vers B
    relay_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (relay_sock == -1) {
        printf("Erreur : socket relais non créée\n");
        return 1;
    }

    relay_addr.sin_family = AF_INET;
    relay_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // B sur le même ordi pour l’instant
    relay_addr.sin_port = htons(12346);                  // Port de B

    if (connect(relay_sock, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) < 0) {
        printf("Erreur : connexion à B échouée\n");
        return 1;
    }

    // 5. Relayer le message à B
    send(relay_sock, buffer, strlen(buffer), 0);
    printf("Relayed à B : %s\n", buffer);

    // 6. Fermer tout
    close(client_sock);
    close(relay_sock);
    close(server_sock);
    return 0;
}
