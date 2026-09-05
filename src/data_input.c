#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Personal header files
#include <data_input.h>

uint32_t* sizeOfBMP(FILE* fd) {
    // Return pointer
    uint32_t *size = malloc(sizeof(uint32_t) * 2);

    // Reading sectors of DIB header
    fseek(fd, 18, SEEK_SET);

    int32_t width;
    int32_t height;
    uint16_t bitsPerPixel;

    fread(&width, sizeof(width), 1, fd);
    fread(&height, sizeof(height), 1, fd);

    fseek(fd, 28, SEEK_SET);
    fread(&bitsPerPixel, sizeof(bitsPerPixel), 1, fd);

    printf("Confirmation of the following sizes:\n");
    printf("Width: %d\n", width);
    printf("Height: %d\n", height);
    printf("Bits per pixel: %d\n", bitsPerPixel);

    // Checking file type size of the bmp file
    if (bitsPerPixel != 24) {
        printf("This example only supports 24-bit BMPs.\n");
        fclose(fd);
        return NULL;
    }

    // Store and launch
    size[0] = width;
    size[1] = height;

    return size;
    
}