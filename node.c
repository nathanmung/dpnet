#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sodium.h>

#define BUFFER_SIZE 1024
#define DEFAULT_MESSAGE "Hello, I'm secured !"

/* This is the node program. A node is a client, a relay or a server, depending the arguments given in the terminal while executing.
For now, the client sends an encrypted message using XChaCha20-Poly1305, a relay relays it to a relay or server still encrypted, and a server decrypts it and shows the message.
So, if you want to run a server and client, you need to first execute the server with these arguments : ./node <listening port> <server's IP> none 0 
Then you run the client like this : ./node none client <server's IP> <server's port> "YOUR MESSAGE"
If you want to run relays as well, you need to execute relays after the server but before the client.
Execute relays like that : ./node <listening port> (same as server's port on client) <relay's IP> <next relay / server IP> <next relay / server port>

Example for a server, a relay and a client :

Terminal 1 (server) : ./node 5001 127.0.0.1 none 0 
Terminal 2 (relay)  : ./node 5000 127.0.0.1 127.0.0.1 5001
Terminal 3 (client) : ./node none client 127.0.0.1 5000 "Hello, this is a test."

Run these lines in your 3 terminals on the same computer and it will work.
Also works on 3 different computers on the same LAN.
WAN not tested yet, but it should work if settings are properly set.  */


ssize_t read_exact(int sock, void *buf, size_t size) {
    size_t total_received = 0;
    while (total_received < size) {
        ssize_t received = recv(sock, buf + total_received, size - total_received, 0);
        if (received < 0) {
            perror("recv failed");
            return -1;
        }
        if (received == 0) {
            printf("Connexion fermée par le pair\n");
            return 0;
        }
        total_received += received;
    }
    return total_received;
}

int main(int argc, char *argv[]) {
    if (argc < 5 || argc > 6) {
        printf("Usage: %s <mon_port> <mon_ip> <ip_suivant> <port_suivant> [message]\n", argv[0]);
        printf("Pour client : mon_port = none, mon_ip = client\n");
        return 1;
    }

    int server_sock, client_sock, relay_sock;
    struct sockaddr_in server_addr, relay_addr;
    unsigned char buffer[BUFFER_SIZE] = {0};
    unsigned char nonce[crypto_secretbox_xchacha20poly1305_NONCEBYTES];
    unsigned char key[crypto_secretbox_xchacha20poly1305_KEYBYTES];
    char *my_port_str = argv[1];
    char *my_ip = argv[2];
    char *next_ip = argv[3];
    int next_port = atoi(argv[4]);
    char *message = (argc == 6) ? argv[5] : DEFAULT_MESSAGE;

    if (strlen(message) >= BUFFER_SIZE) {
        printf("Erreur : le message est trop long (max %d caractères)\n", BUFFER_SIZE - 1);
        return 1;
    }

    if (sodium_init() < 0) {
        printf("Erreur : sodium_init\n");
        return 1;
    }

    memset(key, 0, crypto_secretbox_xchacha20poly1305_KEYBYTES);
    strncpy((char*)key, "supersecretkey32byteslong123456", crypto_secretbox_xchacha20poly1305_KEYBYTES);

    // Mode client
    if (strcmp(my_port_str, "none") == 0 && strcmp(my_ip, "client") == 0) {
        client_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (client_sock == -1) {
            perror("Erreur : socket client");
            return 1;
        }

        relay_addr.sin_family = AF_INET;
        relay_addr.sin_addr.s_addr = inet_addr(next_ip);
        relay_addr.sin_port = htons(next_port);

        if (connect(client_sock, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) < 0) {
            perror("Erreur : connexion au relais");
            close(client_sock);
            return 1;
        }
        printf("Connecté à %s:%d\n", next_ip, next_port);

        randombytes_buf(nonce, sizeof(nonce));
        unsigned char cipher[BUFFER_SIZE + crypto_secretbox_xchacha20poly1305_MACBYTES];
        size_t cipher_len = strlen(message) + crypto_secretbox_xchacha20poly1305_MACBYTES;
        
        crypto_secretbox_xchacha20poly1305_easy(cipher, (unsigned char*)message, strlen(message), nonce, key);
        
        uint32_t cipher_len_net = htonl(cipher_len);
        ssize_t sent = send(client_sock, &cipher_len_net, sizeof(cipher_len_net), 0);
        if (sent != sizeof(cipher_len_net)) {
            perror("Erreur : envoi taille cipher");
            close(client_sock);
            return 1;
        }
        sent = send(client_sock, nonce, sizeof(nonce), 0);
        if (sent != sizeof(nonce)) {
            perror("Erreur : envoi nonce");
            close(client_sock);
            return 1;
        }
        sent = send(client_sock, cipher, cipher_len, 0);
        if (sent != cipher_len) {
            perror("Erreur : envoi cipher");
            close(client_sock);
            return 1;
        }
        printf("Envoyé sécurisé : %s (%zu bytes)\n", message, cipher_len);
        
        close(client_sock);
        return 0;
    }

    // Mode Relais ou Service
    int my_port = atoi(my_port_str);
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Erreur : socket serveur");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(my_ip);
    server_addr.sin_port = htons(my_port);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Erreur : bind");
        return 1;
    }
    listen(server_sock, 5);
    printf("Écoute sur %s:%d...\n", my_ip, my_port);

    // Boucle infinie pour relais/service
    while (1) {
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            perror("Erreur : accept");
            continue;
        }

        uint32_t cipher_len_net;
        unsigned char received_nonce[crypto_secretbox_xchacha20poly1305_NONCEBYTES];
        unsigned char cipher[BUFFER_SIZE + crypto_secretbox_xchacha20poly1305_MACBYTES] = {0};

        // Réception des données
        ssize_t received = read_exact(client_sock, &cipher_len_net, sizeof(cipher_len_net));
        if (received != sizeof(cipher_len_net)) {
            if (received < 0) perror("Erreur : réception taille cipher");
            else printf("Erreur : taille cipher incomplète (%zd/%zu)\n", received, sizeof(cipher_len_net));
            close(client_sock);
            continue;
        }
        size_t cipher_len = ntohl(cipher_len_net);

        if (cipher_len > sizeof(cipher)) {
            printf("Erreur : message chiffré trop grand (%zu > %zu)\n", cipher_len, sizeof(cipher));
            close(client_sock);
            continue;
        }

        if (read_exact(client_sock, received_nonce, sizeof(received_nonce)) != sizeof(received_nonce)) {
            perror("Erreur : réception nonce");
            close(client_sock);
            continue;
        }

        if (read_exact(client_sock, cipher, cipher_len) != cipher_len) {
            perror("Erreur : réception cipher");
            close(client_sock);
            continue;
        }

        if (strcmp(next_ip, "none") != 0) {
            printf("Reçu sur relais : %zu bytes chiffrés\n", cipher_len);

            relay_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (relay_sock == -1) {
                perror("Erreur : socket relais");
                close(client_sock);
                continue;
            }

            // Réinitialisation complète de relay_addr à chaque itération
            memset(&relay_addr, 0, sizeof(relay_addr)); // Ajouté pour éviter tout résidu
            relay_addr.sin_family = AF_INET;
            relay_addr.sin_addr.s_addr = inet_addr(next_ip);
            relay_addr.sin_port = htons(next_port);

            if (connect(relay_sock, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) < 0) {
                perror("Erreur : connexion au suivant");
                close(relay_sock);
                close(client_sock);
                continue;
            }

            ssize_t sent = send(relay_sock, &cipher_len_net, sizeof(cipher_len_net), 0);
            if (sent != sizeof(cipher_len_net)) {
                perror("Erreur : envoi taille cipher au suivant");
                close(relay_sock);
                close(client_sock);
                continue;
            }
            sent = send(relay_sock, received_nonce, sizeof(received_nonce), 0);
            if (sent != sizeof(received_nonce)) {
                perror("Erreur : envoi nonce au suivant");
                close(relay_sock);
                close(client_sock);
                continue;
            }
            sent = send(relay_sock, cipher, cipher_len, 0);
            if (sent != cipher_len) {
                perror("Erreur : envoi cipher au suivant");
                close(relay_sock);
                close(client_sock);
                continue;
            }

            printf("Relayed à %s:%d (%zu bytes)\n", next_ip, next_port, cipher_len);
            close(relay_sock);
        } else {
            printf("Reçu sur service : %zu bytes chiffrés\n", cipher_len);
            if (crypto_secretbox_xchacha20poly1305_open_easy(buffer, cipher, cipher_len, received_nonce, key) == 0) {
                printf("Reçu final (déchiffré) : %s\n", buffer);
            } else {
                printf("Erreur : déchiffrement\n");
            }
        }

        close(client_sock);
        memset(buffer, 0, BUFFER_SIZE);
    }

    close(server_sock);
    return 0;
}
