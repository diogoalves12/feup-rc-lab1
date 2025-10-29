// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include "alarm_sigaction.h"
#include "state_machine.h"
#include <string.h>
#include <time.h>
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

// Runtime stats
static unsigned long stat_tx_iframes = 0;
static unsigned long stat_rx_iframes = 0;
static unsigned long stat_rej_sent = 0;
static unsigned long stat_rej_recv = 0;
static unsigned long stat_timeouts = 0;

/////////////////////////////////////////////////
// Helper functions
/////////////////////////////////////////////////

/**
 * Writes the full buffer to the serial port, handling partial writes.
 * @param buf Pointer to the bytes to transmit.
 * @param len Number of bytes to write.
 * @return Total bytes written, or -1 if the writeBytesSerialPort fails.
 */
static int writeAll(const unsigned char *buf, int len) {
    int total = 0;
    while (total < len) {
        int w = writeBytesSerialPort(buf + total, len - total);
        if (w < 0) return -1;
        total += w;
    }
    return total;
}

/**
 * Applies byte stuffing to a byte.
 * @param b Byte to stuff.
 * @param out Output buffer where the stuffed byte(s) are written.
 * @return Number of bytes produced (1 for normal data, 2 if escaped).
 */
static int stuffByte(unsigned char b, unsigned char *out) {
    if (b == FLAG || b == ESCAPE) {
        out[0] = ESCAPE;
        out[1] = (unsigned char)(b ^ ESC_XOR);
        return 2;
    }
    out[0] = b;
    return 1;
}

/**
 * Checks whether the header matches the expected address, control and BCC1.
 * @param header Header bytes [A, C, BCC1].
 * @param a Expected address field.
 * @param c Expected control field.
 * @return TRUE when the header is valid, FALSE otherwise.
 */
static int checkHeader(const unsigned char header[3], unsigned char a, unsigned char c) {
    return header[0] == a && header[1] == c && header[2] == (unsigned char)(a ^ c);
}

/**
 * Builds a supervision frame.
 * @param a Address field.
 * @param c Control field.
 * @param frame Output buffer (frame) (5 bytes).
 */
static void buildSupervisionFrame(unsigned char a, unsigned char c, unsigned char frame[5]) {
    frame[0] = FLAG;
    frame[1] = a;
    frame[2] = c;
    frame[3] = (unsigned char)(a ^ c); // calculate BCC1
    frame[4] = FLAG;
}

/**
 * Sends a supervision frame.
 * @param a Address field.
 * @param c Control field.
 * @return 0 on success, -1 on write error.
 */
static int sendSupervisionFrame(unsigned char a, unsigned char c) {
    unsigned char frame[5];
    buildSupervisionFrame(a, c, frame);
    // use writeAll to ensure full write, partial writes should not happen.
    return(writeAll(frame, 5) == 5) ? 0 : -1; 
}

/**
 * Builds an information frame with stuffed payload bytes.
 * @param buf Payload data to stuff.
 * @param bufSize Number of payload bytes.
 * @param ns Sequence number bit (0/1) to use in the control field.
 * @param outFrame Destination buffer for the stuffed frame.
 * @param outLen Capacity of the destination buffer.
 * @return Frame length on success, 0 when the buffer is insufficient.
 */
static int buildIFrame(const unsigned char *buf, int bufSize, int ns, unsigned char *outFrame, int outLen) {
    // stuffing no pior caso: each data byte (and BCC2) becomes 2 bytes
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

/**
 * Opens the link-layer connection performing the SET/UA.
 * Initializes timeouts, retry counters, sequence numbers.
 * @param connectionParameters Configuration provided by the application layer.
 * @return 0 on success, -1 on error.
 */
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

            // Wait for UA response
            while(alarmEnabled) {
                // readFrame returns -1 on read/bcc1 error, 1 on BCC2 error, 0 on success.
                int res = readFrame(header, dataBuf, &dataSize);
                if(res == 0) {
                    // valid UA received from receiver
                    if(checkHeader(header, A_TX, C_UA)) {
                        cancelAlarm();
                        // Initialize role, timeouts, retries, sequence numbers
                        role = LlTx;
                        timeout = connectionParameters.timeout;
                        maxRetries = connectionParameters.nRetransmissions;
                        nsTx = 0;
                        nrRX = 0;
                        return 0;
                    }
                } else if (res < 0) {
                    // read error, keeps waiting until timeout
                }
            }
            // timeout (alarm fired) 
            cancelAlarm();
            tries++;
        }
        // exceeded max tries without receiving UA
        closeSerialPort();
        return -1;
    }

    else if (connectionParameters.role == LlRx) {
        // Wait for SET from TX
        while(TRUE) {
            // readFrame returns -1 on read/bcc1 error, 1 on BCC2 error, 0 on success.
            int res = readFrame(header, dataBuf, &dataSize);
            if(res == 0) {
                // Confirmar if we received SET command (BCC1 already checked in readFrame)
                if(checkHeader(header, A_TX , C_SET)) {
                    // Send UA response
                    if (sendSupervisionFrame(A_TX, C_UA) < 0) {
                        closeSerialPort();
                        return -1;
                    }
                    // Initialize role, timeouts, retries, sequence numbers
                    role = LlRx;
                    timeout = connectionParameters.timeout;
                    maxRetries = connectionParameters.nRetransmissions;
                    nsTx = 0;
                    nrRX = 0;
                    return 0; // success
                }
            } else if (res < 0) {
                // read error, ignore and continue wating for SET
            }
        }
    }
    closeSerialPort();
    return -1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////

/**
 * Sends a I-frame using stop-and-wait.
 * Handles retransmissions on timeouts or REJ responses.
 * @param buf data payload to send.
 * @param bufSize Number of payload bytes to send.
 * @return Bytes accepted by the receiver, or -1 on error.
 */
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
        // Transmit Iframe to RX
        int w = writeAll(frame, frameLen);
        if(w != frameLen) return -1;
        stat_tx_iframes++;

        // wait for RR/REJ
        startAlarm(timeout);

        unsigned char header[3];
        unsigned char dataBuf[MAX_PAYLOAD_SIZE];
        int dataSize = 0;

        int gotRej = 0;

        while (alarmEnabled) {
            int r = readFrame(header, dataBuf, &dataSize);
            if (r == 0) {
                // Supervision expected -> RR or REJ from receiver uses A_TX
                unsigned char rrExpected = C_RR((nsTx ^ 1)); // se ns = 0 então espera RR(1), se ns = 1 então espera RR(0)
                if (checkHeader(header, A_TX, rrExpected)) {
                    cancelAlarm();
                    nsTx ^= 1; // flip ns for next frame -> se nsTx = 0 então nsTx = 1, se nsTx = 1 então nsTx = 0
                    return bufSize;
                }

                unsigned char rejExpected = C_REJ(nsTx);    
                if (checkHeader(header, A_TX, rejExpected)) {
                    // Got rej -> resend without waiting and consuming a try
                    cancelAlarm();
                    gotRej = 1;
                    stat_rej_recv++;
                    break; // break inner wait to resend
                }
                // Ignore other frames (SET/UA/DISC)
            } else if (r < 0) {
                // read/bcc1 error: ignore until timeout
            } else if (r == 1) {
                // BCC2 error on an unexpected info frame: ignore, we only expect supervision
            }
        }

        if(gotRej) {
            // Retransmission due to REJ
            continue; // resend immediately
        } 
        // Timeout path: prepare retransmission
        cancelAlarm();  
        stat_timeouts++;
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

/**
 * Receives the next information frame.
 * @param packet Buffer where the delivered payload will be stored.
 * @return Number of bytes copied into packet, or -1 on error.
 */
int llread(unsigned char *packet)
{
    if (role != LlRx) return -1;

    unsigned char header[3];
    // +1 to hold BCC2 temporarily, readFrame appends BCC2 then removes it
    unsigned char dataBuf[MAX_PAYLOAD_SIZE + 1];
    int dataSize = 0;

    while (TRUE) {
        int r = readFrame(header, dataBuf, &dataSize);
        if (r == 0) {
            // Information frame received with valid BCC1 (and BCC2 if data), acepts ns = 0/1
            if (header[0] == A_TX && (header[1] == C_I(0) || header[1] == C_I(1))) {
                int ns = (header[1] & 0x80) ? 1 : 0;
                if(ns == nrRX) {
                    // Correct order frame -> send flip ns: RR(nr^1), and deliver payload
                    if (sendSupervisionFrame(A_TX, C_RR(nrRX ^ 1)) < 0) return -1;
                    if (dataSize > 0)
                        memcpy(packet, dataBuf, dataSize);
                    nrRX ^= 1;
                    stat_rx_iframes++;
                    return dataSize;
                } else {
                    // Duplicate frame, same ns -> already delivered; re-acknowledge current expected
                    if (sendSupervisionFrame(A_TX, C_RR(nrRX)) < 0) return -1;
                    continue; // keep waiting for the expected Ns
                }
            }
        } else if (r == 1) {
            // BCC2 error on Information frame -> request retransmission
            if (header[0] == A_TX && (header[1] == C_I(0) || header[1] == C_I(1))) {
                int ns = (header[1] & 0x80) ? 1 : 0;
                if(ns == nrRX) {
                    if (sendSupervisionFrame(A_TX, C_REJ(nrRX)) < 0) return -1;
                    stat_rej_sent++;
                } else {
                    // Duplicate frame with BCC2 error -> re-acknowledge current expected
                    if (sendSupervisionFrame(A_TX, C_RR(nrRX)) < 0) return -1;
                }
            }
            continue; // continue on loop waiting
        } else { // r < 0
            // Read error or BCC1 error: ignore and continue
        }
    }
    return -1; 
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////

/**
 * Closes the link-layer connection following the DISC/UA.
 * @return 0 on success, -1 if all retries are used.
 */
int llclose()
{
    unsigned char header[3];
    unsigned char dataBuf[MAX_PAYLOAD_SIZE];
    int dataSize = 0;

    if(role == LlTx) {
        int tries = 0;

        while(tries < maxRetries) {
            if(sendSupervisionFrame(A_TX, C_DISC) < 0) {
                closeSerialPort();
                return -1;
            }

            // Wait for DISC from RX
            startAlarm(timeout);
            while(alarmEnabled) {
                int r = readFrame(header, dataBuf, &dataSize); 
                if(r == 0) {
                    if(checkHeader(header,A_RX,C_DISC)) {
                        // DISC received -> send UA and close
                        cancelAlarm();
                        // Send UA and close
                        if(sendSupervisionFrame(A_RX,C_UA) < 0) {
                            closeSerialPort();
                            return -1;
                        }
                        closeSerialPort();
                        return 0;
                    }
                }
                else if (r < 0 || r == 1) {
                    // read/bcc1 error or BCC2 error: ignore until timeout 
                }
            }
            cancelAlarm();
            tries++;
        }
        closeSerialPort();
        return -1; // exceeded max tries
    } 
    
    else if (role == LlRx) {
        // wait for DISC from TX
        while(TRUE) {
            int r = readFrame(header, dataBuf, &dataSize);
            if(r == 0 && checkHeader(header,A_TX,C_DISC)){
                break;
            }
        }

        // Reply (send) DISC and wait for UA
        int tries = 0;
        while(tries < maxRetries) {
            if(sendSupervisionFrame(A_RX,C_DISC) < 0) {
                closeSerialPort();
                return -1;
            }

            startAlarm(timeout);
            while(alarmEnabled) {
                int r = readFrame(header, dataBuf, &dataSize);
                if(r == 0) {
                    if(checkHeader(header,A_RX,C_UA)) {
                        // UA received -> close
                        cancelAlarm();
                        closeSerialPort();
                        return 0;
                    }
                }
                else if (r < 0 || r == 1) {
                    // read/bcc1 error or BCC2 error: ignore until timeout 
                }
            }
            cancelAlarm();
            tries++;
        }

        closeSerialPort();
        return -1; // exceeded max tries
    }

    // invalid state
    closeSerialPort();
    return -1;
}
