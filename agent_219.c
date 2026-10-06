#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define SID "9122"
#define AUTH_TOKEN "OPS-2219"
#define BUFFER_SIZE 1024

/*
 * Send all requested bytes.
 * send() is allowed to send fewer bytes than requested,
 * so we keep sending until everything is sent.
 */
int send_all(int sock_fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(sock_fd,
                            data + total_sent,
                            length - total_sent,
                            0);

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}

/*
 * Receive one complete line ending with '\n'.
 */
int recv_line(int sock_fd, char *buffer, size_t size)
{
    size_t position = 0;

    while (position < size - 1)
    {
        char ch;
        ssize_t received = recv(sock_fd, &ch, 1, 0);

        if (received == 0)
        {
            return 0;
        }

        if (received < 0)
        {
            return -1;
        }

        buffer[position++] = ch;

        if (ch == '\n')
        {
            break;
        }
    }

    buffer[position] = '\0';
    return (int)position;
}

double get_cpu_load(void)
{
    FILE *file;
    double load = 0.0;

    file = fopen("/proc/loadavg", "r");

    if (file == NULL)
    {
        return 0.0;
    }

    fscanf(file, "%lf", &load);

    fclose(file);

    return load;
}

long get_memory_used_mb(void)
{
    FILE *file;
    char label[64];
    long total = 0;
    long available = 0;

    file = fopen("/proc/meminfo", "r");

    if (file == NULL)
    {
        return 0;
    }

    while (fscanf(file, "%63s %ld kB", label, &available) == 2)
    {
        if (strcmp(label, "MemTotal:") == 0)
        {
            total = available;
        }
        else if (strcmp(label, "MemAvailable:") == 0)
        {
            break;
        }
    }

    fclose(file);

    return (total - available) / 1024;
}

long get_uptime_sec(void)
{
    FILE *file;
    double uptime = 0.0;

    file = fopen("/proc/uptime", "r");

    if (file == NULL)
    {
        return 0;
    }

    fscanf(file, "%lf", &uptime);

    fclose(file);

    return (long)uptime;
}

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /* Prepare server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    /* Bind to port 9410 */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    /* Listen for Controllers */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("RemoteOps Agent listening on port %d...\n", PORT);

    /* Accept one Controller for now */
    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_len);

    if (client_fd < 0)
    {
        perror("accept");
        close(server_fd);
        return 1;
    }

    printf("Controller connected!\n");

    int authenticated = 0;
    char buffer[BUFFER_SIZE];

    while (1)
    {
        int result = recv_line(client_fd, buffer, sizeof(buffer));

        if (result == 0)
        {
            printf("Controller disconnected.\n");
            break;
        }

        if (result < 0)
        {
            perror("recv");
            break;
        }

        /* Remove newline */
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received: %s\n", buffer);

        /*
         * AUTH command
         */
        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char token[100];

            if (sscanf(buffer + 5, "%99s", token) == 1 &&
                strcmp(token, AUTH_TOKEN) == 0)
            {
                authenticated = 1;

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "OK AUTHENTICATED SID:%s\n",
                         SID);

                send_all(client_fd,
                         response,
                         strlen(response));
            }
            else
            {
                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 001 AUTH_FAILED SID:%s\n",
                         SID);

                send_all(client_fd,
                         response,
                         strlen(response));
            }
        }

        /*
         * Reject everything else before authentication.
         */
        else if (!authenticated)
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 003 AUTH_REQUIRED SID:%s\n",
                     SID);

            send_all(client_fd,
                     response,
                     strlen(response));
        }

        /*
         * SYSINFO command
         */
        else if (strcmp(buffer, "SYSINFO") == 0)
       {
           double cpu_load = get_cpu_load();
           long memory_used_mb = get_memory_used_mb();
           long uptime_sec = get_uptime_sec();

           char response[BUFFER_SIZE];

           snprintf(response,
                    sizeof(response),
                    "OK SYSINFO %.2f %ld %ld SID:%s\n",
                    cpu_load,
                    memory_used_mb,
                    uptime_sec,
                    SID);

            send_all(client_fd,
                    response,
                    strlen(response));
         }

        /*
         * QUIT command
         */
        else if (strcmp(buffer, "QUIT") == 0)
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "OK BYE SID:%s\n",
                     SID);

            send_all(client_fd,
                     response,
                     strlen(response));

            break;
        }

        /*
         * Other commands will be implemented later.
         */
        else
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 999 UNKNOWN_COMMAND SID:%s\n",
                     SID);

            send_all(client_fd,
                     response,
                     strlen(response));
        }
    }

    close(client_fd);
    close(server_fd);

    return 0;
}
