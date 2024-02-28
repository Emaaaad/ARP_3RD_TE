#include <bits/sigaction.h>
#ifndef CONSTANTS_H
#define CONSTANTS_H
#define _POSIX_C_SOURCE 200809L


#define maxMsgLength 200

#define SEM_PATH "/sem_path"
#define SHM_PATH "/shm_path"

#define SHM_SIZE sizeof(struct Position)

#define M 1.0
#define K 1.0
#define T 0.5

#define boardSize 100
#define numberOfProcesses 5

#define counterThresold 5

#define windowWidth 1.00
#define scoreboardWinHeight 0.20
#define windowHeight 0.80

#define MAX_TARGETS 20
#define MAX_OBSTACLES 20

#define BUFFER_SIZE 1024
#define RADIUS 2.0

typedef struct {
    int x;
    int y;
    int number;
} Point;

// Define the struct to hold target information
typedef struct {
    int numTargets;
    int numTargetsReached;
} TargetInfo;


void handleSignal(int signo, siginfo_t *siginfo, void *context) {
    if (signo == SIGINT) {
        exit(1);
    }
    if (signo == SIGUSR1) {
        pid_t watchdogPID = siginfo->si_pid;
        kill(watchdogPID, SIGUSR2);
    }
}

#endif