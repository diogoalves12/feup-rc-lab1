#ifndef ALARM_SIGACTION_H
#define ALARM_SIGACTION_H

#include <signal.h>

#define FALSE 0
#define TRUE 1

extern int alarmEnabled;
extern int alarmCount;

void alarmHandler(int signal);
void setupAlarmHandler();
void startAlarm(int seconds);
void cancelAlarm();

#endif // ALARM_SIGACTION_H
