#include "../credis-common/include/credis_common.h"
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <csignal>
#include <vector>
#include <algorithm>

#define PORT 1234
#define BUFFER_SIZE 64
#define MAX_REQUEST_SIZE 4096
#define MAX_RESPONSE_SIZE 1024

enum class State {
    REQ = 0,
    RES = 1,
    END = 2,
};

struct Conn {
    int fd = -1;
    State state = State::REQ;
};

// Function prototypes
static sockaddr_in create_addr(uint16_t port);
static void handle_client(int connfd);
static void sigint_handler(int sig_num);
static int32_t handle_one_request(int connfd);
static int32_t handle_request(int connfd);
static int32_t send_response(int connfd);

// Global variables
static int server_fd;

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

        while (true) {
            int32_t err = handle_one_request(connfd);
            if (err) {
                break;
            }
        }

        close(connfd);
    }

    return 0;
}

static int32_t handle_one_request(int connfd) {
    int32_t err = handle_request(connfd);
    if (err) return err;

    return send_response(connfd);
}

static int32_t handle_request(int connfd) {
    char buf[4];
    int32_t err = read_full(connfd, buf, 4);
    if (err) {
        logger("ERROR", errno ? "read length: %s" : "EOF", strerror(errno));
        return err;
    }

    uint32_t len;
    std::memcpy(&len, buf, 4);
    if (len > MAX_REQUEST_SIZE) {
        logger("ERROR", "Request too long: %u", len);
        return -1;
    }

    char request[MAX_REQUEST_SIZE + 1];
    err = read_full(connfd, request, static_cast<std::size_t>(len));
    if (err) {
        logger("ERROR", "read content: %s", strerror(errno));
        return err;
    }

    request[len] = '\0';
    logger("INFO", "Received: %s", request);
    return 0;
}

static int32_t send_response(int connfd) {
    const char reply[] = "world";
    char wbuf[4 + sizeof(reply)];
    uint32_t len = static_cast<uint32_t>(std::strlen(reply));
    std::memcpy(wbuf, &len, 4);
    std::memcpy(&wbuf[4], reply, len);
    return write_full(connfd, wbuf, 4 + len);
}

static sockaddr_in create_addr(uint16_t port) {
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
    write(connfd, response, std::strlen(response));
    logger("INFO", "Sent response to client");
}

static void sigint_handler(int sig_num) {
    logger("INFO", "Shutting down server...");
    close(server_fd);
    std::exit(0);
}
