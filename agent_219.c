#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define PORT 9410
#define SID "9122"
#define AUTH_TOKEN "OPS-2219"
#define BUFFER_SIZE 32768
#define STORAGE_DIR "./agentfiles/IT24102219"
#define MAX_FILE_SIZE (10 * 1024 * 1024)
#define MONITOR_INTERVAL 2

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

void get_process_list(char *output, size_t output_size)
{
    FILE *process_file;
    char line[256];
    size_t used = 0;

    output[0] = '\0';

    process_file = popen("ps -e -o pid= -o comm=", "r");

    if (process_file == NULL)
    {
        snprintf(output, output_size, "PROCESS_LIST_ERROR");
        return;
    }

    while (fgets(line, sizeof(line), process_file) != NULL)
    {
        int pid;
        char process_name[128];

        if (sscanf(line, "%d %127s", &pid, process_name) == 2)
        {
            int written;

            written = snprintf(output + used,
                               output_size - used,
                               "%s%d/%s",
                               used == 0 ? "" : ",",
                               pid,
                               process_name);

            if (written < 0 ||
                (size_t)written >= output_size - used)
            {
                break;
            }

            used += (size_t)written;
        }
    }

    pclose(process_file);
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

volatile int monitor_running = 0;
pthread_t monitor_thread;

struct monitor_info
{
    char client_ip[INET_ADDRSTRLEN];
    int udp_port;
};

void *monitor_function(void *arg)
{
    struct monitor_info *info =
        (struct monitor_info *)arg;

    int udp_socket;

    struct sockaddr_in udp_addr;

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_socket < 0)
    {
        perror("UDP socket");
        monitor_running = 0;
        free(info);
        return NULL;
    }

    memset(&udp_addr, 0, sizeof(udp_addr));

    udp_addr.sin_family = AF_INET;
    udp_addr.sin_port = htons(info->udp_port);

    if (inet_pton(AF_INET,
                  info->client_ip,
                  &udp_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(udp_socket);
        monitor_running = 0;
        free(info);
        return NULL;
    }

    while (monitor_running)
    {
        double cpu_load = get_cpu_load();
        long memory_used_mb = get_memory_used_mb();
        long uptime_sec = get_uptime_sec();

        char message[BUFFER_SIZE];

        snprintf(message,
                 sizeof(message),
                 "SYSINFO %.2f %ld %ld SID:%s",
                 cpu_load,
                 memory_used_mb,
                 uptime_sec,
                 SID);

        sendto(udp_socket,
               message,
               strlen(message),
               0,
               (struct sockaddr *)&udp_addr,
               sizeof(udp_addr));

        sleep(MONITOR_INTERVAL);
    }

    close(udp_socket);
    free(info);

    return NULL;
}

int monitor_active = 0;
pthread_t monitor_thread;


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
          * LISTPROC command
          */
         else if (strcmp(buffer, "LISTPROC") == 0)
        {
          char process_list[BUFFER_SIZE];

          get_process_list(process_list,
                     sizeof(process_list));

          char response[BUFFER_SIZE];

          snprintf(response,
                    sizeof(response),
                   "OK PROCS %s SID:%s\n",
                   process_list,
                   SID);

          send_all(client_fd,
                   response,
                   strlen(response));
         }

          /*
           * PUT command
           */
          else if (strncmp(buffer, "PUT ", 4) == 0)
         {
               char filename[256];
               long file_size;

               if (sscanf(buffer + 4, "%255s %ld", filename, &file_size) != 2)
               {
                  char response[BUFFER_SIZE];

                  snprintf(response,
                           sizeof(response),
                           "ERR 004 FILE_TOO_LARGE SID:%s\n",
                           SID);

                  send_all(client_fd, response, strlen(response));
          }
          else if (file_size < 0 || file_size > MAX_FILE_SIZE ||
                   strchr(filename, '/') != NULL ||
                   strstr(filename, "..") != NULL)
          {
               char response[BUFFER_SIZE];

               snprintf(response,
                        sizeof(response),
                        "ERR 004 FILE_TOO_LARGE SID:%s\n",
                        SID);

               send_all(client_fd, response, strlen(response));
           }
           else
           {
               char path[BUFFER_SIZE];

               snprintf(path,
                        sizeof(path),
                        "%s/%s",
                        STORAGE_DIR,
                        filename);

               FILE *file = fopen(path, "wb");

               if (file == NULL)
               {
                  char response[BUFFER_SIZE];

                  snprintf(response,
                           sizeof(response),
                           "ERR 999 FILE_WRITE_ERROR SID:%s\n",
                           SID);

                  send_all(client_fd, response, strlen(response));
               }
               else
               {
                  int transfer_ok =
                      recv_all(client_fd, NULL, 0);

                  if (file_size > 0)
                  {
                      char file_buffer[4096];
                      long remaining = file_size;

                      transfer_ok = 0;

                      while (remaining > 0)
                      {
                           size_t chunk_size =
                                remaining < (long)sizeof(file_buffer)
                                    ? (size_t)remaining
                                    : sizeof(file_buffer);

                           ssize_t received =
                                recv(client_fd,
                                     file_buffer,
                                     chunk_size,
                                     0);

                           if (received <= 0)
                           {
                               transfer_ok = -1;
                               break;
                            }

                           if (fwrite(file_buffer,
                                      1,
                                      (size_t)received,
                                      file) != (size_t)received)
                            {
                               transfer_ok = -1;
                               break;
                            }

                            remaining -= received;
                          }
                       }

                       fclose(file);

                       if (transfer_ok == 0)
                       {
                           char response[BUFFER_SIZE];

                           snprintf(response,
                                    sizeof(response),
                                    "OK FILE_RECEIVED %s SID:%s\n",
                                    filename,
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
                                    "ERR 999 FILE_TRANSFER_ERROR SID:%s\n",
                                    SID);

                           send_all(client_fd,
                                    response,
                                    strlen(response));
                        }
                     }
                  }
               }

          /*
           * GET command
           */
           else if (strncmp(buffer, "GET ", 4) == 0)
           {
                char filename[256];

                if (sscanf(buffer + 4, "%255s", filename) != 1 ||
                    strchr(filename, '/') != NULL ||
                    strstr(filename, "..") != NULL)
                {
                    char response[BUFFER_SIZE];

                    snprintf(response,
                             sizeof(response),
                             "ERR 005 FILE_NOT_FOUND SID:%s\n",
                             SID);

                    send_all(client_fd,
                             response,
                             strlen(response));
                }
                else
                {
                    char path[BUFFER_SIZE];

                    snprintf(path,
                             sizeof(path),
                             "%s/%s",
                             STORAGE_DIR,
                             filename);

                    FILE *file = fopen(path, "rb");

                    if (file == NULL)
                    {
                       char response[BUFFER_SIZE];

                       snprintf(response,
                                sizeof(response),
                                "ERR 005 FILE_NOT_FOUND SID:%s\n",
                                SID);

                       send_all(client_fd,
                                response,
                                strlen(response));
                 }
                 else
                 {
                      fseek(file, 0, SEEK_END);
                      long file_size = ftell(file);
                      rewind(file);

                      if (file_size < 0 || file_size > MAX_FILE_SIZE)
                      {
                          fclose(file);

                          char response[BUFFER_SIZE];

                          snprintf(response,
                                   sizeof(response),
                                   "ERR 004 FILE_TOO_LARGE SID:%s\n",
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
                                   "OK FILE_SEND %s %ld SID:%s\n",
                                   filename,
                                   file_size,
                                   SID);

                          if (send_all(client_fd,
                                       response,
                                       strlen(response)) == 0)
                          {
                              if (send_file(client_fd,
                                            file,
                                            (size_t)file_size) < 0)
                              {
                                  printf("File send failed.\n");
                              }
                           }

                           fclose(file);
                        }
                     }
                  }
               }

         /*
          * EXEC command
          */
          else if (strncmp(buffer, "EXEC ", 5) == 0)
          {
               char command_name[100];
               char exec_output[2048];
               const char *system_command = NULL;

               if (sscanf(buffer + 5, "%99s", command_name) != 1)
               {
                    char response[BUFFER_SIZE];

                    snprintf(response,
                             sizeof(response),
                             "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                             SID);

               send_all(client_fd, response, strlen(response));
           }
           else
           {
               /*
                * Fixed whitelist.
                * No user-supplied shell command is executed.
                */
                if (strcmp(command_name, "DATE") == 0)
                {
                     system_command = "date";
                }
                else if (strcmp(command_name, "UPTIME") == 0)
                {
                     system_command = "uptime -p";
                }
                else if (strcmp(command_name, "DISKFREE") == 0)
                {
                     system_command = "df -h / | awk 'NR==2 {print $4}'";
                }
                else if (strcmp(command_name, "HOSTNAME") == 0)
                {
                     system_command = "hostname";
                }
                else if (strcmp(command_name, "WHOAMI") == 0)
                {
                     system_command = "whoami";
                }
                else
                {
                     char response[BUFFER_SIZE];

                     snprintf(response,
                              sizeof(response),
                              "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                              SID);

                     send_all(client_fd, response, strlen(response));

                     system_command = NULL;
                }

                if (system_command != NULL)
                {
                    FILE *pipe;
                    pipe = popen(system_command, "r");

                    if (pipe == NULL)
                    {
                        char response[BUFFER_SIZE];

                        snprintf(response,
                                 sizeof(response),
                                 "ERR 999 EXEC_FAILED SID:%s\n",
                                 SID);

                        send_all(client_fd, response, strlen(response));
                     }
                     else
                     {
                          memset(exec_output, 0, sizeof(exec_output));

                          if (fgets(exec_output,
                                    sizeof(exec_output),
                                    pipe) != NULL)
                          {
                              exec_output[strcspn(exec_output, "\r\n")] = '\0';
                          }
                          else
                          {
                              strcpy(exec_output, "NO_OUTPUT");
                          }

                          pclose(pipe);

                          char response[BUFFER_SIZE];

                          snprintf(response,
                                   sizeof(response),
                                   "OK EXEC_RESULT %s SID:%s\n",
                                   exec_output,
                                   SID);

                          send_all(client_fd,
                                   response,
                                   strlen(response));
                   }
                }
             }
          }

        /*
         * MONITOR START command
         */
         else if (strncmp(buffer, "MONITOR START ", 14) == 0)
{
    int udp_port;

    if (sscanf(buffer + 14, "%d", &udp_port) != 1 ||
        udp_port < 1 ||
        udp_port > 65535)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 006 INVALID_UDP_PORT SID:%s\n",
                 SID);

        send_all(client_fd,
                 response,
                 strlen(response));
    }
    else if (monitor_running)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 007 MONITOR_ALREADY_RUNNING SID:%s\n",
                 SID);

        send_all(client_fd,
                 response,
                 strlen(response));
    }
    else
    {
        struct monitor_info *info =
            malloc(sizeof(struct monitor_info));

        if (info == NULL)
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 999 MONITOR_START_FAILED SID:%s\n",
                     SID);

            send_all(client_fd,
                     response,
                     strlen(response));
        }
        else
        {
            if (inet_ntop(AF_INET,
                          &client_addr.sin_addr,
                          info->client_ip,
                          sizeof(info->client_ip)) == NULL)
            {
                free(info);

                char response[BUFFER_SIZE];

                snprintf(response,
                         sizeof(response),
                         "ERR 999 MONITOR_START_FAILED SID:%s\n",
                         SID);

                send_all(client_fd,
                         response,
                         strlen(response));
            }
            else
            {
                info->udp_port = udp_port;

                monitor_running = 1;

                if (pthread_create(&monitor_thread,
                                   NULL,
                                   monitor_function,
                                   info) != 0)
                {
                    monitor_running = 0;
                    free(info);

                    char response[BUFFER_SIZE];

                    snprintf(response,
                             sizeof(response),
                             "ERR 999 MONITOR_START_FAILED SID:%s\n",
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
                             "OK MONITOR_STARTED SID:%s\n",
                             SID);

                    send_all(client_fd,
                             response,
                             strlen(response));
                }
            }
        }
    }
}

        /*
         * MONITOR STOP command
         */
         else if (strcmp(buffer, "MONITOR STOP") == 0)
{
    monitor_running = 0;

    if (pthread_join(monitor_thread, NULL) == 0)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "OK MONITOR_STOPPED SID:%s\n",
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
                 "ERR 999 MONITOR_STOP_FAILED SID:%s\n",
                 SID);

        send_all(client_fd,
                 response,
                 strlen(response));
    }
}

        /*
         * QUIT command
         */
        else if (strcmp(buffer, "QUIT") == 0)
        {

            if (monitor_running)
            {
                monitor_running = 0;
                pthread_join(monitor_thread, NULL);
            }

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
