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
#include <fcntl.h> // Add this line to include fcntl
#include <poll.h> // Add this line to include poll.h

#define PORT 1234
#define BUFFER_SIZE 64
#define MAX_MESSAGE_SIZE 4096

using namespace std;

enum class State {
    REQ = 0,
    RES = 1,
    END = 2,
};

struct Conn {
    int fd = -1;
    State state = State::REQ;
    // buffer for read
    size_t rbuf_size = 0;
    uint8_t rbuf[MAX_MESSAGE_SIZE+4];
    // buffer for write
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4+MAX_MESSAGE_SIZE];
};

// Function prototypes
static sockaddr_in create_addr(uint16_t port);
static void handle_client(int connfd);
static void sigint_handler(int sig_num);
static int32_t handle_one_request(int connfd);
static int32_t handle_request(int connfd);
static int32_t send_response(int connfd);
static void fd_set_nb(int fd);

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

  std::vector<Conn *> fd2conn;

  fd_set_nb(server_fd);

  vector<struct pollfd> poll_args;
  // the event loop
  while (true) {
    poll_args.clear();

    struct pollfd pfd = {server_fd, POLLIN, 0};
    poll_args.push_back(pfd);

    for(Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }

      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == State::REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    for(size_t i = 1; i < poll_args.size(); i++) {
      if (poll_args[i].revents) {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn-> == State::END) {
          // client closed normally, or something bad happened
          // destroy the conn
          fd2conn[conn->fd] = nullptr;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }

    if(poll_args[0].revents) {
      (void)accept_new_connection(fd2conn, server_fd);
    }
  }

	return 0;
}

static void conn_put(vector<Conn *> &fd2conn, int fd, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_connection(vector<Conn *> &fd2conn, int server_fd) {
  struct sockaddr_in client_addr = {};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(server_fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    logger("ERROR", "accept: %s", strerror(errno));
    return -1;
  }

  fd_set_nb(connfd);

  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if(!conn) {
    close(connfd);
    return -1;
  }

  conn->fd = connfd;
  conn->state = State::REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;

  conn_put(fd2conn, connfd, conn);
  return 0;
}

static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    logger("ERROR", "fcntl: %s", strerror(errno));
    std::exit(1);
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    logger("ERROR", "fcntl: %s", strerror(errno));
    std::exit(1);
  }
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
	if (len > MAX_MESSAGE_SIZE) {
		logger("ERROR", "Request too long: %u", len);
		return -1;
	}

	char request[MAX_MESSAGE_SIZE + 1];
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
