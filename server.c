#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <errno.h>
#include <math.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define SERVER_PORT 8080 // TODO: change the server port to your university ID
#define SERVER_DIR "srv/front"

#define MAX_QUEUED_CONNECTIONS SOMAXCONN
#define MAX_REQUEST_SIZE 2048
#define MAX_IMAGE_SIZE (10 * 1024 * 1024)
#define MEDIAN_WINDOW 3

typedef struct
{
    unsigned char **buffer;
    size_t *size;
} buffer_context;

typedef struct
{
    unsigned char *original_image;
    size_t original_size;
    unsigned char *processed_image;
    size_t processed_size;
} image_job;

typedef struct
{
    char *key;
    image_job value;
} image_job_entry;

void write_image_callback(void *context, void *data, int size);
int float_compare(const void *a, const void *b);
int setup_server_socket(int *server_socket);
int receive_request(int request_socket, char *request_data, size_t max_size);
void parse_request(const char *request_data, char *method, char *path);
void cleanup_connection(int request_socket);
void cleanup_resources(int file_handle, int request_socket, int server_socket, image_job_entry *job_table);
void apply_median_filter(unsigned char *img, unsigned char *filtered, int w, int h, int channels);
void process_image(image_job_entry *job_table, const char *uuid_str);
ssize_t send_all(int socket, const void *buffer, size_t length, int flags);
int set_client_socket_options(int client_socket);
int handle_post_images(int request_socket, const char *request_data, ssize_t bytes_received, image_job_entry **job_table);
int handle_get_image(int request_socket, const char *path, image_job_entry *job_table);
int handle_get_static_file(int request_socket, const char *path, const char *server_dir_path, size_t server_dir_path_len, int *file_to_serve_handle);
int send_not_implemented(int request_socket);

ssize_t send_all(int socket, const void *buffer, size_t length, int flags)
{
    const char *ptr = (const char *)buffer;
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(socket, ptr + total_sent, length - total_sent, flags);
        if (sent <= 0) {
            if (sent == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            return sent;
        }
        total_sent += sent;
    }

    return total_sent;
}

void write_image_callback(void *context, void *data, int size)
{
    buffer_context *ctx = (buffer_context *)context;
    if (ctx == NULL || ctx->buffer == NULL || ctx->size == NULL || data == NULL || size <= 0) {
        return;
    }
    if (*(ctx->size) > SIZE_MAX - size) {
        return;
    }

    size_t new_size = *(ctx->size) + size;
    unsigned char *new_data = NULL;
    if (*(ctx->buffer) == NULL) {
        new_data = (unsigned char *)calloc(1, new_size);
    } else {
        new_data = (unsigned char *)realloc(*(ctx->buffer), new_size);
    }
    if (new_data == NULL) {
        return;
    }
    memcpy(new_data + *(ctx->size), data, size);
    *(ctx->buffer) = new_data;
    *(ctx->size) = new_size;
}

int float_compare(const void *a, const void *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    if (isnan(fa)) {
        return isnan(fb) ? 0 : 1;
    }
    if (isnan(fb)) {
        return -1;
    }

    return (fa > fb) - (fa < fb);
}

int setup_server_socket(int *server_socket)
{
    if (server_socket == NULL) {
        fprintf(stderr, "Invalid server_socket pointer\n");
        return EXIT_FAILURE;
    }

    *server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*server_socket == -1) {
        perror("Failed to create a server socket");
        return EXIT_FAILURE;
    }

    const int reuse = 1;
    if (setsockopt(*server_socket, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse)) == -1) {
        perror("Failed to make 0.0.0.0 reusable");
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }

#ifdef SO_REUSEPORT
    if (setsockopt(*server_socket, SOL_SOCKET, SO_REUSEPORT, (const char *) &reuse, sizeof(reuse)) == -1) {
        perror("Failed to make " TO_STRING(SERVER_PORT) " reusable");
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }
#endif

    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;

    if (setsockopt(*server_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        perror("Failed to set receive timeout");
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }

    if (setsockopt(*server_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        perror("Failed to set send timeout");
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }

    const int nodelay = 1;
    if (setsockopt(*server_socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) == -1) {
        perror("Failed to set TCP_NODELAY on server socket");
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(SERVER_PORT);

    if (bind(*server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) == -1) {
        perror("Failed to bind to 0.0.0.0:" TO_STRING(SERVER_PORT));
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }

    if (listen(*server_socket, MAX_QUEUED_CONNECTIONS) == -1) {
        perror("Failed to start listening for connections");
        close(*server_socket);
        *server_socket = -1;
        return EXIT_FAILURE;
    }

    puts("The server is listening on 0.0.0.0:" TO_STRING(SERVER_PORT));

    return EXIT_SUCCESS;
}

int receive_request(int request_socket, char *request_data, size_t max_size)
{
    ssize_t bytes_received = recv(request_socket, request_data, max_size - 1, 0);
    if (bytes_received == -1) {
        perror("Failed to receive the request data");
        return -1;
    }

    if (bytes_received >= 0) {
        request_data[bytes_received] = '\0';
    }

    return bytes_received;
}

void parse_request(const char *request_data, char *method, char *path)
{
    method[0] = '\0';
    path[0] = '\0';

    if (sscanf(request_data, "%9s %"TO_STRING(PATH_MAX)"s", method, path) < 2) {
        method[0] = '\0';
        path[0] = '\0';
        return;
    }

    method[9] = '\0';
    path[PATH_MAX] = '\0';
}

void cleanup_connection(int request_socket)
{
    if (shutdown(request_socket, SHUT_WR) == -1) {
        if (errno != ENOTCONN) {
            perror("Warning: Failed to shutdown write side of socket");
        }
    } else {
        char leftovers[1024];
        ssize_t bytes_read;
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        setsockopt(request_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        while ((bytes_read = recv(request_socket, leftovers, sizeof(leftovers), 0)) > 0) {}
        if (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ETIMEDOUT) {
            perror("Warning: Failed to drain the socket");
        }
        if (shutdown(request_socket, SHUT_RD) == -1) {
            if (errno != ENOTCONN) {
                perror("Warning: Failed to shutdown read side of socket");
            }
        }
    }

    if (close(request_socket) == -1) {
        perror("Warning: Failed to close the socket");
    }
}

void cleanup_resources(int file_handle, int request_socket, int server_socket, image_job_entry *job_table)
{
    if (file_handle != -1) {
        close(file_handle);
    }

    if (request_socket != -1) {
        cleanup_connection(request_socket);
    }

    if (server_socket != -1) {
        close(server_socket);
    }

    if (job_table != NULL) {
        for (int i = 0; i < shlenu(job_table); i++) {
            if (job_table[i].key != NULL) {
                free(job_table[i].key);
                job_table[i].key = NULL;
            }
            if (job_table[i].value.original_image != NULL) {
                free(job_table[i].value.original_image);
                job_table[i].value.original_image = NULL;
            }
            if (job_table[i].value.processed_image != NULL) {
                free(job_table[i].value.processed_image);
                job_table[i].value.processed_image = NULL;
            }
        }
        shfree(job_table);
    }
}

void apply_median_filter(unsigned char *img, unsigned char *filtered, int w, int h, int channels)
{
    int half_window = MEDIAN_WINDOW / 2;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < channels; c++) {
                float window[MEDIAN_WINDOW * MEDIAN_WINDOW];
                int window_idx = 0;

                for (int wy = -half_window; wy <= half_window; wy++) {
                    for (int wx = -half_window; wx <= half_window; wx++) {
                        int sx = x + wx;
                        int sy = y + wy;

                        int clamped_sx = sx < 0 ? 0 : (sx >= w ? w - 1 : sx);
                        int clamped_sy = sy < 0 ? 0 : (sy >= h ? h - 1 : sy);

                        window[window_idx++] = (float)img[(clamped_sy * w + clamped_sx) * channels + c];
                    }
                }

                qsort(window, MEDIAN_WINDOW * MEDIAN_WINDOW, sizeof(float), float_compare);
                filtered[(y * w + x) * channels + c] = (unsigned char)window[(MEDIAN_WINDOW * MEDIAN_WINDOW) / 2];
            }
        }
    }
}

void process_image(image_job_entry *job_table, const char *uuid_str)
{
    int idx = shgeti(job_table, uuid_str);
    if (idx == -1) {
        return;
    }

    image_job *job = &job_table[idx].value;

    if (!job->original_image || job->original_size == 0) {
        return;
    }

    int w, h, channels;
    unsigned char *img = stbi_load_from_memory(job->original_image, job->original_size, &w, &h, &channels, 0);
    if (!img) {
        return;
    }

    size_t alloc_size = (size_t)w * (size_t)h * (size_t)channels;
    if (alloc_size > SIZE_MAX / sizeof(unsigned char) || w <= 0 || h <= 0 || channels <= 0) {
        stbi_image_free(img);
        return;
    }

    unsigned char *filtered = calloc(w * h * channels, sizeof(unsigned char));
    if (!filtered) {
        stbi_image_free(img);
        return;
    }

    apply_median_filter(img, filtered, w, h, channels);

    unsigned char *out_buffer = NULL;
    size_t out_size = 0;

    buffer_context ctx = { &out_buffer, &out_size };
    stbi_write_png_to_func(write_image_callback, &ctx, w, h, channels, filtered, w * channels);

    free(filtered);
    stbi_image_free(img);
    if (job->processed_image) {
        free(job->processed_image);
        job->processed_image = NULL;
        job->processed_size = 0;
    }

    if (out_buffer) {
        job->processed_image = out_buffer;
        job->processed_size = out_size;
    }
}

int handle_post_images(int request_socket, const char *request_data, ssize_t bytes_received, image_job_entry **job_table)
{
    char *content_length_start = strstr(request_data, "Content-Length: ");
    if (!content_length_start) {
        char response_data[] = "HTTP/1.1 411 Length Required\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 411 response");
            return EXIT_FAILURE;
        }
        return 0;
    }
    char *endptr;
    errno = 0;
    size_t content_length = strtoul(content_length_start + 16, &endptr, 10);
    if (errno != 0 || *endptr != '\r' || content_length == 0) {
        char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 400 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    if (content_length > MAX_IMAGE_SIZE) {
        char response_data[] = "HTTP/1.1 413 Payload Too Large\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 413 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    char *body_start = strstr(request_data, "\r\n\r\n");
    if (!body_start) {
        char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 400 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    body_start += 4;
    size_t header_size = body_start - request_data;

    if (header_size > (size_t)bytes_received) {
        char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 400 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    size_t initial_body_size = bytes_received - header_size;

    unsigned char *image_buffer = NULL;
    image_buffer = calloc(1, content_length);
    if (!image_buffer) {
        char response_data[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 500 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    if (initial_body_size > content_length) {
        initial_body_size = content_length;
    }

    memcpy(image_buffer, body_start, initial_body_size);
    size_t total_image_size = initial_body_size;

    while (total_image_size < content_length) {
        size_t remaining = content_length - total_image_size;
        ssize_t bytes_received = recv(request_socket, image_buffer + total_image_size, remaining, 0);
        if (bytes_received < 0) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                perror("Timeout while receiving image data");
                free(image_buffer);
                return 0;
            } else {
                perror("Error receiving image data");
                free(image_buffer);
                return 0;
            }
        } else if (bytes_received == 0) {
            fprintf(stderr, "Client disconnected during image upload\n");
            free(image_buffer);
            return 0;
        }
        total_image_size += bytes_received;
    }

    uuid_t uuid;
    uuid_generate(uuid);

    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);

    image_job new_job = {
        .original_image = image_buffer,
        .original_size = total_image_size,
        .processed_image = NULL,
        .processed_size = 0
    };

    char *key_copy = strdup(uuid_str);
    if (!key_copy) {
        free(image_buffer);
        char response_data[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 500 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    shput(*job_table, key_copy, new_job);

    char response_header[256];
    int written = snprintf(response_header, sizeof(response_header), "HTTP/1.1 202 Accepted\r\nLocation: /images/%s/\r\n\r\n", uuid_str);
    if (written < 0 || written >= sizeof(response_header)) {
        char response_data[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 500 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    if (send_all(request_socket, response_header, written, 0) == -1) {
        perror("Failed to send the 202 response");
        return EXIT_FAILURE;
    }

    process_image(*job_table, uuid_str);

    return 0;
}

int handle_get_image(int request_socket, const char *path, image_job_entry *job_table)
{
    char uuid_str[37] = {0};
    if (sscanf(path, "/images/%36[0-9a-f-]", uuid_str) != 1 || 
        strlen(uuid_str) != 36) {
        char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 400 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    if (uuid_str[8] != '-' || uuid_str[13] != '-' || uuid_str[18] != '-' || uuid_str[23] != '-') {
        char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 400 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    int idx = shgeti(job_table, uuid_str);
    if (idx == -1) {
        char response_data[] = "HTTP/1.1 404 Not Found\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 404 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    image_job job = shget(job_table, uuid_str);
    if (!job.processed_image) {
        char response_data[] = "HTTP/1.1 202 Accepted\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 202 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    char response_header[] = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n\r\n";
    if (send_all(request_socket, response_header, sizeof(response_header) - 1, 0) == -1) {
        perror("Failed to send the 200 response header");
        return EXIT_FAILURE;
    }

    if (send_all(request_socket, job.processed_image, job.processed_size, 0) == -1) {
        perror("Failed to send the processed image");
        return EXIT_FAILURE;
    }

    return 0;
}

int handle_get_static_file(int request_socket, const char *path, const char *server_dir_path, size_t server_dir_path_len, int *file_to_serve_handle)
{
    if (strstr(path, "..") != NULL) {
        char response_data[] = "HTTP/1.1 403 Forbidden\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 403 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    if (path[0] != '/') {
        char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 400 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    char file_name[NAME_MAX + 1] = {0};
    size_t path_len = strlen(path);

    if (path_len > NAME_MAX) {
        char response_data[] = "HTTP/1.1 414 URI Too Long\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 414 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    strncpy(file_name, path, NAME_MAX);
    file_name[NAME_MAX] = '\0';

    if (strcmp(file_name, "/") == 0) {
        strncat(file_name, "index.html", NAME_MAX - strlen(file_name));
    }

    const char* file_path_part = file_name;
    if (file_path_part[0] == '/') {
        file_path_part++;
    }

    char file_path[PATH_MAX + 1] = {0};
    int path_len_required = snprintf(file_path, PATH_MAX, "%s/%s", server_dir_path, file_path_part);

    if (path_len_required < 0 || path_len_required >= PATH_MAX) {
        char response_data[] = "HTTP/1.1 414 URI Too Long\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 414 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    char resolved_path[PATH_MAX + 1] = {0};
    if (realpath(file_path, resolved_path) == NULL) {
        char response_data[] = "HTTP/1.1 404 Not Found\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 404 response");
            return EXIT_FAILURE;
        }
        return 0;
    }
    if (strncmp(resolved_path, server_dir_path, server_dir_path_len) != 0) {
        char response_data[] = "HTTP/1.1 403 Forbidden\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 403 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    *file_to_serve_handle = open(resolved_path, O_RDONLY);
    if (*file_to_serve_handle == -1) {
        char response_data[] = "HTTP/1.1 404 Not Found\r\n\r\n";
        if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
            perror("Failed to send the 404 response");
            return EXIT_FAILURE;
        }
        return 0;
    }

    char response_data[] = "HTTP/1.1 200 OK\r\n\r\n";
    if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
        perror("Failed to send the response header");
        return EXIT_FAILURE;
    }

    char file_data[MAX_REQUEST_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = read(*file_to_serve_handle, file_data, MAX_REQUEST_SIZE)) > 0) {
        if (send_all(request_socket, file_data, bytes_read, 0) == -1) {
            perror("Failed to send the requested file");
            return EXIT_FAILURE;
        }
    }

    if (bytes_read == -1) {
        perror("Failed to read the requested file");
        return EXIT_FAILURE;
    }

    close(*file_to_serve_handle);
    *file_to_serve_handle = -1;

    return 0;
}

int send_not_implemented(int request_socket)
{
    char response_data[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
    if (send_all(request_socket, response_data, sizeof(response_data) - 1, 0) == -1) {
        perror("Failed to send the 501 response");
        return EXIT_FAILURE;
    }
    return 0;
}

int set_client_socket_options(int client_socket)
{
    if (client_socket < 0) {
        return EXIT_FAILURE;
    }

    const int tcp_nodelay = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay)) == -1) {
        perror("Failed to set TCP_NODELAY");
        return EXIT_FAILURE;
    }

    struct timeval timeout;
    timeout.tv_sec = 15;
    timeout.tv_usec = 0;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        perror("Failed to set client receive timeout");
        return EXIT_FAILURE;
    }

    if (setsockopt(client_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == -1) {
        perror("Failed to set client send timeout");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    int program_status = EXIT_SUCCESS;

    int server_socket  = -1;
    int request_socket = -1;
    int file_to_serve_handle = -1;

    image_job_entry *job_table = NULL;

    char server_dir_path[PATH_MAX + 1] = {0};
    if (realpath(SERVER_DIR, server_dir_path) == NULL) {
        perror("Failed to resolve the " SERVER_DIR " into an absolute path");
        program_status = EXIT_FAILURE;
        goto end;
    }
    server_dir_path[PATH_MAX]  = '\0';
    size_t server_dir_path_len = strlen(server_dir_path);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("Failed to ignore SIGPIPE");
        program_status = EXIT_FAILURE;
        goto end;
    }

    if (setup_server_socket(&server_socket) != EXIT_SUCCESS) {
        program_status = EXIT_FAILURE;
        goto end;
    }

    while (true) {
        struct sockaddr_in client_address;
        socklen_t client_address_size = sizeof(client_address);
        memset(&client_address, 0, client_address_size);
        request_socket = accept(server_socket, (struct sockaddr *) &client_address, &client_address_size);
        if (request_socket == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("Failed to accept a new connection");
            program_status = EXIT_FAILURE;
            goto end;
        }

        if (set_client_socket_options(request_socket) != EXIT_SUCCESS) {
            cleanup_connection(request_socket);
            request_socket = -1;
            continue;
        }

        char request_data[MAX_REQUEST_SIZE + 1] = {0};
        ssize_t bytes_received = receive_request(request_socket, request_data, MAX_REQUEST_SIZE);
        if (bytes_received == -1) {
            cleanup_connection(request_socket);
            request_socket = -1;
            continue;
        } 
        if (bytes_received == 0) {
            cleanup_connection(request_socket);
            request_socket = -1;
            continue;
        }

        char method[10] = {0};
        char path[PATH_MAX + 1] = {0};
        parse_request(request_data, method, path);
        if (method[0] == '\0' || path[0] == '\0') {
            char response_data[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
            send_all(request_socket, response_data, sizeof(response_data) - 1, 0);
            cleanup_connection(request_socket);
            request_socket = -1;
            continue;
        }

        int result = EXIT_SUCCESS;
        if (strcmp(method, "POST") == 0 && strcmp(path, "/images") == 0) {
            result = handle_post_images(request_socket, request_data, bytes_received, &job_table);
        } else if (strcmp(method, "GET") == 0 && strncmp(path, "/images/", 8) == 0) {
            result = handle_get_image(request_socket, path, job_table);
        } else if (strcmp(method, "GET") == 0) {
            result = handle_get_static_file(request_socket, path, server_dir_path, server_dir_path_len, &file_to_serve_handle);
        } else {
            result = send_not_implemented(request_socket);
        }
        if (result == EXIT_FAILURE) {
            program_status = EXIT_FAILURE;
            goto end;
        }

        cleanup_connection(request_socket);
        request_socket = -1;
    }

end:
    cleanup_resources(file_to_serve_handle, request_socket, server_socket, job_table);

    return program_status;
}
