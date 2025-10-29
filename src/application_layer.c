// Application layer protocol implementation using START/DATA/END packets
#include "application_layer.h"
#include "link_layer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Application protocol opcodes
#define C_DATA  0x01
#define C_START 0x02
#define C_END   0x03

// TLV tags
#define T_FILESIZE 0x00
#define T_FILENAME 0x01

// DATA payload bytes (rest are header fields)
#define APP_PAYLOAD_SIZE 996

// Packet helpers ------------------------------------------------------------
static unsigned int big_endian_len(uint64_t value) {
    unsigned int n = 1;
    while (value >> (n * 8)) n++;
    return n;
}

static int build_control_packet(unsigned char control,
                                const char *filename,
                                uint64_t filesize,
                                unsigned char *out,
                                size_t outCap)
{
    unsigned int lsize = big_endian_len(filesize);
    unsigned int lname = (unsigned int)strlen(filename);
    size_t needed = 1 + 1 + 1 + lsize + 1 + 1 + lname;
    if (needed > outCap) return -1;

    size_t i = 0;
    out[i++] = control;
    out[i++] = T_FILESIZE;
    out[i++] = (unsigned char)lsize;
    for (int b = (int)lsize - 1; b >= 0; --b) {
        out[i + b] = (unsigned char)(filesize & 0xFF);
        filesize >>= 8;
    }
    i += lsize;
    out[i++] = T_FILENAME;
    out[i++] = (unsigned char)lname;
    memcpy(&out[i], filename, lname);
    i += lname;
    return (int)i;
}

static int parse_control_packet(const unsigned char *pkt, int len,
                                uint64_t *filesize, char *nameBuf, size_t nameCap)
{
    if (len < 1) return -1;
    if (pkt[0] != C_START && pkt[0] != C_END) return -1;

    int i = 1;
    int foundSize = 0;
    while (i + 2 <= len) {
        unsigned char type = pkt[i++];
        unsigned char l = pkt[i++];
        if (i + l > len) return -1;
        if (type == T_FILESIZE) {
            uint64_t v = 0;
            for (int b = 0; b < l; ++b) v = (v << 8) | pkt[i + b];
            *filesize = v;
            foundSize = 1;
        } else if (type == T_FILENAME) {
            size_t copy = (l < nameCap - 1) ? l : nameCap - 1;
            memcpy(nameBuf, &pkt[i], copy);
            nameBuf[copy] = '\0';
        }
        i += l;
    }
    return foundSize ? 0 : -1;
}

static int build_data_packet(unsigned char seq,
                             const unsigned char *payload,
                             unsigned int payloadLen,
                             unsigned char *out,
                             size_t outCap)
{
    if (payloadLen > APP_PAYLOAD_SIZE) return -1;
    size_t need = 4 + payloadLen;
    if (need > outCap) return -1;
    out[0] = C_DATA;
    out[1] = seq;
    out[2] = (unsigned char)((payloadLen >> 8) & 0xFF);
    out[3] = (unsigned char)(payloadLen & 0xFF);
    if (payloadLen) memcpy(&out[4], payload, payloadLen);
    return (int)need;
}

// TX path ------------------------------------------------------------------
static int run_transmitter(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("[APP][TX] fopen"); return -1; }
    setvbuf(f, NULL, _IOFBF, 1 << 20);

    if (fseek(f, 0, SEEK_END) != 0) { perror("[APP][TX] fseek"); fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { perror("[APP][TX] ftell"); fclose(f); return -1; }
    rewind(f);

    unsigned char packet[MAX_PAYLOAD_SIZE];

    int ctrlLen = build_control_packet(C_START, filename, (uint64_t)sz,
                                       packet, sizeof(packet));
    if (ctrlLen < 0 || llwrite(packet, ctrlLen) < 0) {
        printf("[APP][TX] failed to send START\n");
        fclose(f);
        return -1;
    }

    unsigned char seq = 0;
    size_t totalSent = 0;
    for (;;) {
        size_t rd = fread(&packet[4], 1, APP_PAYLOAD_SIZE, f);
        if (rd == 0) break;
        int dataLen = build_data_packet(seq, &packet[4], (unsigned int)rd,
                                        packet, sizeof(packet));
        if (dataLen < 0 || llwrite(packet, dataLen) < 0) {
            printf("[APP][TX] llwrite error on seq=%u\n", (unsigned)seq);
            fclose(f);
            return -1;
        }
        totalSent += rd;
        seq = (unsigned char)((seq + 1) & 0xFF);
    }

    ctrlLen = build_control_packet(C_END, filename, (uint64_t)sz,
                                   packet, sizeof(packet));
    if (ctrlLen >= 0) (void)llwrite(packet, ctrlLen);

    fclose(f);
    printf("[APP][TX] data bytes sent = %zu\n", totalSent);
    return 0;
}

// RX path ------------------------------------------------------------------
static int run_receiver(const char *outFilename)
{
    unsigned char packet[MAX_PAYLOAD_SIZE];
    int len;
    uint64_t expectedSize = 0;
    char txName[256] = {0};

    // Wait START
    for (;;) {
        len = llread(packet);
        if (len <= 0) continue;
        if (packet[0] == C_START &&
            parse_control_packet(packet, len, &expectedSize, txName, sizeof(txName)) == 0) {
            printf("[APP][RX] START: size=%llu, name=%s\n",
                   (unsigned long long)expectedSize,
                   txName[0] ? txName : "(n/a)");
            break;
        }
    }

    FILE *out = fopen(outFilename, "wb");
    if (!out) { perror("[APP][RX] fopen"); return -1; }
    setvbuf(out, NULL, _IOFBF, 1 << 20);

    unsigned char expectSeq = 0;
    uint64_t written = 0;
    int done = 0;

    while (!done) {
        len = llread(packet);
        if (len <= 0) continue;

        if (packet[0] == C_DATA) {
            if (len < 4) continue;
            unsigned char seq = packet[1];
            unsigned int payloadLen = ((unsigned int)packet[2] << 8) | packet[3];
            if (payloadLen > APP_PAYLOAD_SIZE || (int)(4 + payloadLen) > len) continue;
            if (seq != expectSeq) {
                // duplicate/out-of-order -> ignore, link layer should prevent this
                continue;
            }
            size_t wr = fwrite(&packet[4], 1, payloadLen, out);
            if (wr != payloadLen) { perror("[APP][RX] fwrite"); break; }
            written += payloadLen;
            expectSeq = (unsigned char)((expectSeq + 1) & 0xFF);
        } else if (packet[0] == C_END) {
            done = 1;
        }
    }

    fclose(out);
    printf("[APP][RX] data bytes received = %llu\n", (unsigned long long)written);
    return 0;
}

// Application entry --------------------------------------------------------
void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer ll = {0};
    strncpy(ll.serialPort, serialPort, sizeof(ll.serialPort) - 1);
    ll.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    ll.baudRate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;

    printf("[APP] calling llopen (role=%s)\n", role);
    if (llopen(ll) != 0) {
        printf("[APP] llopen FAILED\n");
        return;
    }

    int status = (ll.role == LlTx) ? run_transmitter(filename)
                                   : run_receiver(filename);

    printf("[APP] calling llclose\n");
    int cr = llclose();
    printf("[APP] llclose %s\n", (cr == 0) ? "OK" : "FAILED");

    if (status != 0) {
        printf("[APP] completed with errors\n");
    }
}
