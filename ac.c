#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>
#include <netdb.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/epoll.h>

#include <sys/sendfile.h>
#include <sys/stat.h>

#include <arpa/inet.h>

#define MAX_EVENTS 10

int is_valid_file(char* file_path) {
	if (access(file_path, F_OK) == 0) {
		return 1;
	} else {
		return 0;
	}

}


int client(char* ip, int port, char* file) {
	if (!is_valid_file(file)) {
		perror("Error");
		exit(EXIT_FAILURE);
	};

	int fd = socket(AF_INET, SOCK_STREAM, 0);            // Socket stuff
	
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	int ai_status = getaddrinfo(ip, NULL, &hints, &res);
	if (ai_status != 0) {
		perror("Error");
		exit(1);
	}

	struct sockaddr_in* sin = (struct sockaddr_in*)res->ai_addr;
	char* converted_ip = inet_ntoa(sin->sin_addr);
	printf("IP: %s\n", converted_ip);

	struct sockaddr_in server_addr;
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	inet_pton(AF_INET, converted_ip, &server_addr.sin_addr);

	if (connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
		perror("Error: connect");
		exit(1);
	};
	printf("Connected\n");

	printf("File: %s\n", file);

	// Sending filename
	send(fd, basename(file), strlen(file), 0);            // File stuff

	int filefd = open(file, O_RDONLY);

	struct stat st;
	fstat(filefd, &st);

	off_t offset = 0;
	size_t remaining = st.st_size;

	char status[6];
	ssize_t bytes_received = recv(fd, status, sizeof(status), 0);
    	if (bytes_received == -1) {
		perror("Error receiving READY message");
		return 1;
	}

	if (strncmp(status, "READY", 5) != 0) {
		fprintf(stderr, "Unexpected response from server: %s\n", status);
        	return 1;
	}

	while (remaining > 0) {
		printf("Remaining: %d\n", remaining);
		ssize_t sent = sendfile(fd, filefd, &offset, remaining);

		if (sent == -1) {
		    perror("sendfile");
		    break;
		} else if (sent == 0) { // Sent all
		    break;
		}

		remaining -= sent;
	}

	close(filefd);

}

int server(int port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0); // Making my socket and getting back the file descriptor, which tells me where it is
	if (fd == -1) {
		perror("socket creation failed");
		exit(1);
	}

	int option = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option))) {
		perror("setsockopt");
		exit(1);
	}

	struct sockaddr_in addr; // Creating sockaddr_in which I will give to my socket
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr);

	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) { // Binding my socket, now the sockaddr is saved to the socket
		perror("bind failed");
		exit(1);
	}

	printf("Binded on port = %d\n", port);
	if (listen(fd, 5) == -1) { // I put a queue of 5 before rejecting the connections
		perror("listen failed");
		exit(1);
	}

	int epfd = epoll_create1(0);
	if (epfd == -1) {
		perror("epoll_create1");
		exit(1);
	}

	struct epoll_event ev, events[MAX_EVENTS];
	ev.events = EPOLLIN;
	ev.data.fd = fd; // Listening FD

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
		perror("epoll_ctl");
		exit(1);
	}

	while (1) {
		int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

		if (n == -1) {
		    perror("epoll_wait");
		    exit(1);
		}

		for (int i = 0; i < n; i++) {

		    if (events[i].data.fd == fd) { // Listening FD gets new client connection
			int client_fd = accept(fd, NULL, NULL);
			if (client_fd == -1) {
			    perror("accept");
			    exit(1);
			}

			ev.events = EPOLLIN;
			ev.data.fd = client_fd;

			if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev)) {
			    perror("epoll_ctl");
			    exit(1);
			}

			printf("New Connection Established\n");
		    }

		    else if (events[i].events & EPOLLIN) { // a client FD has new data which we see by bitwise AND on EPOLLIN

		    	char file_name[256];
			ssize_t bytes_recieved = read(events[i].data.fd, file_name, sizeof(file_name) - 1);
			file_name[bytes_recieved] = '\0';

			send(events[i].data.fd, "READY", 5, 0);

			int file_fd = open(file_name, O_CREAT | O_WRONLY | O_TRUNC, 0666);

			char buffer[4096];
			ssize_t bytes_read = 0;

			while ((bytes_read = read(events[i].data.fd, buffer, sizeof(buffer))) > 0) {
				write(file_fd, buffer, bytes_read);
			}
			close(events[i].data.fd);
		    }
		}
	}

	close(epfd);
	close(fd);

	return 0;
}

int main(int argc, char** argv) {
	int opt;
	int b_ip = 0;
	int b_port = 0;
	int b_file = 0;

	char* ip;
	int port;

	char* file_path;

	while ((opt = getopt(argc, argv, "p:f:")) != -1) {
		switch(opt) {
		case 'p':
		   	b_port = 1;
			port = atoi(optarg);
			break;
		case 'f':
			b_file = 1;
			file_path = optarg;
			break;
		default: /* '?' */
		   fprintf(stderr, "usage: %s [-f file] [-p port] [-k key] [destination] [port]\n",
			argv[0]);
		   exit(-1);
		}
	}

	if (argc > optind + 1) { // Retrieving client args for ip and port 
		if (!b_file) {
			fprintf(stderr, "usage: %s [-f file] [-p port] [-k key] [destination] [port]\n",
				argv[0]);
			exit(-1);
		}

		b_ip = 1;
		ip = argv[optind];
		b_port = 1;
		port = atoi(argv[optind + 1]);
		client(ip, port, file_path);
		return 1;
	}

	if (!b_port || b_file) {
		fprintf(stderr, "usage: %s [-f file] [-p port] [-k key] [destination] [port]\n",
			argv[0]);
		exit(-1);
	}

	server(port);
}
