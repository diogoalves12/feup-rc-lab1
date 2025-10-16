#include "frame_sm.h"
#include "alarm_sigaction.h"
#include "serial_port.h"

#define FLAG 0x7E

typedef enum {
    INITIAL,
    HEADER,
    DATA,
    END,
} State;

int readFrame(unsigned char *header, unsigned char *data, int *dataSize, int expectedA, int expectedC) {
    State state = INITIAL;
    int bcc2 = 0;
    int byteRead = FALSE;
    int headerIndex = 0;
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
                if (header[2] != (A ^ C) || A != expectedA || C != expectedC) {
                    state = INITIAL;
                    headerIndex = 0;
                } else {
                    state = DATA;
                    bcc2 = 0;
                    *dataSize = 0;
                }
            }
            break;
        case DATA:
            if(byte == FLAG) {
                if(*dataSize > 0) {
                    unsigned char lastByte = data[(*dataSize) - 1];
                    bcc2 ^= lastByte;
                    (*dataSize)--;
                    if(bcc2 == lastByte) {
                        state = END;
                    } else {
                        state = INITIAL;
                    }
                } else {
                    state = END;
                }
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
    return state == END ? 0 : -1;
}

