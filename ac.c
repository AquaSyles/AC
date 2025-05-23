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

#include <openssl/evp.h>
#include <openssl/rand.h>

#define MAX_EVENTS 10

int is_valid_file(char* file_path) {
	if (access(file_path, F_OK) == 0) {
		return 1;
	} else {
		return 0;
	}
}

void handle_error() {
	perror("Error");
	exit(1);
}

unsigned char* get_if_buffer(int if_fd, int* if_file_length) {
	struct stat st;
	fstat(if_fd, &st);

	int size = st.st_size;
	*if_file_length = size;

	unsigned char* if_buffer = malloc(size);

	read(if_fd, if_buffer, size);

	return if_buffer;
}

enum MODE {
	ENCRYPT = 0,
	DECRYPT
};

EVP_CIPHER_CTX* get_ctx(enum MODE mode, unsigned char* key, unsigned char* iv) {
	EVP_CIPHER_CTX* ctx;
	if (!(ctx = EVP_CIPHER_CTX_new())) {
		handle_error();
	}

	switch (mode) {
		case ENCRYPT:
			EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
			break;
		case DECRYPT:
			EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
			break;
	}

	return ctx;
}

int decrypt(unsigned char* key, unsigned char* iv, char* decrypt_file_path) {
	int encrypted_fd = open("encrypted", O_RDONLY);
	int decrypted_fd = open(decrypt_file_path, O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (encrypted_fd == -1 || decrypted_fd == -1) {
		printf("FD1: %d, FD2: %d", encrypted_fd, decrypted_fd);
		handle_error();
	}

	EVP_CIPHER_CTX* ctx = get_ctx(DECRYPT, key, iv);

	int encryption_length = 0;
    unsigned char* encryption_buffer = get_if_buffer(encrypted_fd, &encryption_length);

	int decrypted_length = 0;
	unsigned char* decrypted_buffer = malloc(encryption_length + EVP_CIPHER_block_size(EVP_CIPHER_CTX_cipher(ctx)));

	if (!EVP_DecryptUpdate(ctx, decrypted_buffer, &decrypted_length, encryption_buffer, encryption_length)) {
		unlink("encrypted");
		unlink(decrypt_file_path);
		handle_error();
	}

	int padding_decryption_length = 0;

	if (!EVP_DecryptFinal_ex(ctx, decrypted_buffer + decrypted_length, &padding_decryption_length)) {
		unlink("encrypted");
		unlink(decrypt_file_path);
		handle_error();
	}

	int total_bytes = decrypted_length + padding_decryption_length;

	write(decrypted_fd, decrypted_buffer, total_bytes);

	free(encryption_buffer);
	free(decrypted_buffer);

	close(encrypted_fd);

	return decrypted_fd;
}

int encrypt(unsigned char* key, unsigned char* iv, char* plaintext_file_path) {
	int plaintext_fd = open(plaintext_file_path, O_RDONLY);
	int encrypted_fd = open("encrypted", O_CREAT | O_TRUNC | O_RDWR, 0644);
	if (plaintext_fd == -1 || encrypted_fd == -1) {
		printf("FD1: %d, FD2: %d", plaintext_fd, encrypted_fd);
		handle_error();
	}

	EVP_CIPHER_CTX* ctx = get_ctx(ENCRYPT, key, iv);

	int plaintext_length = 0;
    unsigned char* plaintext_buffer = get_if_buffer(plaintext_fd, &plaintext_length);

	int encryption_length = 0;
	unsigned char* encryption_buffer = malloc(plaintext_length + EVP_CIPHER_block_size(EVP_CIPHER_CTX_cipher(ctx)));

	if (!EVP_EncryptUpdate(ctx, encryption_buffer, &encryption_length, plaintext_buffer, plaintext_length)) {
		handle_error();
	}

	int padding_encryption_length = 0;

	if (!EVP_EncryptFinal_ex(ctx, encryption_buffer + encryption_length, &padding_encryption_length)) {
		handle_error();
	}

	int total_bytes = encryption_length + padding_encryption_length;

	write(encrypted_fd, encryption_buffer, total_bytes);

	free(plaintext_buffer);
	free(encryption_buffer);

	close(plaintext_fd);

	return encrypted_fd;
}

int client(char* ip, int port, char* file, unsigned char* key) {
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

	unsigned char iv[16];
	RAND_bytes(iv, 16);
	send(fd, iv, 16, 0);

	bytes_received = recv(fd, status, sizeof(status), 0);
    if (bytes_received == -1) {
		perror("Error receiving READY message");
		return 1;
	}

	if (strncmp(status, "READY", 5) != 0) {
		fprintf(stderr, "Unexpected response from server: %s\n", status);
        	return 1;
	}

	int encrypted_fd = encrypt(key, iv, file);

	struct stat st;
	fstat(encrypted_fd, &st);

	off_t offset = 0;
	size_t remaining = st.st_size;

	while (remaining > 0) {
		printf("Remaining: %d\n", remaining);
		ssize_t sent = sendfile(fd, encrypted_fd, &offset, remaining);

		if (sent == -1) {
		    perror("sendfile");
		    break;
		} else if (sent == 0) { // Sent all
		    break;
		}

		remaining -= sent;
	}

	close(encrypted_fd);
	unlink("encrypted");
}

int server(int port, unsigned char* key) {
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

				unsigned char iv[16];
				bytes_recieved = recv(events[i].data.fd, iv, sizeof(iv), 0);

				send(events[i].data.fd, "READY", 5, 0);

				int file_fd = open("encrypted", O_CREAT | O_WRONLY | O_TRUNC, 0666);

				char buffer[4096];
				ssize_t bytes_read = 0;

				while ((bytes_read = read(events[i].data.fd, buffer, sizeof(buffer))) > 0) {
					write(file_fd, buffer, bytes_read);
				}

				int decrypted_fd = decrypt(key, iv, file_name);
				unlink("encrypted");
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
	int b_key = 0;

	char* ip;
	int port;
	unsigned char key[32];

	char* file_path;

	while ((opt = getopt(argc, argv, "p:f:k:")) != -1) {
		switch(opt) {
		case 'p':
		   	b_port = 1;
			port = atoi(optarg);
			break;
		case 'f':
			b_file = 1;
			file_path = optarg;
			break;
		case 'k':
			b_key = 1;
			int key_length = strlen(optarg);
			if (key_length > 32) key_length = 32;
			memcpy(key, optarg, key_length);
			break;
		default: /* '?' */
		   fprintf(stderr, "usage: %s [-f file] [-p port] [-k key] [destination] [port]\n",
			argv[0]);
		   exit(-1);
		}
	}

	if (!b_key) {
		memcpy(key, "key", 3);
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
		client(ip, port, file_path, key);
		return 1;
	}

	if (!b_port || b_file) {
		fprintf(stderr, "usage: %s [-f file] [-p port] [-k key] [destination] [port]\n",
			argv[0]);
		exit(-1);
	}

	server(port, key);
}
