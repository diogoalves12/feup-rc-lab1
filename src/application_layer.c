// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Application control field
#define C_DATA  0x01
#define C_START 0x02
#define C_END   0x03

// TLV tags
#define T_FILESIZE 0x00
#define T_FILENAME 0x01

// Limits
#define APP_PAYLOAD_SIZE 996


/////////////////////////////////////////////////
// Helper functions
/////////////////////////////////////////////////

/**
 * @brief Computes how many bytes are needed to represent a uint64_t in big-endian.
 * @param value The value to be measured.
 * @return Number of bytes (>0) required to represent @p value.
 */
static unsigned int big_endian_len(uint64_t value) {
    unsigned int n = 1;
    while (value >> (n * 8)) n++;
    return n;
}

/**
 * @brief Builds a control packet (C_START or C_END) with the following layout:
 *        [C][T_FILESIZE][lenSize][size (big-endian, lenSize bytes)]
 *        [T_FILENAME][lenName][filename (lenName bytes)]
 *
 *        The filename length must fit in a single byte, TLV length (0 - 255).
 *
 * @param control  C_START or C_END.
 * @param filename File name to advertise (lenName 0-255).
 * @param filesize File size in bytes.
 * @param[out] out Output buffer where the packet is written.
 * @param outLen   Len of @p out.
 *
 * @return Packet length (>=0) on success, or -1 on error/insufficient capacity.
 */
static int buildControlPacket(unsigned char control, const char *filename, uint64_t filesize, unsigned char *out, size_t outLen)
{
    unsigned int lenSize = big_endian_len(filesize);

    // filename has 1 octet (byte, 0-255) in TLV 
    unsigned int lenName = (unsigned int)strlen(filename);
    if (lenName > 255) return -1;

    // Total: C + (T, L, Size[L]) + (T, L, Name[L])
    size_t totalLen = 1 + 1 + 1 + lenSize + 1 + 1 + lenName;
    if (totalLen > outLen) return -1;

    size_t i = 0;

    // Control field (1 - start, 3 - end)
    out[i++] = control;
    // TLV: FILESIZE
    out[i++] = T_FILESIZE;
    out[i++] = (unsigned char)lenSize;

    // Write filesize in big-endian (most significant byte first, extracts LSBs and place from right to left, MSB->LSB)
    for (int b = (int)lenSize - 1; b >= 0; --b) {
        out[i + b] = (unsigned char)(filesize & 0xFF);
        filesize >>= 8;
    }
    i += lenSize;

    // TLV: FILENAME
    out[i++] = T_FILENAME;
    out[i++] = (unsigned char)lenName;
    memcpy(&out[i], filename, lenName);
    i += lenName;

    return (int)i;
}

/**
 * @brief Parses a control packet (C_START or C_END).
 *        Extracts the filesize (if present) and the filename (if present).
 *
 * @param packet Pointer to the received packet bytes.
 * @param len Packet length in bytes.
 * @param[out] filesize Receives the parsed file size (if present).
 * @param[out] nameBuf Buffer to store the parsed file name (NUL-terminated).
 * @param nameBufLen  Capacity of @p nameBuf.
 *
 * @return 0 on success (filesize found), or -1 on invalid format/error.
 */
static int parseControlPacket(const unsigned char *packet, int len, uint64_t *filesize, char *nameBuf, size_t nameBufLen)
{
    // At least the C field must be present
    if (len < 1) return -1;
    if (packet[0] != C_START && packet[0] != C_END) return -1;

    int i = 1; // current index in packet
    int foundSize = 0; // set once T_FILESIZE is parsed

    // Iterate TLVs: [T][L][V...]
    while (i + 2 <= len) {
        unsigned char type = packet[i++];
        unsigned char l = packet[i++];
        if (i + l > len) return -1; // TLV exceeds packet bounds

        if (type == T_FILESIZE) {
            // Rebuild big-endian value
            uint64_t v = 0;
            for (int b = 0; b < l; ++b) v = (v << 8) | packet[i + b];
            *filesize = v;
            foundSize = 1;
        } else if (type == T_FILENAME) {
            // Copy with NUL termination safeguard
            size_t copy = (l < nameBufLen - 1) ? l : nameBufLen - 1;
            memcpy(nameBuf, &packet[i], copy);
            nameBuf[copy] = '\0';
        }
        i += l;
    }
    return foundSize ? 0 : -1;
}

/**
 * @brief Builds a data packet with layout:
 *        [C_DATA][SEQ][L1][L2][payload...]
 *        where L = (L1<<8)|L2 and 0 ≤ L ≤ APP_PAYLOAD_SIZE.
 *
 * @param seq        Packet sequence number (0..255).
 * @param payload    Pointer to the data bytes.
 * @param payloadLen Number of payload bytes.
 * @param[out] out   Output buffer where the packet is written.
 * @param outLen     Capacity of @p out.
 *
 * @return Packet length (>=0) on success, or -1 on error.
 */
static int buildDataPacket(unsigned char seq, const unsigned char *payload, unsigned int payloadLen, unsigned char *out, size_t outLen)
{
    if (payloadLen > APP_PAYLOAD_SIZE) return -1;

    // C + SEQ + L1 + L2 + payload
    size_t need = 4 + payloadLen;
    if (need > outLen) return -1;

    out[0] = C_DATA;
    out[1] = seq;
    out[2] = (unsigned char)((payloadLen >> 8) & 0xFF);
    out[3] = (unsigned char)(payloadLen & 0xFF);
    if (payloadLen) memcpy(&out[4], payload, payloadLen);

    return (int)need;
}

////////////////////////////////////////////////////////////////////////////////
// Transmitter (TX) path
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Transmitter path: sends C_START, a sequence of C_DATA, then C_END.
 *
 * @param filename Path to the file to transmit.
 * @return 0 on success, -1 on I/O or llwrite() failure.
 */
static int run_transmitter(const char *filename)
{
    // Open source file in binary mode
    FILE *f = fopen(filename, "rb");
    if (!f) { perror("[APP][TX] fopen"); return -1; }

    // Large buffering for efficient I/O (optional)
    setvbuf(f, NULL, _IOFBF, 1 << 20);

    // Determine file size
    if (fseek(f, 0, SEEK_END) != 0) { perror("[APP][TX] fseek"); fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { perror("[APP][TX] ftell"); fclose(f); return -1; }
    rewind(f);

    // Workspace for building application packets
    unsigned char packet[MAX_PAYLOAD_SIZE];

    // Send START (metadata)
    int ctrlLen = buildControlPacket(C_START, filename, (uint64_t)sz, packet, sizeof(packet));
    if (ctrlLen < 0 || llwrite(packet, ctrlLen) < 0) {
        printf("[APP][TX] failed to send START\n");
        fclose(f);
        return -1;
    }

    // Send DATA sequence in chunks up to APP_PAYLOAD_SIZE
    unsigned char seq = 0;
    size_t totalSent = 0;

    while (TRUE) {
        // Read directly into the future payload area of 'packet'
        size_t rd = fread(&packet[4], 1, APP_PAYLOAD_SIZE, f);
        if (rd == 0) break; // EOF

        int dataLen = buildDataPacket(seq, &packet[4], (unsigned int)rd, packet, sizeof(packet));
        if (dataLen < 0 || llwrite(packet, dataLen) < 0) {
            printf("[APP][TX] llwrite error on seq=%u\n", (unsigned)seq);
            fclose(f);
            return -1;
        }
        totalSent += rd;
        seq = (unsigned char)((seq + 1) & 0xFF);
    }

    // Send END (best-effort even if it fails)
    ctrlLen = buildControlPacket(C_END, filename, (uint64_t)sz, packet, sizeof(packet));
    if (ctrlLen >= 0) (void)llwrite(packet, ctrlLen);

    fclose(f);
    printf("[APP][TX] data bytes sent = %zu\n", totalSent);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Receiver (RX) path
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Receiver path: waits for C_START, receives ordered C_DATA packets,
 *        and finishes on C_END. Writes the received bytes to the output file.
 *
 * @param outFilename Path to the destination file.
 * @return 0 on success, -1 on I/O errors.
 */
static int run_receiver(const char *outFilename)
{
    unsigned char packet[MAX_PAYLOAD_SIZE];
    int len;
    uint64_t expectedSize = 0;
    char txName[256] = {0};

    // Wait for START
    for (;;) {
        len = llread(packet);
        if (len <= 0) continue;
        if (packet[0] == C_START &&
            parseControlPacket(packet, len, &expectedSize, txName, sizeof(txName)) == 0) {
            printf("[APP][RX] START: size=%llu, name=%s\n", (unsigned long long)expectedSize, txName[0] ? txName : "(n/a)");
            break;
        }
    }

    // Open output file in binary mode
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
            // Minimum header size is 4 bytes: C / SEQ / L1 / L2
            if (len < 4) continue;

            unsigned char seq = packet[1];
            unsigned int payloadLen = ((unsigned int)packet[2] << 8) | packet[3];

            // Guard against malformed lengths
            if (payloadLen > APP_PAYLOAD_SIZE || (int)(4 + payloadLen) > len) continue;

            // Duplicate or out-of-order (link layer should prevent, but be safe)
            if (seq != expectSeq) continue;

            size_t wr = fwrite(&packet[4], 1, payloadLen, out);
            if (wr != payloadLen) { perror("[APP][RX] fwrite"); break; }
            written += payloadLen;
            expectSeq = (unsigned char)((expectSeq + 1) & 0xFF);

        } else if (packet[0] == C_END) {
            // Sanity check: total received should match the advertised size
            if (written != expectedSize) {
                fprintf(stderr,
                        "[APP][RX] received size (%llu) != expected size (%llu)\n",
                        (unsigned long long)written,
                        (unsigned long long)expectedSize);
            }
            done = 1;
        }
    }

    fclose(out);
    printf("[APP][RX] data bytes received = %llu\n", (unsigned long long)written);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Application entry point
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Application layer entry point. Opens the link-layer connection (llopen),
 *        runs TX or RX according to @p role, and then closes the link (llclose).
 *
 * @param serialPort Path to the serial port device.
 * @param role       "tx" for transmitter, otherwise receiver.
 * @param baudRate   Serial baud rate (must match link-layer support).
 * @param nTries     Maximum number of retransmissions at the link layer.
 * @param timeout    Timeout (seconds) per attempt at the link layer.
 * @param filename   File path used by the TX (source) or RX (destination).
 */
void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // Initialize link-layer configuration
    LinkLayer ll = (LinkLayer){0};
    strncpy(ll.serialPort, serialPort, sizeof(ll.serialPort) - 1);
    ll.role = (strcmp(role, "tx") == 0) ? LlTx : LlRx;
    ll.baudRate = baudRate;
    ll.nRetransmissions = nTries;
    ll.timeout = timeout;

    // Open link-layer connection (performs SET/UA handshake)
    printf("[APP] calling llopen (role=%s)\n", role);
    if (llopen(ll) != 0) {
        printf("[APP] llopen FAILED\n");
        return;
    }

    // Run TX or RX path
    int status = (ll.role == LlTx) ? run_transmitter(filename) : run_receiver(filename);

    // Close link-layer connection (performs DISC/UA)
    printf("[APP] calling llclose\n");
    int cr = llclose();
    printf("[APP] llclose %s\n", (cr == 0) ? "OK" : "FAILED");

    if (status != 0) {
        printf("[APP] completed with errors\n");
    }
}
