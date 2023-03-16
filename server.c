// TODO: use OS threads to make the code below much more efficient

#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define SERVER_PORT 8080 // TODO: change the server port to your university ID
#define SERVER_DIR "/srv/walter"

#define MAX_QUEUED_CONNECTIONS 64
#define MAX_REQUEST_SIZE 2048

int main(int argc, char *argv[])
{
    int program_status = EXIT_SUCCESS;

    int server_socket = -1;
    int request_socket = -1;
    int file_to_serve_handle = -1;

    char server_dir_path[PATH_MAX + 1];
    if (realpath(SERVER_DIR, server_dir_path) == NULL) {
        perror("Failed to resolve the " SERVER_DIR " into an absolute path");

        program_status = EXIT_FAILURE;
        goto end;
    }
    server_dir_path[PATH_MAX] = '\0';
    size_t server_dir_path_len = strlen(server_dir_path);

    if ((server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
        perror("Failed to create a server socket");

        program_status = EXIT_FAILURE;
        goto end;
    }

    const int reuse = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse)) == -1) {
        perror("Failed to make 0.0.0.0 reusable");

        program_status = EXIT_FAILURE;
        goto end;
    }

#ifdef SO_REUSEPORT
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, (const char *) &reuse, sizeof(reuse)) == -1) {
        perror("Failed to make " TO_STRING(SERVER_PORT) " reusable");

        program_status = EXIT_FAILURE;
        goto end;
    }
#endif

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(SERVER_PORT);
    if (bind(server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror("Failed to bind to 0.0.0.0:" TO_STRING(SERVER_PORT));

        program_status = EXIT_FAILURE;
        goto end;
    }

    if (listen(server_socket, MAX_QUEUED_CONNECTIONS) == -1) {
        perror("Failed to start listening for connections");

        program_status = EXIT_FAILURE;
        goto end;
    }

    puts("The server is listening on 0.0.0.0:" TO_STRING(SERVER_PORT));

    while (true) {
        struct sockaddr_in client_address;
        size_t client_address_size = sizeof(client_address);
        memset(&client_address, 0, client_address_size);
        if ((request_socket = accept(server_socket, (struct sockaddr *) &client_address, (socklen_t *) &client_address_size)) == -1) {
            perror("Failed to accept a new connection");

            program_status = EXIT_FAILURE;
            goto end;
        }

        char request_data[MAX_REQUEST_SIZE + 1];
        memset(request_data, 0, MAX_REQUEST_SIZE);
        if (recv(request_socket, &request_data, MAX_REQUEST_SIZE, 0) == -1) {
            perror("Failed to receive the request data");

            program_status = EXIT_FAILURE;
            goto end;
        }
        request_data[MAX_REQUEST_SIZE] = '\0';

        char file_name[NAME_MAX + 1];
        sscanf(request_data, "GET %" TO_STRING(NAME_MAX) "s HTTP", file_name);
        file_name[NAME_MAX] = '\0';
        if (strcmp(file_name, "/") == 0) {
            strncat(file_name, "index.html", NAME_MAX - strlen(file_name));
        }

        char file_path[PATH_MAX + 1];
        snprintf(file_path, PATH_MAX, "%s/%s", server_dir_path, file_name);
        file_path[PATH_MAX] = '\0';

        char resolved_path[PATH_MAX + 1];
        if (realpath(file_path, resolved_path) == NULL ||
            strncmp(resolved_path, server_dir_path, server_dir_path_len) != 0) {
            char response_data[] = "HTTP/1.1 404 Not Found\r\n\r\n";
            if (send(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
                perror("Failed to send the 404 response");

                program_status = EXIT_FAILURE;
                goto end;
            }
        } else {
            resolved_path[PATH_MAX] = '\0';
            if ((file_to_serve_handle = open(resolved_path, O_RDONLY)) == -1) {
                char response_data[] = "HTTP/1.1 404 Not Found\r\n\r\n";
                if (send(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
                    perror("Failed to send the 404 response");

                    program_status = EXIT_FAILURE;
                    goto end;
                }
            } else {
                char response_data[] = "HTTP/1.1 200 OK\r\n\r\n";
                if (send(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
                    perror("Failed to send the response header");

                    program_status = EXIT_FAILURE;
                    goto end;
                }

                char file_data[MAX_REQUEST_SIZE];

                ssize_t bytes_read;
                while ((bytes_read = read(file_to_serve_handle, file_data, MAX_REQUEST_SIZE)) > 0) {
                    if (send(request_socket, file_data, bytes_read, 0) == -1) {
                        perror("Failed to send the requested file");

                        program_status = EXIT_FAILURE;
                        goto end;
                    }
                }
                if (bytes_read == -1) {
                    perror("Failed to read the requested file");

                    program_status = EXIT_FAILURE;
                    goto end;
                }

                close(file_to_serve_handle);
                file_to_serve_handle = -1;
            }
        }

        shutdown(request_socket, SHUT_WR);
        char leftovers[1024];
        while (recv(request_socket, leftovers, sizeof(leftovers), 0) > 0) { }
        shutdown(request_socket, SHUT_RD);
        close(request_socket);
        request_socket = -1;
    }

end:
    if (file_to_serve_handle != -1) {
        close(file_to_serve_handle);
        file_to_serve_handle = -1;
    }

    if (request_socket != -1) {
        shutdown(request_socket, SHUT_WR);
        char leftovers[1024];
        while (recv(request_socket, leftovers, sizeof(leftovers), 0) > 0) { }
        shutdown(request_socket, SHUT_RD);
        close(request_socket);
        request_socket = -1;
    }

    if (server_socket != -1) {
        close(server_socket);
        server_socket = -1;
    }

    return program_status;
}
