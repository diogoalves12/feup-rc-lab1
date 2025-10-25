// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "alarm_sigaction.h"
#include "state_machine.h"

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FLAG 0x7E
#define ESCAPE 0x7D
#define A_TX 0x03 // Address field in frames that are commands sent by the Transmitter or replies sent by the Receiver
#define A_RX 0x01 // Address field in frames that are commands sent by the Receiver or replies sent by the Transmitter
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B

static void buildSupervisionFrame(unsigned char a, unsigned char c, unsigned char frame[5]) {
    frame[0] = FLAG;
    frame[1] = a;
    frame[2] = c;
    frame[3] = (unsigned char)(a ^ c); // calculate BCC1
    frame[4] = FLAG;
}

static int sendSupervisionFrame(unsigned char a, unsigned char c) {
    unsigned char f[5];
    buildSupervisionFrame(a, c, f);
    int w = writeBytesSerialPort(f, 5);
    return (w == 5) ? 0 : -1;
}

// Verificação de header (A, C, BCC1)
static int checkHeader(const unsigned char header[3], unsigned char a, unsigned char c) {
    return header[0] == a && header[1] == c && header[2] == (unsigned char)(a ^ c);
}

// not used yet
static int waitForSupervision(unsigned char expectedA, unsigned char expectedC) {
    unsigned char header[3];
    unsigned char dataBuf[MAX_PAYLOAD_SIZE];
    int dataSize = 0;

    while (alarmEnabled) {
        int r = readFrame(header, dataBuf, &dataSize);
        if (r == 0) {
            if (checkHeader(header, expectedA, expectedC)) return 0;
        } else if (r < 0) {
            // erro/BCC1: ignora até timeout
        }
    }
    return -1; // timeout
}



////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{

    if(openSerialPort(connectionParameters.serialPort, connectionParameters.baudRate) == -1) {
        return -1;
    }

    setupAlarmHandler();

    unsigned char header[3];
    unsigned char dataBuf[ MAX_PAYLOAD_SIZE];
    int dataSize = 0;

    if(connectionParameters.role == LlTx) {
        int tries = 0;

        while(tries < connectionParameters.nRetransmissions) {
            if(sendSupervisionFrame(A_TX, C_SET) < 0) {
                closeSerialPort();
                return -1;
            }

            startAlarm(connectionParameters.timeout);
            
            while(alarmEnabled) {
                int res = readFrame(header, dataBuf, &dataSize);
                if(res == 0) {
                    if(checkHeader(header, A_TX, C_UA)) {
                        // UA received
                        cancelAlarm();
                        return 0;
                    }
                } else if (res < 0) {
                    // read error, ignores untill timeout
                }
            }
            cancelAlarm();
            tries++;
        }
        // exceeded max tries
        closeSerialPort();
        return -1; // exceeded max tries
    }

    else if (connectionParameters.role == LlRx) {
        while(TRUE) {
            int res = readFrame(header, dataBuf, &dataSize);
            if(res == 0) {
                // readFrame já verificou BCC1; aqui basta confirmar comando
                if(checkHeader(header, A_TX , C_SET)) {
                    if (sendSupervisionFrame(A_TX, C_UA) < 0) {
                        closeSerialPort();
                        return -1;
                    }
                    return 0; // Ligação estabelecida
                }
            } else if (res < 0) {
                // read error, ignore and continue
            }
        }
    }
    closeSerialPort();
    return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO: Implement this function

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose()
{
    // TODO: Implement this function

    return 0;
}
