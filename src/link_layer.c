// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "alarm_sigaction.h"
#include "state_machine.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

// Frame constants
#define FLAG 0x7E
#define ESCAPE 0x7D
#define ESC_XOR 0x20

// Address field
#define A_TX 0x03 // Address field in frames that are commands sent by the Transmitter or replies sent by the Receiver
#define A_RX 0x01 // Address field in frames that are commands sent by the Receiver or replies sent by the Transmitter

// Control field 
#define C_SET 0x03
#define C_UA 0x07
#define C_DISC 0x0B

// Control field helpers
#define C_I(ns)   ((unsigned char)((ns) ? 0x80 : 0x00))
#define C_RR(r)   ((unsigned char)((r) ? 0xAB : 0xAA))
#define C_REJ(r)  ((unsigned char)((r) ? 0x55 : 0x54))

// Link-layer internal state 
static LinkLayerRole role;      // LLTx or LLRx
static int timeout = 0;         // Timeout seconds
static int maxRetries = 0;      // Max retransmissions
static int nsTx = 0;           // Transmitter sequence number (Ns)
static int nrRX = 0;           // Receiver expected Ns

/////////////////////////////////////////////////
// Debug / timing helpers
/////////////////////////////////////////////////

#ifndef LL_DEBUG
#define LL_DEBUG 1
#endif

static int ll_debug_enabled = LL_DEBUG;
static unsigned long long now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_sec * 1000ULL + (unsigned long long)tv.tv_usec / 1000ULL;
}


#define DLOG(fmt, ...) do { \
    if (ll_debug_enabled) { \
        printf("[LL][%llu ms] " fmt "\n", now_ms(), ##__VA_ARGS__); \
    } \
} while (0)

// Runtime stats (simple counters)
static unsigned long stat_tx_iframes = 0;
static unsigned long stat_rx_iframes = 0;
static unsigned long stat_rej_sent = 0;
static unsigned long stat_rej_recv = 0;
static unsigned long stat_timeouts = 0;

/////////////////////////////////////////////////
// Helper functions
/////////////////////////////////////////////////

// Writes the full buffer to the serial port, handling partial writes.
static int writeAll(const unsigned char *buf, int len) {
    int total = 0;
    while (total < len) {
        int w = writeBytesSerialPort(buf + total, len - total);
        if (w < 0) return -1;
        total += w;
    }
    return total;
}

// Byte-stuff a single byte into out buffer, returns bytes written (1 or 2)
static int stuffByte(unsigned char b, unsigned char *out) {
    if (b == FLAG || b == ESCAPE) {
        out[0] = ESCAPE;
        out[1] = (unsigned char)(b ^ ESC_XOR);
        return 2;
    }
    out[0] = b;
    return 1;
}

// Verificação de header (A, C, BCC1)
static int checkHeader(const unsigned char header[3], unsigned char a, unsigned char c) {
    return header[0] == a && header[1] == c && header[2] == (unsigned char)(a ^ c);
}

static void buildSupervisionFrame(unsigned char a, unsigned char c, unsigned char frame[5]) {
    frame[0] = FLAG;
    frame[1] = a;
    frame[2] = c;
    frame[3] = (unsigned char)(a ^ c); // calculate BCC1
    frame[4] = FLAG;
}

static int sendSupervisionFrame(unsigned char a, unsigned char c) {
    unsigned char frame[5];
    buildSupervisionFrame(a, c, frame);
    // int w = writeBytesSerialPort(f, 5);
    //return (w == 5) ? 0 : -1;
    return(writeAll(frame, 5) == 5) ? 0 : -1;
}

// Build an I-frame into outFrame with payload buf[0..bufSize-1].
// Returns total frame length in outLen; returns 0 on error (e.g., too big)
static int buildIFrame(const unsigned char *buf, int bufSize, int ns, unsigned char *outFrame, int outLen) {
    // Worst-case stuffing: each data byte (and BCC2) becomes 2 bytes
    int maxNeeded = 1 + 1 + 1 + 1 + (bufSize * 2) + 2 + 1; // FLAG + A + C + BCC1 + DATA + BCC2 + FLAG
    if (outLen < maxNeeded) return 0;

    int i = 0;
    outFrame[i++] = FLAG;
    outFrame[i++] = A_TX;

    unsigned char c = C_I(ns);
    outFrame[i++] = c;
    outFrame[i++] = (unsigned char)(A_TX ^ c);

    unsigned char bcc2 = 0;
    for (int k = 0; k < bufSize; ++k) {
        bcc2 ^= buf[k];
        i += stuffByte(buf[k], &outFrame[i]);
    }

    i += stuffByte(bcc2, &outFrame[i]);
    outFrame[i++] = FLAG;
    
    return i;
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

    DLOG("llopen: role=%s baud=%d timeout=%d retries=%d",
         (connectionParameters.role == LlTx ? "Tx" : "Rx"),
         connectionParameters.baudRate,
         connectionParameters.timeout,
         connectionParameters.nRetransmissions);

    unsigned char header[3];
    unsigned char dataBuf[ MAX_PAYLOAD_SIZE];
    int dataSize = 0;

    if(connectionParameters.role == LlTx) {
        int tries = 0;

        while(tries < connectionParameters.nRetransmissions) {
            DLOG("llopen[Tx]: send SET (try %d)", tries + 1);
            if(sendSupervisionFrame(A_TX, C_SET) < 0) {
                closeSerialPort();
                return -1;
            }

            startAlarm(connectionParameters.timeout);
            unsigned long long t0 = now_ms();
            
            while(alarmEnabled) {
                int res = readFrame(header, dataBuf, &dataSize);
                if(res == 0) {
                    if(checkHeader(header, A_TX, C_UA)) {
                        // UA received
                        cancelAlarm();
                        DLOG("llopen[Tx]: got UA after %llums", now_ms() - t0);
                        // Initialize internal state for subsequent operations
                        role = LlTx;
                        timeout = connectionParameters.timeout;
                        maxRetries = connectionParameters.nRetransmissions;
                        nsTx = 0;
                        nrRX = 0;
                        return 0;
                    }
                } else if (res < 0) {
                    // read error, ignores untill timeout
                }
            }
            cancelAlarm();
            DLOG("llopen[Tx]: timeout waiting UA (try %d)", tries + 1);
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
                    DLOG("llopen[Rx]: received SET -> send UA");
                    if (sendSupervisionFrame(A_TX, C_UA) < 0) {
                        closeSerialPort();
                        return -1;
                    }
                    // Initialize internal state for subsequent operations
                    role = LlRx;
                    timeout = connectionParameters.timeout;
                    maxRetries = connectionParameters.nRetransmissions;
                    nsTx = 0;
                    nrRX = 0;
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

    if (role != LlTx) return -1;
    if (bufSize < 0 || bufSize > MAX_PAYLOAD_SIZE) return -1;

    // build Iframe
    unsigned char frame[bufSize * 2 + 7]; 
    int frameLen = buildIFrame(buf, bufSize, nsTx, frame, (int)sizeof(frame));
    if(frameLen <= 0) return -1;

    int tries = 0;
    while(tries < maxRetries) { 
        // send Iframe
        int w = writeAll(frame, frameLen);
        if(w != frameLen) return -1;
        stat_tx_iframes++;
        DLOG("llwrite: sent I(ns=%d) len=%d (try %d)", nsTx, frameLen, tries + 1);
        
        // wait for RR/REJ
        startAlarm(timeout);
        unsigned long long t0 = now_ms();

        unsigned char header[3];
        unsigned char dataBuf[MAX_PAYLOAD_SIZE];
        int dataSize = 0;

        int gotRej = 0;

        while (alarmEnabled) {
            int r = readFrame(header, dataBuf, &dataSize);
            if (r == 0) {
                // Supervision expected: RR or REJ from receiver uses A_TX
                unsigned char rrExpected = C_RR((nsTx ^ 1)); // se ns = 0 então espera RR(1), se ns = 1 então espera RR(0)
                if (checkHeader(header, A_TX, rrExpected)) {
                    cancelAlarm();
                    nsTx ^= 1; // toggle ns for next frame -> se nsTx = 0 então nsTx = 1, se nsTx = 1 então nsTx = 0
                    DLOG("llwrite: got RR, next Ns=%d after %llums", nsTx, now_ms() - t0);
                    return bufSize;
                }

                unsigned char rejExpected = C_REJ(nsTx);    
                if (checkHeader(header, A_TX, rejExpected)) {
                    // Explicit rejection: retransmit immediately without consuming a timeout
                    cancelAlarm();
                    gotRej = 1;
                    stat_rej_recv++;
                    DLOG("llwrite: got REJ(ns=%d) after %llums", nsTx, now_ms() - t0);
                    break; // break inner wait to resend
                }
                // Ignore other frames (SET/UA/DISC)
            } else if (r < 0) {
                // read/bcc1 error: ignore until timeout
            } else if (r == 1) {
                // BCC2 error on an unexpected info frame: ignore; we only expect supervision
            }
        }

        if(gotRej) {
            // Retransmission due to REJ
            continue; // resend immediately
        } 
        // Timeout path: prepare retransmission
        cancelAlarm();  
        stat_timeouts++;
        DLOG("llwrite: timeout waiting RR/REJ (try %d)", tries + 1);
        tries++;
        // Rebuild frame with same Ns
        frameLen = buildIFrame(buf, bufSize, nsTx, frame, (int)sizeof(frame));
        if (frameLen <= 0) return -1;
    }

    // Exceeded retries
    return -1;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    if (role != LlRx) return -1;

    unsigned char header[3];
    // +1 to hold BCC2 temporarily; readFrame appends BCC2 then removes it
    unsigned char dataBuf[MAX_PAYLOAD_SIZE + 1];
    int dataSize = 0;

    while (TRUE) {
        int r = readFrame(header, dataBuf, &dataSize);
        if (r == 0) {
            // Supervision or Information frame received with valid BCC1 (and BCC2 if data)
            if (header[0] == A_TX && (header[1] == C_I(0) || header[1] == C_I(1))) {
                // Information frame
                int ns = (header[1] & 0x80) ? 1 : 0;
                if(ns == nrRX) {
                    // Correct in-order frame: send RR(next) and deliver payload
                    if (sendSupervisionFrame(A_TX, C_RR(nrRX ^ 1)) < 0) return -1;
                    if (dataSize > 0)
                        memcpy(packet, dataBuf, dataSize);
                    nrRX ^= 1;
                    stat_rx_iframes++;
                    DLOG("llread: accepted I(ns=%d), sent RR(next=%d), data=%d", ns, nrRX, dataSize);
                    return dataSize;
                } else {
                    // Duplicate frame: already delivered; re-ACK current expected
                    if (sendSupervisionFrame(A_TX, C_RR(nrRX)) < 0) return -1;
                    DLOG("llread: duplicate I(ns=%d), re-sent RR(curr=%d)", ns, nrRX);
                    continue; // keep waiting for the expected Ns
                }
            }
            // Other supervision frames are ignored here (handled in llopen/llclose)
        } else if (r == 1) {
            // BCC2 error on Information frame: request retransmission
            if (header[0] == A_TX && (header[1] == C_I(0) || header[1] == C_I(1))) {
                int ns = (header[1] & 0x80) ? 1 : 0;
                if(ns == nrRX) {
                     if (sendSupervisionFrame(A_TX, C_REJ(nrRX)) < 0) return -1;
                    stat_rej_sent++;
                    DLOG("llread: BCC2 error on I(expected ns=%d) -> sent REJ", nrRX);  
                } else {
                    // Duplicate frame with BCC2 error: re-ACK current expected
                    if (sendSupervisionFrame(A_TX, C_RR(nrRX)) < 0) return -1;
                    DLOG("llread: BCC2 error on duplicate I(ns=%d) -> re-sent RR(curr=%d)", ns, nrRX);
                }
            }
            // then keep waiting
            continue;
        } else { // r < 0
            // Read error or BCC1 error: ignore and continue
        }
    }
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose()
{
    DLOG("llclose: start");
    unsigned char header[3];
    unsigned char dataBuf[MAX_PAYLOAD_SIZE];
    int dataSize = 0;

    if(role == LlTx) {
        int tries = 0;

        while(tries < maxRetries) {
            DLOG("llclose[Tx]: send DISC (try %d)", tries + 1);
            if(sendSupervisionFrame(A_TX, C_DISC) < 0) {
                closeSerialPort();
                return -1;
            }

            // Wait for DISC from receiver
            startAlarm(timeout);
            while(alarmEnabled) {
                int r = readFrame(header, dataBuf, &dataSize); 
                if(r == 0) {
                    if(checkHeader(header,A_TX,C_DISC)) {
                        // DISC received -> send UA and close
                        cancelAlarm();
                        // Send UA and close
                        if(sendSupervisionFrame(A_TX,C_UA) < 0) {
                            closeSerialPort();
                            return -1;
                        }
                        closeSerialPort();
                        DLOG("llclose[Tx]: closed. Stats tx=%lu rx=%lu rej_sent=%lu rej_recv=%lu timeouts=%lu",
                             stat_tx_iframes, stat_rx_iframes, stat_rej_sent, stat_rej_recv, stat_timeouts);
                        return 0;
                    }
                }
                else if (r < 0 || r == 1) {
                    // read/bcc1 error or BCC2 error: ignore until timeout 
                }
            }
            cancelAlarm();
            DLOG("llclose[Tx]: timeout waiting DISC (try %d)", tries + 1);
            tries++;
        }
        closeSerialPort();
        DLOG("llclose[Tx]: exceeded retries. Stats tx=%lu rx=%lu rej_sent=%lu rej_recv=%lu timeouts=%lu",
             stat_tx_iframes, stat_rx_iframes, stat_rej_sent, stat_rej_recv, stat_timeouts);
        return -1; // exceeded max tries
    } 
    
    else if (role == LlRx) {
        // 1) wait for DISC from peer (no timer; may block until received)
        DLOG("llclose[Rx]: waiting DISC");
        while(TRUE) {
            int r = readFrame(header, dataBuf, &dataSize);
            if(r == 0 && checkHeader(header,A_TX,C_DISC)){
                break;
            }
        // else ignore and continue
        }

        // 2) Send DISC and wait for UA
        int tries = 0;
        while(tries < maxRetries) {
            DLOG("llclose[Rx]: send DISC (try %d)", tries + 1);
            if(sendSupervisionFrame(A_TX,C_DISC) < 0) {
                closeSerialPort();
                return -1;
            }

            startAlarm(timeout);
            while(alarmEnabled) {
                int r = readFrame(header, dataBuf, &dataSize);
                if(r == 0) {
                    if(checkHeader(header,A_TX,C_UA)) {
                        // UA received -> close
                        cancelAlarm();
                        closeSerialPort();
                        DLOG("llclose[Rx]: closed. Stats tx=%lu rx=%lu rej_sent=%lu rej_recv=%lu timeouts=%lu",
                             stat_tx_iframes, stat_rx_iframes, stat_rej_sent, stat_rej_recv, stat_timeouts);
                        return 0;
                    }
                }
                else if (r < 0 || r == 1) {
                    // read/bcc1 error or BCC2 error: ignore until timeout 
                }
            }
            cancelAlarm();
            DLOG("llclose[Rx]: timeout waiting UA (try %d)", tries + 1);
            tries++;
        }

        closeSerialPort();
        DLOG("llclose[Rx]: exceeded retries. Stats tx=%lu rx=%lu rej_sent=%lu rej_recv=%lu timeouts=%lu",
             stat_tx_iframes, stat_rx_iframes, stat_rej_sent, stat_rej_recv, stat_timeouts);
        return -1; // exceeded max tries
    }

    // invalid state
    closeSerialPort();
    return -1;
}
