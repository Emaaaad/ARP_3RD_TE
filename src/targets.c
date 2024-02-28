#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#define NUM_TARGETS 12

typedef struct {
    int x;
    int y;
} Point;

void error(const char *msg) {
    perror(msg);
    exit(0);
}

void generateTargets(Point *targets, int width, int height) {
    srand((unsigned int)getpid());
    for (int i = 0; i < NUM_TARGETS; ++i) {
        targets[i].x = rand() % width;
        targets[i].y = rand() % height;
    }
}

int main(int argc, char *argv[]) {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];
    if (argc < 3) {
        fprintf(stderr,"usage %s hostname port\n", argv[0]);
        exit(0);
    }
    portno = atoi(argv[2]);
    while (1) { // Loop until server terminates
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
            error("ERROR opening socket");
        server = gethostbyname(argv[1]);
        if (server == NULL) {
            fprintf(stderr,"ERROR, no such host\n");
            exit(0);
        }
        bzero((char *) &serv_addr, sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        bcopy((char *)server->h_addr_list[0], (char *)&serv_addr.sin_addr.s_addr, server->h_length);
        serv_addr.sin_port = htons(portno); // Convert port number to network byte order
        if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
            error("ERROR connecting");

        // Send "TI" to the server
        n = write(sockfd, "TI", 2);
        if (n < 0)
            error("ERROR writing to socket");

        // Receive window width and height from the server
        bzero(buffer, 256);
        n = read(sockfd, buffer, 255);
        if (n < 0)
            error("ERROR reading from socket");
        printf("Received message: %s\n", buffer);

        double width, height;
        sscanf(buffer, "%lf , %lf", &height, &width);
        printf("Received window height: %.3lf, width: %.3lf\n", height, width);

        // Generate target coordinates using received width and height
        Point targets[NUM_TARGETS];
        generateTargets(targets, width, height);

        // Prepare target message
        char targetMessage[256];
        sprintf(targetMessage, "T[%d] ", NUM_TARGETS);
        for (int i = 0; i < NUM_TARGETS; ++i) {
            char temp[20];
            sprintf(temp, "%d %d", targets[i].x, targets[i].y);
            if (i != NUM_TARGETS - 1)
                strcat(temp, " | ");
            strcat(targetMessage, temp);
        }

        // Print and send target coordinates to the server
        printf("Generated target coordinates:\n%s\n", targetMessage);
        n = write(sockfd, targetMessage, strlen(targetMessage));
        if (n < 0)
            error("ERROR writing to socket");

        printf("Sent target coordinates to server.\n");

        // Receive message from server
        bzero(buffer, 256);
        n = read(sockfd, buffer, 255);
        if (n < 0)
            error("ERROR reading from socket");

        // Check if server sent "GE" message
        if (strcmp(buffer, "GE") != 0) {
            printf("Server didn't respond with expected message. Terminating.\n");
            break; // Exit loop if server doesn't respond as expected
        }

        close(sockfd);
    }
    return 0;
}
