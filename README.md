# AC - AES Encrypted File Transfer Tool

A simple client-server utility in C that securely transfers files over TCP using AES-256-CBC encryption with OpenSSL.

## Features

* AES-256-CBC encryption with key and IV
* TCP socket transfer using `sendfile()`
* Epoll-based concurrent server running on a single thread
* Encrypted file sent, decrypted on arrival

## How to Use

### 1. Get the Code

Clone the project from GitHub:

```bash
git clone https://github.com/AquaSyles/AC.git
cd AC
```

### 2. Install Dependencies

Make sure you're on a Linux system and have `gcc` and OpenSSL development libraries installed.

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential libssl-dev
```

On Arch:

```bash
sudo pacman -Syu
sudo pacman -S base-devel openssl
```

### 3. Compile the Code

```bash
gcc ac.c -o ac -lssl -lcrypto
```

This creates the `ac` executable.

### 4. Run the Server

```bash
./ac -p <port> -k <key_password>
```

* Replace `<port>` with a number like `9000`
* Replace `<key_password>` with your secret key (password)

Example:

```bash
./ac -p 9000 -k "mysecret"
```

Leave this running on the machine where you want to **receive** files.

### 5. Find Your Server IP Address

You need the server’s IP address so the client can connect to it.

Run this on the server:

```bash
ip addr show
```

Look for an IP address under your network interface, usually something like `192.168.x.x` (local network) or a public IP if over the internet.

Use that IP as `<server_ip>` when running the client.

### 6. Run the Client (Send Files)

On the sending machine, run:

```bash
./ac -f <file_to_send> -k <key_password> <server_ip> <server_port>
```

* `<file_to_send>`: path to the file you want to send
* `<key_password>`: must be the same password used by the server
* `<server_ip>`: IP address you got from step 5
* `<server_port>`: must be the same port used by the server

Example:

```bash
./ac -f test.txt -k "mysecret" 192.168.1.10 9000
```

### 7. Output

The server will receive the file and automatically decrypt it to the current directory.

---

## Notes

* Key must be securely shared or derived from a password
* Make sure firewalls allow TCP traffic on the chosen port

## Dependencies

* OpenSSL
* POSIX environment (Linux)
* GCC (to compile the code)
