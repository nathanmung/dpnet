#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

/* Code du client, à exécuter en C. Celui-ci envoie un message personnalisable.
Attention à bien paramétrer le port (défaut : 12345) et l'adresse IP du serveur/relais
(défaut : 127.0.0.1). Le client envoie sur un port à l'IP du serveur ou relais, qui doit
écouter sur le même port pour recevoir le message */

int main() {
    int client_sock;
    struct sockaddr_in server_addr;
    char* message = "Salut, je suis le client !";

    // Étape 1 : Créer la socket
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock == -1) {
        printf("Erreur : socket non créée\n");
        return 1;
    }

    // Étape 2 : Configurer l’adresse du serveur
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");   // IP du serveur
    server_addr.sin_port = htons(12345);

    // Étape 3 : Se connecter au serveur
    if (connect(client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("Erreur : connexion échouée\n");
        return 1;
    }

    // Étape 4 : Envoyer le message
    send(client_sock, message, strlen(message), 0);
    printf("Message envoyé : %s\n", message);

    // Étape 5 : Fermer la socket
    close(client_sock);
    return 0;
}
