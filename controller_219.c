#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410
#define BUFFER_SIZE 32768

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

int recv_all(int sock_fd, void *buffer, size_t length)
{
    size_t total_received = 0;

    while (total_received < length)
    {
        ssize_t received = recv(sock_fd,
                                (char *)buffer + total_received,
                                length - total_received,
                                0);

        if (received <= 0)
        {
            return -1;
        }

        total_received += (size_t)received;
    }

    return 0;
}

int send_file(int sock_fd, FILE *file, size_t file_size)
{
    char buffer[4096];
    size_t total_sent = 0;

    while (total_sent < file_size)
    {
        size_t remaining = file_size - total_sent;
        size_t chunk_size = remaining < sizeof(buffer)
                                ? remaining
                                : sizeof(buffer);

        size_t bytes_read = fread(buffer, 1, chunk_size, file);

        if (bytes_read == 0)
        {
            return -1;
        }

        if (send_all(sock_fd, buffer, bytes_read) < 0)
        {
            return -1;
        }

        total_sent += bytes_read;
    }

    return 0;
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

        /* Remove newline */
        command[strcspn(command, "\r\n")] = '\0';

        /*
         * PUT command
         */
        if (strncmp(command, "PUT ", 4) == 0)
        {
            char filename[256];
            long file_size;

            if (sscanf(command + 4,
                       "%255s %ld",
                       filename,
                       &file_size) != 2)
            {
                printf("Usage: PUT <filename> <filesize>\n");
                continue;
            }

            FILE *file = fopen(filename, "rb");

            if (file == NULL)
            {
                perror("Cannot open file");
                continue;
             }

             fseek(file, 0, SEEK_END);
             long actual_size = ftell(file);
             rewind(file);

             if (actual_size < 0 || actual_size != file_size)
             {
                 printf("File size mismatch. Actual size: %ld bytes\n",
                         actual_size);

                 fclose(file);
                 continue;
              }

              char header[BUFFER_SIZE];

              snprintf(header,
                       sizeof(header),
                       "PUT %s %ld\n",
                       filename,
                       file_size);

               if (send_all(sock_fd,
                            header,
                            strlen(header)) < 0)
               {
                   fclose(file);
                   break;
               }

               if (send_file(sock_fd,
                             file,
                             (size_t)file_size) < 0)
               {
                   fclose(file);
                   break;
               }

               fclose(file);

               int result = recv_line(sock_fd,
                                      response,
                                      sizeof(response));

               if (result <= 0)
               {
                   printf("Agent disconnected.\n");
                   break;
               }

               printf("Agent: %s", response);
               continue;
            }

           /*
            * GET command
            */
           if (strncmp(command, "GET ", 4) == 0)
           {
              if (send_all(sock_fd,
                           command,
                           strlen(command)) < 0)
              {
                  break;
              }

              if (send_all(sock_fd, "\n", 1) < 0)
              {
                  break;
              }

              int result = recv_line(sock_fd,
                                     response,
                                     sizeof(response));

              if (result <= 0)
              {
                  printf("Agent disconnected.\n");
                  break;
              }

              printf("Agent: %s", response);

              if (strncmp(response, "OK FILE_SEND ", 13) == 0)
              {
                  char filename[256];
                  long file_size;

                  if (sscanf(response + 13,
                             "%255s %ld",
                             filename,
                             &file_size) == 2)
                  {
                      char local_name[BUFFER_SIZE];

                      snprintf(local_name,
                               sizeof(local_name),
                               "downloaded_%s",
                               filename);

                      FILE *file = fopen(local_name, "wb");

                      if (file == NULL)
                      {
                         perror("Cannot create downloaded file");
                         break;
                      }

                      char buffer[4096];
                      long remaining = file_size;
                      int transfer_ok = 0;

                      while (remaining > 0)
                      {
                           size_t chunk_size =
                               remaining < (long)sizeof(buffer)
                                   ? (size_t)remaining
                                   : sizeof(buffer);

                           ssize_t received =
                               recv(sock_fd,
                                    buffer,
                                    chunk_size,
                                    0);

                           if (received <= 0)
                           {
                               transfer_ok = -1;
                               break;
                           }

                           if (fwrite(buffer,
                                      1,
                                      (size_t)received,
                                      file) != (size_t)received)
                           {
                               transfer_ok = -1;
                               break;
                           }

                           remaining -= received;
                       }

                       fclose(file);

                       if (transfer_ok == 0)
                       {
                           printf("File downloaded as: %s\n",
                           local_name);
                       }
                       else
                       {
                           printf("File download failed.\n");
                       }
                    }
                }

                continue;
             }

        /* Normal commands: AUTH, SYSINFO, LISTPROC, EXEC, QUIT */

        if (send_all(sock_fd,
                     command,
                     strlen(command)) < 0)
        {
            perror("send");
            break;
        }

        if (send_all(sock_fd, "\n", 1) < 0)
        {
            perror("send");
            break;
        }

        int normal_result = recv_line(sock_fd,
                               response,
                               sizeof(response));

        if (normal_result == 0)
        {
            printf("Agent disconnected.\n");
            break;
        }

        if (normal_result < 0)
        {
            perror("recv");
            break;
         }

         printf("Agent: %s", response);

         if (strcmp(command, "QUIT") == 0)
         {
             break;
         }

    }

    close(sock_fd);

    return 0;
}
