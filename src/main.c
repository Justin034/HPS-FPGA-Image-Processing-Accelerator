#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Personal header files
#include <data_input.h>

int main(int argc, char* argv[]) {
    
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

    // Determine pixel offset
    uint32_t pixelOffset;
    fseek(fd, 10, SEEK_SET);
    fread(&pixelOffset, sizeof(pixelOffset), 1, fd);

    // height = size[0], size[1] = size[1]
    uint32_t* size = sizeOfBMP(fd);

    // Setting up the 2-D array for manipulations
    Pixel **image = malloc(size[0] * sizeof(Pixel*));
    for (int i = 0; i < size[0]; i++) {
        image[i] = malloc(size[1] * sizeof(Pixel));
    }

    /*Learn more about what is happening here in terms of the padding [1]*/

    // Each BMP row is padded to a multiple of 4 bytes
    int rowSize = (size[1] * 3 + 3) & ~3;

    uint8_t *row = malloc(rowSize);

    // Go to pixel data
    fseek(fd, pixelOffset, SEEK_SET);

    for (int y = 0; y < size[0]; y++) {

        fread(row, 1, rowSize, fd);

        for (int x = 0; x < size[1]; x++) {

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
    for(int i = 0; i < size[0]; i++) {
        free(image[i]);
    }

    free(image);
    fclose(fd);
    
    return 0;

}