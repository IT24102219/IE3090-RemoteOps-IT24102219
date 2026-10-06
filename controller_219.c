#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410
#define BUFFER_SIZE 1024

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

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

    /* Create socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        return 1;
    }

    /* Prepare Agent address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        return 1;
    }

    /* Connect to Agent */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");

    char command[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (1)
    {
        printf("RemoteOps> ");

        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            break;
        }

        if (send_all(sock_fd,
                     command,
                     strlen(command)) < 0)
        {
            perror("send");
            break;
        }

        int result = recv_line(sock_fd,
                               response,
                               sizeof(response));

        if (result == 0)
        {
            printf("Agent disconnected.\n");
            break;
        }

        if (result < 0)
        {
            perror("recv");
            break;
        }

        printf("Agent: %s", response);

        if (strncmp(command, "QUIT", 4) == 0)
        {
            break;
        }
    }

    close(sock_fd);

    return 0;
}
