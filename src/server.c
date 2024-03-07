#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <signal.h>
#include <errno.h>
#include "../include/constant.h"

void sigintHandler(int sig_num)
{
    /* Reset handler to catch SIGINT next time. */
    signal(SIGINT, sigintHandler);
    printf("\n Cannot be terminated using Ctrl+C \n");
    fflush(stdout);
}

// Define a global variable to keep track of the remaining targets
int remainingTargets = 0;
double width = boardSize * windowWidth;
double height = boardSize * windowHeight;
FILE *logFile;
int pipeServerWindowTar1[2];
int pipeServerWindowTar[2];
int pipeServerWindowObs[2];

pthread_t pipe_thread;
pthread_t target_thread;
pthread_t obstacle_thread;

// Function to send a "GE" message to the target client
void send_GE_message(int sockfd)
{
    char buffer[1024];
    sprintf(buffer, "GE");
    int n = write(sockfd, buffer, strlen(buffer));
     fprintf(logFile, "Message sent: %s\n", buffer);
     fflush(logFile);

    if (n < 0)
    {
        fprintf(stderr, "ERROR writing to socket l42\n");
        exit(EXIT_FAILURE);
    }
}

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

void parseTargets(char *targetMessage, int *numTargets, Point **targets)
{
    char *token = strtok(targetMessage, "]");
    // fprintf(stderr, "%s\n %d", token, strncmp(token, "T[", 2));
    // fprintf(stderr, "came here");
    if (strncmp(token, "T[", 2) != 0)
    {
        fprintf(stderr, "Error: Target message format incorrect l46\n");
        exit(1);
    }
    *numTargets = atoi(token + 2); // Skip "T[" and convert the remaining to integer
    *targets = malloc(*numTargets * sizeof(Point));
    if (*targets == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for targets\n");
        exit(1);
    }
    fprintf(stderr, "Number of Targets: %d\n", *numTargets);

    double y;
    double x;

    token = strtok(NULL, "|");
    for (int i = 0; i < *numTargets; ++i)
    {
        fprintf(stderr, "Target %d- ", i + 1);
        if (token == NULL)
        {
            fprintf(stderr, "Error: Target message format incorrect l59\n");
            exit(1);
        }
        // fprintf(stderr, "token: %s\n", token);

        sscanf(token, "%lf,%lf", &y, &x);
        fprintf(stderr, "x: %.3lf, y:%.3lf\n", x, y);

        // fprintf(stderr, "y: %lf\n", y);

        (*targets)[i].x = x;

        (*targets)[i].y = y;
        fprintf(stderr, "x, y= %lf, %lf \n", (*targets)[i].x, (*targets)[i].y);
        (*targets)[i].number = rand() % 10 + 1; // Assign a random target number between 1 and 10
        token = strtok(NULL, "|");
    }
}

void parseObstacles(char *obstacleMessage, int *numObstacles, Point **obstacles)
{
    char *token = strtok(obstacleMessage, "]");
    // fprintf(stderr, "%s\n %d", token, strncmp(token, "T[", 2));
    // fprintf(stderr, "came here");
    if (strncmp(token, "O[", 2) != 0)
    {
        fprintf(stderr, "Error: obstacles message format incorrect l46\n");
        exit(1);
    }
    *numObstacles = atoi(token + 2); // Skip "T[" and convert the remaining to integer
    *obstacles = malloc(*numObstacles * sizeof(Point));
    if (*obstacles == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for targets\n");
        exit(1);
    }
    fprintf(stderr, "Number of Obstacles: %d\n", *numObstacles);

    double y;
    double x;

    token = strtok(NULL, "|");
    for (int i = 0; i < *numObstacles; ++i)
    {
        fprintf(stderr, "Obstacle %d- ", i + 1);
        if (token == NULL)
        {
            fprintf(stderr, "Error: Obstacles message format incorrect l59\n");
            exit(1);
        }
        // fprintf(stderr, "token: %s\n", token);

        sscanf(token, "%lf,%lf", &y, &x);
        fprintf(stderr, "x: %.3lf, y:%.3lf\n", x, y);

        // fprintf(stderr, "y: %lf\n", y);

        (*obstacles)[i].x = x;

        (*obstacles)[i].y = y;
        token = strtok(NULL, "|");
    }
}

void *thread_read_pipe(void *args)
{
    ThreadData *data = (ThreadData *)args;
    char *tempbuffer = data->buffer;
    char buffer[BUFFER_SIZE];

    strcpy(buffer, tempbuffer);
    int newsockfd = data->newsockfd;
    // fprintf(stderr, "thread is running \n");

    // Read the struct from the pipe with a loop to handle cases where data is not immediately available
    TargetInfo receivedInfo;
    int bytes_read;
    while (true)
    {

        while ((bytes_read = read(pipeServerWindowTar1[0], &receivedInfo, sizeof(receivedInfo))) < 0)
        {
            if (errno == EINTR)
            {
                // Read was interrupted by a signal, retry the operation
                fprintf(stderr, "reading from pipe interrupted");
                continue;
            }
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No data available, wait for a short period and retry
                sleep(1); // Sleep for 1 second
            }
            else
            {
                perror("Error reading from pipeServerWindowTar");
                break;
            }
            fprintf(stderr, "bytes_read: %d", bytes_read);
        }

        // Check if data was successfully read
        if (bytes_read > 0)
        {
            // Print the received target information
            // printf("Number of targets: %d\n", receivedInfo.numTargets);
            // printf("Number of removed targets: %d\n", receivedInfo.numTargetsReached);
        }
        remainingTargets = receivedInfo.numTargets - receivedInfo.numTargetsReached;
        // Check if the remaining targets count is 4, if yes, send "GE" message to the client
        if (remainingTargets == 4)
        {
            fprintf(stderr, "Targets Reached\n");
            send_GE_message(newsockfd);
            fprintf(stderr, "Sent GE message to target client\n");
            fprintf(logFile, "Message sent: %s\n", buffer);
            fflush(logFile);

            // Receive echo message from the client
            memset(buffer, 0, sizeof(buffer)); // Clear buffer
            read(newsockfd, buffer, sizeof(buffer));
            fprintf(stderr, "ECHO received: %s\n", buffer);
            fprintf(logFile, "ECHO received: %s\n", buffer);
            fflush(logFile);

            // Receive  message from the client
            memset(buffer, 0, sizeof(buffer)); // Clear buffer
            read(newsockfd, buffer, sizeof(buffer));
            fprintf(logFile, "Message received: %s\n", buffer);
            fflush(logFile);

            fprintf(stderr, "Received target message: %s\n", buffer);
            fprintf(stderr, "Parsing target message...\n");
            write(newsockfd, buffer, sizeof(buffer));
            fprintf(logFile, "ECHO sent: %s\n", buffer);
            fflush(logFile);

            int numTargets;
            Point *targets = NULL;
            parseTargets(buffer, &numTargets, &targets);

            // Store targets in pipes
            write(pipeServerWindowTar[1], &numTargets, sizeof(int)); // Write the number of targets to the pipe
            for (int i = 0; i < numTargets; ++i)
            {
                write(pipeServerWindowTar[1], &(targets[i]), sizeof(Point)); // Write each target to the pipe
            }
        }
    }

    // close(newsockfd);
}

void *thread_target_function(void *args)
{
    //fprintf(stderr, "target thread starting to execute \n");
    ThreadData *threadData = (ThreadData *)args;

    int newsockfd = threadData->newsockfd;
    // fprintf(stderr, "executed this - newsockfd: %d\n", newsockfd);
    char *tempbuffer = threadData->buffer;

    char buffer[BUFFER_SIZE];
    strcpy(buffer, tempbuffer);

    // fprintf(stderr, "tempbuffer - %s \n", tempbuffer);
    // fprintf(stderr, "buffer - %s \n", buffer);

    write(newsockfd, buffer, sizeof(buffer));
    fprintf(logFile, "ECHO sent: %s\n", buffer);
    fflush(logFile);


    // Send window width and height to the client
    sprintf(buffer, "%.3lf, %.3lf", height, width);
    int n = write(newsockfd, buffer, strlen(buffer));
    fprintf(logFile, "Message sent: %s\n", buffer);
    fflush(logFile);
    
    if (n < 0)
    {
        fprintf(stderr, "ERROR writing to socket l219\n");
        close(newsockfd);
        return 0;
    }

    memset(buffer, 0, sizeof(buffer)); // Clear buffer
    read(newsockfd, buffer, sizeof(buffer));
    fprintf(stderr, "ECHO received: %s\n", buffer);
    fprintf(logFile, "ECHO received: %s\n", buffer);
    fflush(logFile);

    // Receive target message from the client
    memset(buffer, 0, sizeof(buffer)); // Clear buffer
    n = read(newsockfd, buffer, 1024);
    fprintf(logFile, "Message received: %s\n", buffer);
    fflush(logFile);

    if (n < 0)
    {
        fprintf(stderr, "ERROR reading from socket 275 \n");
        close(newsockfd);
        return 0;
    }

    write(newsockfd, buffer, sizeof(buffer));
    fprintf(logFile, "ECHO sent: %s\n", buffer);
    fflush(logFile);
    
    printf("Received target message: %s\n", buffer);
    printf("Parsing target message...\n");

    int numTargets;
    Point *targets = NULL;
    parseTargets(buffer, &numTargets, &targets);

    // Write received targets to the log file
    fprintf(logFile, "Number of Targets: %d\n", numTargets);
    fflush(logFile);
    fprintf(stderr, "Number of Targets: %d\n", numTargets);
    for (int i = 0; i < numTargets; ++i)
    {
        fprintf(logFile, "Target %d: x=%.3lf, y=%.3lf\n", i + 1, targets[i].x, targets[i].y);
        // printf("Target %d: x=%d, y=%d\n", i+1, targets[i].x, targets[i].y);
    }
    fflush(logFile);

    // Store targets in pipes
    write(pipeServerWindowTar[1], &numTargets, sizeof(int)); // Write the number of targets to the pipe
    for (int i = 0; i < numTargets; ++i)
    {
        fprintf(stderr, "x, y = %lf, %lf \n", targets[i].x, targets[i].y);
        write(pipeServerWindowTar[1], &targets[i], sizeof(Point)); // Write each target to the pipe
    }
    
    free(targets);

    ThreadData threadData2;
    threadData2.newsockfd = newsockfd;
    threadData2.buffer = buffer;
    if (pthread_create(&pipe_thread, NULL, thread_read_pipe, (void *)&threadData2) == 0)
    {
        fprintf(stderr, "starting a thread");
    }
    else
    {
        fprintf(stderr, "error occured while creating thread.");
    }
    pthread_join(target_thread, NULL);

    fprintf(stderr, "target thread finished");

    pthread_exit(NULL);
}

void *obstacle_thread_function(void *args)
{
    pthread_join(obstacle_thread, NULL);

    fprintf(stderr, "obstacle thread starting to execute \n");
    ThreadData *threadData = (ThreadData *)args;

    int newsockfd = threadData->newsockfd;
    fprintf(stderr, "executed this - newsockfd: %d\n", newsockfd);
    char *tempbuffer = threadData->buffer;

    char buffer[BUFFER_SIZE];
    strcpy(buffer, tempbuffer);
    int n;
    while (true)
    {
        // Receive obstacle message from the client
        memset(buffer, 0, sizeof(buffer)); // Clear buffer
        n = read(newsockfd, buffer, 1024);
        fprintf(logFile, "Message received: %s\n", buffer);
        fflush(logFile);

        if (n < 0)
        {
            fprintf(stderr, "ERROR reading from socket - l354\n");
            close(newsockfd);
            continue;
        }
        fprintf(stderr, "Received obstacle message: %s\n", buffer);

        fprintf(stderr, "echoing obstacle message %s", buffer);
        write(newsockfd, buffer, sizeof(buffer));
        fprintf(logFile, "ECHO sent: %s\n", buffer);
        fflush(logFile);

        fprintf(stderr, "Parsing obstacle message...\n");

        int numObstacles;
        Point *obstacles = NULL;
        parseObstacles(buffer, &numObstacles, &obstacles);

        // Write received obstacles to the log file and terminal
        fprintf(logFile, "Number of Obstacles: %d\n", numObstacles);
        fflush(logFile);
        // printf("Number of Obstacles: %d\n", numObstacles);
        for (int i = 0; i < numObstacles; ++i)
        {
            fprintf(logFile, "Obstacle %d: x=%.3lf, y=%.3lf\n", i + 1, obstacles[i].x, obstacles[i].y);
            // printf("Obstacle %d: x=%d, y=%d\n", i+1, obstacles[i].x, obstacles[i].y);
        }
        fflush(logFile);

        // Store obstacles in pipes
        write(pipeServerWindowObs[1], &numObstacles, sizeof(int)); // Write the number of obstacles to the pipe
        for (int i = 0; i < numObstacles; ++i)
        {
            write(pipeServerWindowObs[1], &(obstacles[i]), sizeof(Point)); // Write each obstacle to the pipe
        }

        free(obstacles);
        // close(newsockfd);
    }
}

int main(int argc, char *argv[])
{

    // Signal handling for watchdog
    struct sigaction sig_act;
    sig_act.sa_sigaction = handleSignal;
    sig_act.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &sig_act, NULL);
    sigaction(SIGUSR1, &sig_act, NULL);

    // Pipes
    pid_t serverPID;
    serverPID = getpid();
    int pipeWatchdogServer[2];

    close(pipeServerWindowObs[0]);
    close(pipeServerWindowTar[0]);
    close(pipeServerWindowTar1[1]);

    if (pipe(pipeServerWindowTar) == -1 || pipe(pipeServerWindowObs) == -1 || pipe(pipeServerWindowTar1) == -1)
    {
        perror("pipeServerWindow");
        exit(EXIT_FAILURE);
    }

    sscanf(argv[1], "%d %d|%d %d|%d %d|%d %d", &pipeWatchdogServer[0], &pipeWatchdogServer[1], &pipeServerWindowTar[0], &pipeServerWindowTar[1], &pipeServerWindowObs[0], &pipeServerWindowObs[1], &pipeServerWindowTar1[0], &pipeServerWindowTar1[1]);

    close(pipeWatchdogServer[0]);
    close(pipeServerWindowObs[0]);
    close(pipeServerWindowTar[0]);
    close(pipeServerWindowTar1[1]);

    if (write(pipeWatchdogServer[1], &serverPID, sizeof(serverPID)) == -1)
    {
        perror("write pipeWatchdogServer");
        exit(EXIT_FAILURE);
    }
    printf("%d\n", serverPID);
    close(pipeWatchdogServer[1]); // Closing unnecessary pipes

    // LOG FILE SETUP
    char logFilePath[100];
    snprintf(logFilePath, sizeof(logFilePath), "log/ServerLog.txt");
    logFile = fopen(logFilePath, "w");

    if (logFile == NULL)
    {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    // SHARED MEMORY AND SEMAPHORE SETUP
    double position[6] = {boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2};
    int sharedSegSize = (sizeof(position));

    sem_t *semaphoreID = sem_open(SEM_PATH, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (semaphoreID == SEM_FAILED)
    {
        perror("sem_open");
        fclose(logFile);
        exit(EXIT_FAILURE);
    }
    sem_init(semaphoreID, 1, 0);

    int shmFD = shm_open(SHM_PATH, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG);
    if (shmFD < 0)
    {
        perror("shm_open");
        fclose(logFile);
        sem_close(semaphoreID);
        sem_unlink(SEM_PATH);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shmFD, sharedSegSize) == -1)
    {
        perror("ftruncate");
        fclose(logFile);
        sem_close(semaphoreID);
        sem_unlink(SEM_PATH);
        shm_unlink(SHM_PATH);
        exit(EXIT_FAILURE);
    }
    void *shmPointer = mmap(NULL, sharedSegSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFD, 0);
    if (shmPointer == MAP_FAILED)
    {
        perror("mmap");
        fclose(logFile);
        sem_close(semaphoreID);
        sem_unlink(SEM_PATH);
        shm_unlink(SHM_PATH);
        exit(EXIT_FAILURE);
    }
    sem_post(semaphoreID);

    // Socket setup
    int sockfd, newsockfd;
    socklen_t clilen;
    char buffer[1024];
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    printf("Starting server...\n");

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    printf("Socket created successfully.\n");

    // Clear the memory of serv_addr struct
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    printf("Binding to port %d...\n", portno);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    printf("Listening for incoming connections...\n");
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    printf("Waiting for a client to connect...\n");

    while (1)
    {
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0)
        {
            if (errno == EINTR)
            {
                continue; // Retry accept
            }
            else
            {
                error("ERROR on accept");
            }
        }

        printf("Client connected. Handling request...\n");

        memset(buffer, 0, sizeof(buffer)); // Clear buffer
        // printf("newsockfd: %d", newsockfd);
        n = read(newsockfd, buffer, 1024);
        fprintf(logFile, "Message received: %s\n", buffer);
        fflush(logFile);

        if (n < 0)
        {
            fprintf(stderr, "ERROR reading from socket246\n");
            close(newsockfd);
            continue;
        }

        if (strncmp(buffer, "TI", 2) == 0)
        {
            printf("Target message received: %s\n", buffer);
            ThreadData threadData;
            threadData.buffer = buffer;
            threadData.newsockfd = newsockfd;
            //fprintf(stderr, "buffer outside: %s", buffer);
            if (pthread_create(&target_thread, NULL, thread_target_function, (void *)&threadData) == 0)
            {
              //  fprintf(stderr, "target thread created!");
            }
            else
            {
                fprintf(stderr, "target thread creation failure");
            }
        }
        else if (strncmp(buffer, "OI", 2) == 0)
        {
            ThreadData threadData;
            threadData.buffer = buffer;
            threadData.newsockfd = newsockfd;
            if (pthread_create(&obstacle_thread, NULL, obstacle_thread_function, (void *)&threadData) == 0)
            {
               // fprintf(stderr, "obstacle thread created!");
            }
            else
            {
                fprintf(stderr, "obstacle thread creation failure");
            }
            fprintf(stderr, "obstacle message received: %s \n", buffer);
            printf("Obstacle message received: %s\n", buffer);
            write(newsockfd, buffer, sizeof(buffer));
            fprintf(logFile, "ECHO sent: %s\n", buffer);
            fflush(logFile);

            // Send window width and height to the client
            sprintf(buffer, "%.3lf, %.3lf", height, width);
            n = write(newsockfd, buffer, strlen(buffer));
            fprintf(logFile, "Message sent: %s\n", buffer);
            fflush(logFile);
            
            if (n < 0)
            {
                fprintf(stderr, "ERROR writing to socket\n");
                close(newsockfd);
                continue;
            }

            memset(buffer, 0, sizeof(buffer)); // Clear buffer

            read(newsockfd, buffer, sizeof(buffer));
            printf("ECHO received: %s\n", buffer);
        }
        pthread_join(pipe_thread, NULL);

        fprintf(stderr, "Connection with client closed.\n");
        sleep(1);
    }

    // CLEANUP
    shm_unlink(SHM_PATH);
    sem_close(semaphoreID);
    sem_unlink(SEM_PATH);
    munmap(shmPointer, sharedSegSize);

    // Close the log file
    fclose(logFile);

    close(pipeServerWindowTar[1]);
    close(pipeServerWindowObs[1]);
    close(pipeServerWindowTar1[0]);

    return 0;
}
