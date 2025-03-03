#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sodium.h>

#define BUFFER_SIZE 1024
#define DEFAULT_MESSAGE "Hello, I am secure!"

/* This is the node program. A node can function as a client, a relay, or a server, depending on the arguments provided in the terminal during execution.
For now, the client sends an encrypted message using XChaCha20-Poly1305, a relay forwards it to another relay or server while it remains encrypted, and a server decrypts it and displays the message.
To run a server and client, you need to first start the server with these arguments: ./node <listening_port> <server_ip> none 0
Then, run the client like this: ./node none client <server_ip> <server_port> "YOUR MESSAGE"
If you want to include relays, execute them after the server but before the client.
Run relays with: ./node <listening_port> <relay_ip> <next_relay_or_server_ip> <next_relay_or_server_port>

Example for a server, a relay, and a client:

Terminal 1 (server): ./node 5001 127.0.0.1 none 0
Terminal 2 (relay):  ./node 5000 127.0.0.1 127.0.0.1 5001
Terminal 3 (client): ./node none client 127.0.0.1 5000 "Hello, this is a test."

Run these commands in three terminals on the same computer, and it will work.
It also works across three different computers on the same LAN.
WAN has not been tested yet, but it should work if network settings (e.g., port forwarding and firewall rules) are properly configured.

Additional notes:
- Ensure all nodes are compiled with the same version of the program and have the libsodium library installed.
- You need to compile the program with -lsodium as an argument (e.g., gcc -o node node.c -lsodium) to link the libsodium library.
- The message size is limited to 1023 characters due to the buffer size (BUFFER_SIZE - 1).
- For WAN usage, replace 127.0.0.1 with the appropriate public IP addresses and ensure ports are accessible.
*/

ssize_t read_exact(int sock, void *buf, size_t size) {
    size_t total_received = 0;
    while (total_received < size) {
        ssize_t received = recv(sock, buf + total_received, size - total_received, 0);
        if (received < 0) {
            perror("recv failed");
            return -1;
        }
        if (received == 0) {
            printf("Connection closed by peer\n");
            return 0;
        }
        total_received += received;
    }
    return total_received;
}

int main(int argc, char *argv[]) {
    if (argc < 5 || argc > 6) {
        printf("Usage: %s <my_port> <my_ip> <next_ip> <next_port> [message]\n", argv[0]);
        printf("For client: my_port = none, my_ip = client\n");
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
        printf("Error: message too long (max %d characters)\n", BUFFER_SIZE - 1);
        return 1;
    }

    if (sodium_init() < 0) {
        printf("Error: sodium_init\n");
        return 1;
    }

    memset(key, 0, crypto_secretbox_xchacha20poly1305_KEYBYTES);
    strncpy((char*)key, "supersecretkey32byteslong123456", crypto_secretbox_xchacha20poly1305_KEYBYTES);

    // Client mode
    if (strcmp(my_port_str, "none") == 0 && strcmp(my_ip, "client") == 0) {
        client_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (client_sock == -1) {
            perror("Error: client socket");
            return 1;
        }

        relay_addr.sin_family = AF_INET;
        relay_addr.sin_addr.s_addr = inet_addr(next_ip);
        relay_addr.sin_port = htons(next_port);

        if (connect(client_sock, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) < 0) {
            perror("Error: connection to relay");
            close(client_sock);
            return 1;
        }
        printf("Connected to %s:%d\n", next_ip, next_port);

        randombytes_buf(nonce, sizeof(nonce));
        unsigned char cipher[BUFFER_SIZE + crypto_secretbox_xchacha20poly1305_MACBYTES];
        size_t cipher_len = strlen(message) + crypto_secretbox_xchacha20poly1305_MACBYTES;
        
        crypto_secretbox_xchacha20poly1305_easy(cipher, (unsigned char*)message, strlen(message), nonce, key);
        
        uint32_t cipher_len_net = htonl(cipher_len);
        ssize_t sent = send(client_sock, &cipher_len_net, sizeof(cipher_len_net), 0);
        if (sent != sizeof(cipher_len_net)) {
            perror("Error: sending cipher size");
            close(client_sock);
            return 1;
        }
        sent = send(client_sock, nonce, sizeof(nonce), 0);
        if (sent != sizeof(nonce)) {
            perror("Error: sending nonce");
            close(client_sock);
            return 1;
        }
        sent = send(client_sock, cipher, cipher_len, 0);
        if (sent != cipher_len) {
            perror("Error: sending cipher");
            close(client_sock);
            return 1;
        }
        printf("Sent securely: %s (%zu bytes)\n", message, cipher_len);
        
        close(client_sock);
        return 0;
    }

    // Relay or Service mode
    int my_port = atoi(my_port_str);
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Error: server socket");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(my_ip);
    server_addr.sin_port = htons(my_port);

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error: bind");
        return 1;
    }
    listen(server_sock, 5);
    printf("Listening on %s:%d...\n", my_ip, my_port);

    // Infinite loop for relay/service
    while (1) {
        client_sock = accept(server_sock, NULL, NULL);
        if (client_sock < 0) {
            perror("Error: accept");
            continue;
        }

        uint32_t cipher_len_net;
        unsigned char received_nonce[crypto_secretbox_xchacha20poly1305_NONCEBYTES];
        unsigned char cipher[BUFFER_SIZE + crypto_secretbox_xchacha20poly1305_MACBYTES] = {0};

        // Data reception
        ssize_t received = read_exact(client_sock, &cipher_len_net, sizeof(cipher_len_net));
        if (received != sizeof(cipher_len_net)) {
            if (received < 0) perror("Error: receiving cipher size");
            else printf("Error: incomplete cipher size (%zd/%zu)\n", received, sizeof(cipher_len_net));
            close(client_sock);
            continue;
        }
        size_t cipher_len = ntohl(cipher_len_net);

        if (cipher_len > sizeof(cipher)) {
            printf("Error: encrypted message too large (%zu > %zu)\n", cipher_len, sizeof(cipher));
            close(client_sock);
            continue;
        }

        if (read_exact(client_sock, received_nonce, sizeof(received_nonce)) != sizeof(received_nonce)) {
            perror("Error: receiving nonce");
            close(client_sock);
            continue;
        }

        if (read_exact(client_sock, cipher, cipher_len) != cipher_len) {
            perror("Error: receiving cipher");
            close(client_sock);
            continue;
        }

        if (strcmp(next_ip, "none") != 0) {
            printf("Received on relay: %zu encrypted bytes\n", cipher_len);

            relay_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (relay_sock == -1) {
                perror("Error: relay socket");
                close(client_sock);
                continue;
            }

            // Complete reinitialization of relay_addr at each iteration
            memset(&relay_addr, 0, sizeof(relay_addr)); // Added to avoid any residue
            relay_addr.sin_family = AF_INET;
            relay_addr.sin_addr.s_addr = inet_addr(next_ip);
            relay_addr.sin_port = htons(next_port);

            if (connect(relay_sock, (struct sockaddr*)&relay_addr, sizeof(relay_addr)) < 0) {
                perror("Error: connection to next");
                close(relay_sock);
                close(client_sock);
                continue;
            }

            ssize_t sent = send(relay_sock, &cipher_len_net, sizeof(cipher_len_net), 0);
            if (sent != sizeof(cipher_len_net)) {
                perror("Error: sending cipher size to next");
                close(relay_sock);
                close(client_sock);
                continue;
            }
            sent = send(relay_sock, received_nonce, sizeof(received_nonce), 0);
            if (sent != sizeof(received_nonce)) {
                perror("Error: sending nonce to next");
                close(relay_sock);
                close(client_sock);
                continue;
            }
            sent = send(relay_sock, cipher, cipher_len, 0);
            if (sent != cipher_len) {
                perror("Error: sending cipher to next");
                close(relay_sock);
                close(client_sock);
                continue;
            }

            printf("Relayed to %s:%d (%zu bytes)\n", next_ip, next_port, cipher_len);
            close(relay_sock);
        } else {
            printf("Received on service: %zu encrypted bytes\n", cipher_len);
            if (crypto_secretbox_xchacha20poly1305_open_easy(buffer, cipher, cipher_len, received_nonce, key) == 0) {
                printf("Received final (decrypted): %s\n", buffer);
            } else {
                printf("Error: decryption\n");
            }
        }

        close(client_sock);
        memset(buffer, 0, BUFFER_SIZE);
    }

    close(server_sock);
    return 0;
}
