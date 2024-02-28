#define _POSIX_C_SOURCE 200809L

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


// Define a global variable to keep track of the remaining targets
int remainingTargets = 0;

// Function to send a "GE" message to the target client
void send_GE_message(int sockfd) {
    char buffer[256];
    sprintf(buffer, "GE");
    int n = write(sockfd, buffer, strlen(buffer));
    if (n < 0) {
        fprintf(stderr, "ERROR writing to socket\n");
        exit(EXIT_FAILURE);
    }
}

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void parseTargets(char *targetMessage, int *numTargets, Point **targets) {
    char *token = strtok(targetMessage, " ");
    if (token == NULL || strncmp(token, "T[", 2) != 0) {
        fprintf(stderr, "Error: Target message format incorrect\n");
        exit(1);
    }
    *numTargets = atoi(token + 2); // Skip "T[" and convert the remaining to integer
    *targets = malloc(*numTargets * sizeof(Point));
    if (*targets == NULL) {
        fprintf(stderr, "Failed to allocate memory for targets\n");
        exit(1);
    }

    for (int i = 0; i < *numTargets; ++i) {
        token = strtok(NULL, " |");
        if (token == NULL) {
            fprintf(stderr, "Error: Target message format incorrect\n");
            exit(1);
        }
        (*targets)[i].x = atoi(token);
        token = strtok(NULL, " |");
        if (token == NULL) {
            fprintf(stderr, "Error: Target message format incorrect\n");
            exit(1);
        }
        (*targets)[i].y = atoi(token);
        (*targets)[i].number = rand() % 10 + 1; // Assign a random target number between 1 and 10
    }
}

void parseObstacles(char *obstacleMessage, int *numObstacles, Point **obstacles) {
    char *token = strtok(obstacleMessage, " ");
    if (token == NULL || strncmp(token, "O[", 2) != 0) {
        fprintf(stderr, "Error: Obstacle message format incorrect\n");
        exit(1);
    }
    *numObstacles = atoi(token + 2); // Skip "O[" and convert the remaining to integer

    *obstacles = malloc(*numObstacles * sizeof(Point));
    if (*obstacles == NULL) {
        fprintf(stderr, "Failed to allocate memory for obstacles\n");
        exit(1);
    }

    for (int i = 0; i < *numObstacles; ++i) {
        token = strtok(NULL, " |");
        if (token == NULL) {
            fprintf(stderr, "Error: Obstacle message format incorrect\n");
            exit(1);
        }
        (*obstacles)[i].x = atoi(token);
        token = strtok(NULL, " |");
        if (token == NULL) {
            fprintf(stderr, "Error: Obstacle message format incorrect\n");
            exit(1);
        }
        (*obstacles)[i].y = atoi(token);
    }
}

int main(int argc, char *argv[]) {
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
    int pipeServerWindowTar[2];
    int pipeServerWindowObs[2];
    int pipeServerWindowTar1[2];
    close(pipeServerWindowObs[0]);
    close(pipeServerWindowTar[0]);
    close(pipeServerWindowTar1[1]);



    if (pipe(pipeServerWindowTar) == -1 || pipe(pipeServerWindowObs) == -1 || pipe(pipeServerWindowTar1)== -1)
    {
        perror("pipeServerWindow");
        exit(EXIT_FAILURE);
        }

        sscanf(argv[1], "%d %d|%d %d|%d %d|%d %d", &pipeWatchdogServer[0], &pipeWatchdogServer[1],&pipeServerWindowTar[0],&pipeServerWindowTar[1],&pipeServerWindowObs[0],&pipeServerWindowObs[1], &pipeServerWindowTar1[0], &pipeServerWindowTar1[1]);

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
    FILE *logFile;
    char logFilePath[100];
    snprintf(logFilePath, sizeof(logFilePath), "log/ServerLog.txt");
    logFile = fopen(logFilePath, "w");

    if (logFile == NULL) {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    // SHARED MEMORY AND SEMAPHORE SETUP
    double position[6] = {boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2};
    int sharedSegSize = (sizeof(position));

    sem_t *semaphoreID = sem_open(SEM_PATH, O_CREAT, S_IRUSR | S_IWUSR, 1);
    if (semaphoreID == SEM_FAILED) {
        perror("sem_open");
        fclose(logFile);
        exit(EXIT_FAILURE);
    }
    sem_init(semaphoreID, 1, 0);

    int shmFD = shm_open(SHM_PATH, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG);
    if (shmFD < 0) {
        perror("shm_open");
        fclose(logFile);
        sem_close(semaphoreID);
        sem_unlink(SEM_PATH);
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shmFD, sharedSegSize) == -1) {
        perror("ftruncate");
        fclose(logFile);
        sem_close(semaphoreID);
        sem_unlink(SEM_PATH);
        shm_unlink(SHM_PATH);
        exit(EXIT_FAILURE);
    }
    void *shmPointer = mmap(NULL, sharedSegSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFD, 0);
    if (shmPointer == MAP_FAILED) {
        perror("mmap");
        fclose(logFile);
        sem_close(semaphoreID);
        sem_unlink(SEM_PATH);
        shm_unlink(SHM_PATH);
        exit(EXIT_FAILURE);
    }
    sem_post(semaphoreID);

    // Socket setup
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;

    printf("Starting server...\n");

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    printf("Socket created successfully.\n");

    // Clear the memory of serv_addr struct
    memset(&serv_addr, 0, sizeof(serv_addr));
    portno = 12345;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    printf("Binding to port %d...\n", portno);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    printf("Listening for incoming connections...\n");
    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    printf("Waiting for a client to connect...\n");


    double width = boardSize * windowWidth;
    double height = boardSize * windowHeight;
    while (1) {
    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd < 0) {
        if (errno == EINTR) {
            continue; // Retry accept
        } else {
            error("ERROR on accept");
        }
    }

    printf("Client connected. Handling request...\n");

    memset(buffer, 0, sizeof(buffer)); // Clear buffer

    n = read(newsockfd, buffer, 255);
    if (n < 0) {
        fprintf(stderr, "ERROR reading from socket\n");
        close(newsockfd);
        continue;
    }

    if (strncmp(buffer, "TI", 2) == 0) {
    printf("Target message received: %s\n", buffer);



    // Send window width and height to the client
    sprintf(buffer, "%.3lf, %.3lf", height, width);
    n = write(newsockfd, buffer, strlen(buffer));
    if (n < 0) {
        fprintf(stderr, "ERROR writing to socket\n");
        close(newsockfd);
        continue;
    }

    // Receive target message from the client
    memset(buffer, 0, sizeof(buffer)); // Clear buffer
    n = read(newsockfd, buffer, 255);
    if (n < 0) {
        fprintf(stderr, "ERROR reading from socket\n");
        close(newsockfd);
        continue;
    }
    printf("Received target message: %s\n", buffer); 
        printf("Parsing target message...\n");


    int numTargets;
    Point *targets = NULL;
    parseTargets(buffer, &numTargets, &targets);

    // Write received targets to the log file
    fprintf(logFile, "Number of Targets: %d\n", numTargets);
    printf("Number of Targets: %d\n", numTargets);
    for (int i = 0; i < numTargets; ++i) {
        fprintf(logFile, "Target %d: x=%d, y=%d\n", i+1, targets[i].x, targets[i].y);
        printf("Target %d: x=%d, y=%d\n", i+1, targets[i].x, targets[i].y);
    }
    fflush(logFile);

    // Store targets in pipes
    write(pipeServerWindowTar[1], &numTargets, sizeof(int)); // Write the number of targets to the pipe
    for (int i = 0; i < numTargets; ++i) {
        write(pipeServerWindowTar[1], &(targets[i]), sizeof(Point)); // Write each target to the pipe
    }

    free(targets);

    // Read the struct from the pipe with a loop to handle cases where data is not immediately available
    TargetInfo receivedInfo;
    int bytes_read;
    while ((bytes_read = read(pipeServerWindowTar1[0], &receivedInfo, sizeof(receivedInfo))) < 0) {
        if (errno == EINTR) {
            // Read was interrupted by a signal, retry the operation
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No data available, wait for a short period and retry
            sleep(1); // Sleep for 1 second
        } else {
            perror("Error reading from pipeServerWindowTar");
            break;
        }
    }

    // Check if data was successfully read
    if (bytes_read > 0) {
        // Print the received target information
    // printf("Number of targets: %d\n", receivedInfo.numTargets);
        //printf("Number of removed targets: %d\n", receivedInfo.numTargetsReached);
    }
    remainingTargets = receivedInfo.numTargets - receivedInfo.numTargetsReached;
    // Check if the remaining targets count is 4, if yes, send "GE" message to the client
        if (remainingTargets == 4) {
            printf("Targets Reached\n");
            send_GE_message(newsockfd);
            printf("Sent GE message to target client\n");
        }
        }
        else if (strncmp(buffer, "OI", 2) == 0)
        {
            printf("Obstacle message received: %s\n", buffer);

            // Send window width and height to the client
            sprintf(buffer, "%.3lf, %.3lf", height, width);
            n = write(newsockfd, buffer, strlen(buffer));
            if (n < 0)
            {
                fprintf(stderr, "ERROR writing to socket\n");
                close(newsockfd);
                continue;
            }

            // Receive obstacle message from the client
            memset(buffer, 0, sizeof(buffer)); // Clear buffer
            n = read(newsockfd, buffer, 255);
            if (n < 0)
            {
                fprintf(stderr, "ERROR reading from socket\n");
                close(newsockfd);
                continue;
            }
            printf("Received obstacle message: %s\n", buffer);
            printf("Parsing obstacle message...\n");


    int numObstacles;
    Point *obstacles = NULL;
    parseObstacles(buffer, &numObstacles, &obstacles);

    // Write received obstacles to the log file and terminal
    fprintf(logFile, "Number of Obstacles: %d\n", numObstacles);
    printf("Number of Obstacles: %d\n", numObstacles);
    for (int i = 0; i < numObstacles; ++i) {
        fprintf(logFile, "Obstacle %d: x=%d, y=%d\n", i+1, obstacles[i].x, obstacles[i].y);
        printf("Obstacle %d: x=%d, y=%d\n", i+1, obstacles[i].x, obstacles[i].y);
    }
    fflush(logFile);

    // Store obstacles in pipes
    write(pipeServerWindowObs[1], &numObstacles, sizeof(int)); // Write the number of obstacles to the pipe
    for (int i = 0; i < numObstacles; ++i) {
        write(pipeServerWindowObs[1], &(obstacles[i]), sizeof(Point)); // Write each obstacle to the pipe
    }

    free(obstacles);
    }
    close(newsockfd);
    printf("Connection with client closed.\n");
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
