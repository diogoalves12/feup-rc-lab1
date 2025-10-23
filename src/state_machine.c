#include "state_machine.h"
#include "alarm_sigaction.h"
#include "serial_port.h"

#define FLAG 0x7E

typedef enum {
    INITIAL,
    HEADER,
    DATA,
    END,
} State;

int readFrame(unsigned char *header, unsigned char *data, int *dataSize, unsigned char expectedA, unsigned char expectedC) {
    State state = INITIAL;
    startAlarm(3);
    unsigned char bcc2 = 0;
    unsigned char bcc1 = 0;
    int byteRead = FALSE;
    int headerIndex = 0;
    unsigned char lastByte = 0;
    unsigned char A, C;
    *dataSize = 0;
    
    while(state != END && (alarmEnabled || state != INITIAL)) {
        if(!alarmEnabled) {
            if(!byteRead) break;
            byteRead = FALSE;
            startAlarm(3);
        }
        unsigned char byte;
        int result = readByteSerialPort(&byte);
        if(!result) continue;
        if(result == -1) return -1;

        byteRead = TRUE;
        switch (state) {
        case INITIAL:
            if (byte == FLAG) {
                state = HEADER;
                headerIndex = 0;
            }
            break;
        case HEADER:
            if (byte == FLAG) { 
                headerIndex = 0;
                break;
            }
            header[headerIndex++] = byte;
            if(headerIndex == 3) {
                A = header[0];
                C = header[1];
                bcc1 = header[2];
                state = DATA;
                bcc2 = 0;
                *dataSize = 0;
            }
            break;
        case DATA:
            if(byte == FLAG) {
                if(*dataSize > 0) {
                    lastByte = data[(*dataSize) - 1];
                    bcc2 ^= lastByte;
                    --(*dataSize);
                }
                state = END;
                break;
            }
            data[(*dataSize)++] = byte;
            bcc2 ^= byte;
            break;
        case END:
        default:
            break;
        }
    }

    // verificar control (se tem data ou se nao tem)
    if(bcc1 != (A ^ C) || bcc2 != lastByte || (false) || (false)) {
        return -1;
    }
    if(expectedA == A || expectedC == C) return 1;
    return 0;


    return state == END ? 0 : -1;
}

