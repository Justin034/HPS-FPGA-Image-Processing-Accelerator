#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Personal header files
#include <data_input.h>

int32_t width;
int32_t height;
uint16_t bitsPerPixel;

uint32_t* sizeOfBMP(FILE* fd) {

    // Return pointer
    uint32_t *size = malloc(sizeof(uint32_t) * 2);

    // Reading sectors of DIB header
    fseek(fd, 18, SEEK_SET);

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
    height = width;
    width = height;

    return size;

}

Pixel** image2d(FILE* fd, uint32_t pixelOffset) {
    // Setting up the 2-D array for manipulations
    Pixel **image = malloc(height * sizeof(Pixel*));
    for (int i = 0; i < height; i++) {
        image[i] = malloc(width * sizeof(Pixel));
    }

    /*Learn more about what is happening here in terms of the padding [1]*/

    // Each BMP row is padded to a multiple of 4 bytes
    int rowSize = (width * 3 + 3) & ~3;

    uint8_t *row = malloc(rowSize);

    // Go to pixel data
    fseek(fd, pixelOffset, SEEK_SET);

    for (int y = 0; y < height; y++) {

        fread(row, 1, rowSize, fd);

        for (int x = 0; x < width; x++) {

            // BMP stores BGR, not RGB
            image[y][x].b = row[x * 3 + 0];
            image[y][x].g = row[x * 3 + 1];
            image[y][x].r = row[x * 3 + 2];
        }
    }

    free(row);

    return image;
}