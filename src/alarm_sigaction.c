#define _POSIX_C_SOURCE 200809L

#include "alarm_sigaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int alarmEnabled = FALSE;
int alarmCount = 0;

void alarmHandler(int signal)
{
    if (!alarmEnabled) return;
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm #%d received\n", alarmCount);
}

void setupAlarmHandler()
{
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }
}

void startAlarm(int seconds)
{
    alarm(seconds);
    alarmEnabled = TRUE;
}

void cancelAlarm()
{
    alarm(0);
    alarmEnabled = FALSE;
}

 