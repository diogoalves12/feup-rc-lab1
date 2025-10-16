#ifndef FRAME_SM_H
#define FRAME_SM_H

#include <stddef.h>


int readFrame(unsigned char *header, unsigned char *data, int *dataSize, int expectedA, int expectedC);

#endif // FRAME_SM_H
