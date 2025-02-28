#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

/* Le serveur reçoit le message et l'affiche. Attention à bien choisir le port. */

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr;
    char buffer[1024] = {0};
    int client_count = 0;  // Compteur pour limiter à 4 clients

    // Créer la socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        printf("Erreur : socket non créée\n");
        return 1;
    }

    // Configurer l’adresse
    server_addr.sin_family = AF_INET;  
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(12346);

    // Lier la socket
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Erreur : bind échoué\n");
        return 1;
    }

    // Écouter
    listen(server_sock, 5);
    printf("Serveur en écoute sur le port 12346...\n");

    // Boucle pour accepter 4 clients
    while (client_count < 4) {
        // Accepter une connexion
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            printf("Erreur : accept échoué\n");
            continue;  // Passe au suivant si erreur
        }

        // Recevoir le message
        read(client_sock, buffer, 1024);
        printf("Client %d a dit : %s\n", client_count + 1, buffer);

        // Réinitialiser le buffer
        memset(buffer, 0, 1024);  // Remet à zéro pour le prochain message

        // Fermer la socket du client
        close(client_sock);

        client_count++;  // Incrémente le compteur
    }

    // Fermer la socket serveur
    close(server_sock);
    return 0;
}
