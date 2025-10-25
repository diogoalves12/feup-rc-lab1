#include "state_machine.h"
#include "serial_port.h"

#define FLAG 0x7E
#define ESC  0x7D
#define ESC_XOR 0x20

typedef enum { START, FLAG_RCV, A_RCV, C_RCV, BCC1_OK, DATA_RCV, STOP } State;

// Reads a frame from the serial port.
// returns -1 on read/bcc1 error, 1 on BCC2 error, 0 on success.
int readFrame(unsigned char *header, unsigned char *data, int *dataSize) {
    State state = START;

    unsigned char A = 0, C = 0, bcc1 = 0;

    unsigned char bcc2 = 0;
    unsigned char calcBCC1 = 0, calcBCC2 = 0;
    *dataSize = 0;

    int escapeNext = 0;
    unsigned char byte;
    int result;

    while(state != STOP) {
        result = readByteSerialPort(&byte); // readByteSerialPort returns -1 on error, 0 if no byte was received, 1 if a byte was received.
        if(result == -1) return -1;
        if(result == 0) continue;

        switch (state) {
        case START:
            if(byte == FLAG) state = FLAG_RCV;
            break;

        case FLAG_RCV:
            if(byte == FLAG) break; // multiple FLAGs received
            A = byte;
            state = A_RCV;
            break;

        case A_RCV: 
            if(byte == FLAG) {state = FLAG_RCV; break;} 
            C = byte;
            state = C_RCV; 
            break;

        case C_RCV:
            if(byte == FLAG) {state = FLAG_RCV; break;}
            bcc1 = byte;
            calcBCC1 = (A ^ C);
            state = BCC1_OK;
            break;

        case BCC1_OK:
            if(byte == FLAG) {
                // Supervion Frame
                header[0] = A; header[1] = C; header[2] = bcc1;
                state = STOP;
            } else {
                if(byte == ESC) {
                    escapeNext = 1;
                } else {
                // Information Frame
                unsigned char actualByte = escapeNext ? (byte ^ ESC_XOR) : byte;
                escapeNext = 0;
                data[(*dataSize)++] = actualByte;
                calcBCC2 ^= actualByte;
                }
                state = DATA_RCV;
            }
            break;

        case DATA_RCV:
            if(byte == FLAG) {
                if(*dataSize > 0) {
                    bcc2 = data[(*dataSize) - 1];
                    calcBCC2 ^= bcc2; // removes bcc2 from data
                    --(*dataSize);
                }
                header[0] = A; header[1] = C; header[2] = bcc1;
                escapeNext = 0;
                state = STOP;
                break;
            }

            if(byte == ESC) {
                escapeNext = 1;
            } else {
                unsigned char actualByte = escapeNext ? (byte ^ ESC_XOR) : byte;
                escapeNext = 0;
                data[(*dataSize)++] = actualByte;
                calcBCC2 ^= actualByte;
            }
            break;

        case STOP:
            break;            
        default:
            break;
        }
    }

    if(bcc1 != calcBCC1) return -1; // bcc1 verification error

    if(*dataSize > 0) {
        if (calcBCC2 != bcc2) return 1; // bcc2 verification error
    }

    return 0; // success
}
