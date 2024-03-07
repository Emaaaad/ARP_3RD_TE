#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <curses.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include "../include/constant.h"

int totalScore = 0;

// Function for creating a new window
WINDOW *createBoard(int height, int width, int starty, int startx)
{
    WINDOW *displayWindow;
    displayWindow = newwin(height, width, starty, startx);

    for (int i = 0; i < height * 0.9; ++i)
    {
        mvwaddch(displayWindow, i, 0, '|');
    } // Left border
    for (int i = 0; i < width * 0.9; ++i)
    {
        mvwaddch(displayWindow, 0, i, '=');
    } // Upper border
    int rightBorderX = (int)(width * 0.9);
    for (int i = 0; i < height * 0.9; ++i)
    {
        mvwaddch(displayWindow, i, rightBorderX, '|');
    } // Right border
    int bottomBorderY = (int)(height * 0.9);
    for (int i = 0; i < width * 0.9; ++i)
    {
        mvwaddch(displayWindow, bottomBorderY, i, '=');
    } // Bottom border

    wrefresh(displayWindow);
    return displayWindow;
}

// Function for setting up ncurses windows
void setupNcursesWindows(WINDOW **display, WINDOW **scoreboard)
{
    int inPos[4] = {LINES / 200, COLS / 200, (LINES / 200) + LINES * windowHeight, COLS / 200};

    int displayHeight = LINES * windowHeight;
    int displayWidth = COLS * windowWidth;

    int scoreboardHeight = LINES * scoreboardWinHeight;
    int scoreboardWidth = COLS * windowWidth;

    *scoreboard = createBoard(scoreboardHeight, scoreboardWidth, inPos[2], inPos[3]);
    *display = createBoard(displayHeight, displayWidth, inPos[0], inPos[1]);
}
// Global variables for ncurses windows
WINDOW *display = NULL;
WINDOW *scoreboard = NULL;
// Signal handler function for SIGWINCH
void handleResize(int sig)
{
    endwin();
    refresh();
    setupNcursesWindows(&display, &scoreboard);
    refresh();
}

void displayTargets(WINDOW *win, Point *targets, int numTargets, double scalex, double scaley)
{
    wattron(win, COLOR_PAIR(4));
    for (int i = 0; i < numTargets; ++i)
    {
        // printf("number: %d, targetsx, targetsy = %lf, %lf ", i, targets[i].x, targets[i].y);

        // Adjust target position based on scaling factor to fit within the window
        int x = (int)(targets[i].x / scalex);
        int y = (int)(targets[i].y / scaley);
        mvwprintw(win, y, x, "%d", targets[i].number);
    }
    wattroff(win, COLOR_PAIR(4));
}

void displayObstacles(WINDOW *win, Point *obstacles, int numObstacles, double scalex, double scaley)

{
    wattron(win, COLOR_PAIR(3));
    for (int i = 0; i < numObstacles; ++i)
    {
        // Adjust obstacle position based on scaling factor to fit within the window
        int x = (int)(obstacles[i].x / scalex);
        int y = (int)(obstacles[i].y / scaley);
        mvwprintw(win, y, x, "#");
    }
    wattroff(win, COLOR_PAIR(3));
}

void logData(FILE *logFile, double *position, int sharedSegSize)
{
    time_t rawtime;
    struct tm *timeinfo;
    char timeStr[9]; // String to store time (HH:MM:SS)

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);

    fprintf(logFile, "[Time: %s] Drone Position: %.2f, %.2f\n",
            timeStr, position[4], position[5]);
    fflush(logFile);
}

int main(int argc, char *argv[])
{
    // Initializing ncurses
    initscr();
    int key, initial;
    initial = 0;
    int lastObstacleHit = 0;

    // Setting up colors
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK);
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_GREEN, COLOR_BLACK);

    // Setting up signal handling for window resize
    signal(SIGWINCH, handleResize);

    // Setting up signal handling for watchdog
    struct sigaction signalAction;

    signalAction.sa_sigaction = handleSignal;
    signalAction.sa_flags = SA_SIGINFO;
    sigaction(SIGINT, &signalAction, NULL);
    sigaction(SIGUSR1, &signalAction, NULL);

    // Extracting pipe information from command line arguments
    int pipeWindowKeyboard[2], pipeWatchdogWindow[2], (pipeServerWindowTar[2]), (pipeServerWindowObs[2]), (pipeServerWindowTar1[2]);
    sscanf(argv[1], "%d %d|%d %d|%d %d|%d %d|%d %d", &pipeWindowKeyboard[0], &pipeWindowKeyboard[1], &pipeWatchdogWindow[0],
           &pipeWatchdogWindow[1], &pipeServerWindowTar[0], &pipeServerWindowTar[1], &pipeServerWindowObs[0], &pipeServerWindowObs[1],
           &pipeServerWindowTar1[0], &pipeServerWindowTar1[1]);
    close(pipeWindowKeyboard[0]);
    close(pipeWatchdogWindow[0]);
    close(pipeServerWindowObs[1]);
    close(pipeServerWindowTar[1]);
    close(pipeServerWindowTar1[0]);

    // Sending PID to watchdog
    pid_t windowPID;
    windowPID = getpid();
    if (write(pipeWatchdogWindow[1], &windowPID, sizeof(windowPID)) == -1)
    {
        perror("write pipeWatchdogWindow");
        exit(EXIT_FAILURE);
    }
    printf("%d\n", windowPID);
    close(pipeWatchdogWindow[1]);

    // Shared memory setup
    double position[6] = {boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2, boardSize / 2};
    int sharedSegSize = (sizeof(position));

    sem_t *semID = sem_open(SEM_PATH, 0);
    if (semID == SEM_FAILED)
    {
        perror("sem_open");
        exit(EXIT_FAILURE);
    }
    int shmfd = shm_open(SHM_PATH, O_RDWR, S_IRWXU | S_IRWXG);
    if (shmfd < 0)
    {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }
    void *shmPointer = mmap(NULL, sharedSegSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    if (shmPointer == MAP_FAILED)
    {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    // Open the log files
    FILE *logFile;
    char logFilePath[100];
    snprintf(logFilePath, sizeof(logFilePath), "log/windowLog.txt");
    logFile = fopen(logFilePath, "w");

    if (logFile == NULL)
    {
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }

    Point *targets = NULL;
    int numTargets = 0;
    Point *obstacles = NULL;
    int numObstacles = 0;
    int numTargetsReached = 0;

    int rvalue;
    bool freeFlagTarget = false;
    bool freeFlagObs = false;
    while (1)
    {
        // Read targets from the pipe

        rvalue = read(pipeServerWindowTar[0], &numTargets, sizeof(int));
        // fprintf(stderr, "rvalue: %d", rvalue);
        if (rvalue > 0 && numTargets > 0)
        {
            if (freeFlagTarget)
            {
                free(targets);
            }
            freeFlagTarget = true;
            // free(targets);
            targets = (Point *)malloc(numTargets * sizeof(Point));
            for (int i = 0; i < numTargets; i++)
            {
                read(pipeServerWindowTar[0], &(targets[i]), sizeof(Point)); // Write each target to the pipe
            }
            // read(pipeServerWindowTar[0], targets, numTargets * sizeof(Point));
        }

        // Read the number of obstacles from shared memory
        rvalue = read(pipeServerWindowObs[0], &numObstacles, sizeof(int));

        if (rvalue > 0 && numObstacles > 0)
        {
            if (freeFlagObs)
            {
                free(obstacles);
            }
            freeFlagObs = true;
            obstacles = (Point *)malloc(numObstacles * sizeof(Point));
            read(pipeServerWindowObs[0], obstacles, numObstacles * sizeof(Point));
        }

        // Refreshing windows
        WINDOW *win, *scoreboard;
        setupNcursesWindows(&win, &scoreboard);
        curs_set(0);
        nodelay(win, TRUE);

        double scalex, scaley;

        scalex = (double)boardSize / ((double)COLS * (windowWidth - 0.1));
        scaley = (double)boardSize / ((double)LINES * (windowHeight - 0.1));
        // fprintf(stderr, "scalex, scaley = %lf, %lf", scalex, scaley);

        // Displaying targets
        if (numTargets > 0)
        {
            displayTargets(win, targets, numTargets, scalex, scaley);
            // free(targets);
        }

        // Displaying obstacles
        if (numObstacles > 0)
        {
            displayObstacles(win, obstacles, numObstacles, scalex, scaley);
            // free(obstacles);
        }
        // Check if the drone reaches any of the targets
        bool droneReachedTarget = false;
        int targetReachedIndex = -1;
        for (int i = 0; i < numTargets; ++i)
        {
            double distance = sqrt(pow(position[4] - targets[i].x, 2) + pow(position[5] - targets[i].y, 2));
            if (distance < RADIUS)
            {
                droneReachedTarget = true;
                targetReachedIndex = i;
                break;
            }
        }

        // Check if the drone reaches any of the obstacles
        bool droneReachedObstacle = false;
        for (int i = 0; i < numObstacles; ++i)
        {
            double distance = sqrt(pow(position[4] - obstacles[i].x, 2) + pow(position[5] - obstacles[i].y, 2));
            if (distance < RADIUS)
            {
                droneReachedObstacle = true;
                break;
            }
        }

        int removedTargetCount = 0; // Initialize removed target count

        if (droneReachedTarget)
        {
            // Increment removed target count
            removedTargetCount++;

            // Store the value of the removed target
            int removedTarget, removedTargetValue;
            removedTarget = targets[targetReachedIndex].number;

            if (1 <= removedTarget && removedTarget <= 10)
            {
                removedTargetValue = removedTarget;
            }
            else
            {
                removedTargetValue = 0; // Set a default value if condition is not met
            }

            // Update targets (remove the target reached by the drone)
            for (int i = targetReachedIndex; i < numTargets - 1; ++i)
            {
                targets[i] = targets[i + 1];
            }

            // Increment the counter for the number of targets reached
            numTargetsReached++;

            // printf("numTargets: %d, numTargetsReached: %d\n", numTargets, numTargetsReached);

            // Check if there are only 4 targets remaining
            if (numTargets - numTargetsReached == 4)
            {
                // Create a struct to hold target information
                TargetInfo targetInfo;
                targetInfo.numTargets = numTargets;
                targetInfo.numTargetsReached = numTargetsReached;

                // Write the struct to the pipe
                if (write(pipeServerWindowTar1[1], &targetInfo, sizeof(targetInfo)) < 0)
                {
                    perror("Error writing to pipeServerWindowTar");
                }
                // Reset numTargetsReached
                numTargetsReached = 0;
            }

            // Write the removed target value to shared memory
            sem_wait(semID);
            memcpy(shmPointer, &removedTargetValue, sizeof(removedTargetValue));
            sem_post(semID);
        }

        // Write to shared memory only if an obstacle was reached
        if (droneReachedObstacle)
        {
            int obstacleHit = -1; // Indicates an obstacle was hit
            sem_wait(semID);
            memcpy(shmPointer, &obstacleHit, sizeof(obstacleHit));
            sem_post(semID);
        }
        // Read the removed target value from shared memory
        int sharedValue;
        sem_wait(semID);
        memcpy(&sharedValue, shmPointer, sizeof(sharedValue));
        sem_post(semID);

        // Check if there's a new obstacle hit
        if (sharedValue == -1 && lastObstacleHit == 0)
        {
            // New obstacle hit detected
            totalScore -= 2;
            lastObstacleHit = 1; // Update the last obstacle hit state
        }
        else if (sharedValue != -1)
        {
            // If the current value is not an obstacle hit, reset the last obstacle hit state
            lastObstacleHit = 0;
        }

        // Handle valid target score updates
        if (1 <= sharedValue && sharedValue <= 10)
        {
            totalScore += sharedValue;
        }
        // Print the score in the scoreboard window
        wattron(scoreboard, COLOR_PAIR(1));
        mvwprintw(scoreboard, 1, 1, "Position of the drone: %.2f,%.2f", position[4], position[5]);
        wattroff(scoreboard, COLOR_PAIR(1));

        // Display the score
        wattron(scoreboard, COLOR_PAIR(1));
        mvwprintw(scoreboard, 2, 1, "Score: %d", totalScore); // Display cumulative score
        wattroff(scoreboard, COLOR_PAIR(1));

        // Display targets on the window
        // displayTargets(win, targets, numTargets, scalex, scaley);
        // displayObstacles(win, obstacles, numObstacles, scalex, scaley);

        // Sending the first drone position to drone.c via shared memory
        if (initial == 0)
        {
            sem_wait(semID);
            memcpy(shmPointer, position, sharedSegSize);
            sem_post(semID);

            initial++;
        }

        // Showing the drone and position in the konsole
        wattron(win, COLOR_PAIR(2));
        mvwprintw(win, (int)(position[5] / scaley), (int)(position[4] / scalex), "+");
        wattroff(win, COLOR_PAIR(2));

        wrefresh(win);
        wrefresh(scoreboard);
        noecho();

        // Sending user input to keyboardManager.c
        key = wgetch(win);
        if (key != ERR)
        {
            int keypress = write(pipeWindowKeyboard[1], &key, sizeof(key));
            if (keypress < 0)
            {
                perror("writing error");
                close(pipeWindowKeyboard[1]);
                exit(EXIT_FAILURE);
            }
            if ((char)key == 'q')
            {
                close(pipeWindowKeyboard[1]);
                fclose(logFile);
                exit(EXIT_SUCCESS);
            }
        }
        usleep(500000);

        // Reading from shared memory
        sem_wait(semID);
        memcpy(position, shmPointer, sharedSegSize);
        sem_post(semID);

        // Writing to the log file
        logData(logFile, position, sharedSegSize);
        clear();
        sleep(1);
    }

    // Cleaning up
    close(pipeServerWindowTar[0]);
    close(pipeServerWindowObs[0]);
    close(pipeServerWindowTar1[1]);

    shm_unlink(SHM_PATH);
    sem_close(semID);
    sem_unlink(SEM_PATH);

    endwin();

    // Closing the log file
    fclose(logFile);

    return 0;
}
s
