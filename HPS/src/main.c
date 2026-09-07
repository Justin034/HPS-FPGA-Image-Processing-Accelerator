#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Personal header files
#include "data_input.h"

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

    // height = size[0], size[1] = size[1]
    uint32_t* size = sizeOfBMP(fd);
    Pixel** image = image2d(fd);

    /*Section to implement the DCT and quantizing steps to then store as a separate file type
    Future plan is to make another file in the future be able to reverse the process and then compare storage.*/


    // Erasing the image memory allocation
    for(int i = 0; i < size[0]; i++) {
        free(image[i]);
    }

    free(image);
    fclose(fd);
    
    return 0;

}