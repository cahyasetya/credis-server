#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define PORT 1234
#define BUFFER_SIZE 64

// Function prototypes
struct sockaddr_in create_addr(uint16_t port);
static void handle_client(int connfd);
void logger(const char* level, const char* format, ...);
void sigint_handler(int sig_num);

// Global variables
int server_fd;

int main() {
    // Set up signal handler
    signal(SIGINT, sigint_handler);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        logger("ERROR", "socket: %s", strerror(errno));
        return 1;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        logger("ERROR", "setsockopt: %s", strerror(errno));
        return 1;
    }

    // Bind socket
    struct sockaddr_in addr = create_addr(PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        logger("ERROR", "bind: %s", strerror(errno));
        exit(1);
    }

    // Listen for connections
    if (listen(server_fd, SOMAXCONN) != 0) {
        logger("ERROR", "listen: %s", strerror(errno));
        exit(1);
    }

    logger("INFO", "Server is listening on port %d...", PORT);

    // Main server loop
    while (true) {
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connfd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (connfd < 0) {
            logger("ERROR", "accept: %s", strerror(errno));
            continue;
        }

        logger("INFO", "New client connected");
        handle_client(connfd);
        close(connfd);
    }

    return 0;
}

struct sockaddr_in create_addr(uint16_t port) {
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    return addr;
}

static void handle_client(int connfd) {
    char rbuf[BUFFER_SIZE] = {0};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        logger("ERROR", "read: %s", strerror(errno));
        return;
    }
    logger("INFO", "Client says: %s", rbuf);

    const char *response = "Hello, client!\n";
    write(connfd, response, strlen(response));
    logger("INFO", "Sent response to client");
}

void logger(const char* level, const char* format, ...) {
    time_t now = time(NULL);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list args;
    va_start(args, format);

    printf("[%s] %s: ", time_str, level);
    vprintf(format, args);
    printf("\n");

    va_end(args);
}

void sigint_handler(int sig_num) {
    logger("INFO", "Shutting down server...");
    close(server_fd);
    exit(0);
}
