#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#define NUM_OBSTACLES 10
#define DELAY_SECONDS 5
FILE *logFile;


typedef struct
{
    double x;
    double y;
} Point;

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

void generateObstacles(Point *obstacles, int width, int height)
{
    srand(time(NULL));
    for (int i = 0; i < NUM_OBSTACLES; ++i)
    {
        obstacles[i].x = rand() % width;
        obstacles[i].y = rand() % height;
    }
}


int main(int argc, char *argv[])
{
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    // LOG FILE SETUP
    char logFilePath[100];
    snprintf(logFilePath, sizeof(logFilePath), "log/ObstacleLog.txt");
    logFile = fopen(logFilePath, "a");


    char buffer[1024];

    if (logFile == NULL)
    {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    if (argc < 3)
    {
        fprintf(stderr, "usage %s hostname port\n", argv[0]);
        exit(0);
    }
    portno = atoi(argv[2]);

    // Open socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    server = gethostbyname(argv[1]);
    if (server == NULL)
    {
        fprintf(stderr, "ERROR, no such host\n");
        exit(0);
    }
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr_list[0], (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno); // Convert port number to network byte order
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR connecting");

    // Send "OI" to the server only once
    n = write(sockfd, "OI", 2);
    fprintf(logFile, "Message sent: %s\n", buffer);
    fflush(logFile);

    if (n < 0)
        error("ERROR writing to socket");

    // Receive echo message from the server
    memset(buffer, 0, sizeof(buffer)); // Clear buffer
    read(sockfd, buffer, sizeof(buffer));
    printf("ECHO received: %s\n", buffer);
    fprintf(logFile, "ECHO recieved: %s\n", buffer);
        fflush(logFile);



    // Receive window width and height from the server
    bzero(buffer, 1024);
    n = read(sockfd, buffer, 1024);
    printf("Received message: %s\n", buffer);
    fprintf(logFile, "Message received: %s\n", buffer);
        fflush(logFile);



    if (n < 0)
        error("ERROR reading from socket");
    printf("Received message: %s\n", buffer);
    write(sockfd, buffer, sizeof(buffer));
    fprintf(logFile, "ECHO sent: %s\n", buffer);
    fflush(logFile);


    double width, height;
    sscanf(buffer, "%lf , %lf", &height, &width);
    printf("Received window height: %.3lf, width: %.3lf\n", height, width);
    // fprintf(logFile, "Message received: %s\n", buffer);


    while (1)
    {

        // Generate obstacle coordinates using received width and height
        Point obstacles[NUM_OBSTACLES];
        generateObstacles(obstacles, width, height);

        // Prepare obstacle message
        char obstacleMessage[1024];
        sprintf(obstacleMessage, "O[%d] ", NUM_OBSTACLES);
        for (int i = 0; i < NUM_OBSTACLES; ++i)
        {
            char temp[20];
            sprintf(temp, "%.3lf, %.3lf", obstacles[i].y, obstacles[i].x);
            if (i != NUM_OBSTACLES - 1)
                strcat(temp, " | ");
            strcat(obstacleMessage, temp);
        }

        // Print and send obstacle coordinates to the server
        printf("Generated obstacle coordinates:\n%s\n", obstacleMessage);
        n = write(sockfd, obstacleMessage, strlen(obstacleMessage));
        //fprintf(logFile, "Message sent: %s\n", buffer);

        if (n < 0)
            error("ERROR writing to socket");

        printf("Sent obstacle coordinates to server.\n");

        // Receive echo message from the server
        memset(buffer, 0, sizeof(buffer)); // Clear buffer
        read(sockfd, buffer, sizeof(buffer));
        fprintf(stderr, "ECHO received: %s\n", buffer);
        fprintf(logFile, "ECHO received: %s\n", buffer);
        fflush(logFile);

        sleep(DELAY_SECONDS); // Sleep before sending again
    }

    close(sockfd);
    fclose(logFile);

    return 0;
}
