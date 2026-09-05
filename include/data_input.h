// data_input.h
#ifndef DATA_INPUT_H
#define DATA_INPUT_H

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

uint32_t* sizeOfBMP(FILE* fd);

#endif