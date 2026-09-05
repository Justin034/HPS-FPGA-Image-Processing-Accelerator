#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Pixel;

int main(int argc, char* argv) {

    // file input check
    if (argc == 1 || argc > 3) {
        printf("There is either no input file or more than 1 was provided");
        return -1;
    }

    // file open check
    FILE *fd = fopen(argv[1], "rb");
    if (fd == NULL) {
        perror("Failed to open the file.");
        return -1;
    }

    // Reading sectors of the bmp file type
    uint16_t type;
    uint32_t fileSize;
    uint32_t pixelOffset;

    fread(&type, sizeof(type), 1, fd);
    fread(&fileSize, sizeof(fileSize), 1, fd);

    fseek(fd, 10, SEEK_SET);
    fread(&pixelOffset, sizeof(pixelOffset), 1, fd);

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
    printf("Width: %d", width);
    printf("Height: %d", height);
    printf("Bits per pixel: %d", bitsPerPixel);

    // Checking file type size of the bmp file
    if (bitsPerPixel != 24) {
        printf("This example only supports 24-bit BMPs.\n");
        fclose(fd);
        return -1;
    }
    
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

    /*End of [1]*/

    // Example: print first pixel
    printf("R=%d G=%d B=%d\n",
           image[0][0].r,
           image[0][0].g,
           image[0][0].b);

    // Erasing the image memory allocation
    for(int i = 0; i < height; i++) {
        free(image[i]);
    }

    free(image);
    fclose(fd);
    
    return 0;
}