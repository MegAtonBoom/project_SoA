/*
 *  Basic program to test the read VFS function 
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#define MAX_BUFFER_SIZE 10000

int main() {
    int fd;
    char buffer[MAX_BUFFER_SIZE];
    ssize_t bytes_read;
    size_t buffer_size;
    char choice;

    // Open the file
    do{
        fd = open("msgfs/msg-file", O_RDONLY);
        if (fd == -1) {
            perror("Error opening the file");
            return 1;
        }

        do {
            
            printf("Tell me the number of bytes to read <10000: ");
            scanf("%zu", &buffer_size);
            getchar(); 

            
            bytes_read = read(fd, buffer, buffer_size);
            if (bytes_read == -1) {
                perror("Error in the file read");
                close(fd);
                return 1;
            }

            
            printf("Read bytes: %zd\n", bytes_read);
            printf("Buffer content: %.*s\n", (int)bytes_read, buffer);

        
            printf("Keep reading using the same fd? (y/n): ");
            scanf(" %c", &choice);
            getchar(); 
        } while (choice == 'y' || choice == 'Y');
        close(fd);
        printf("Keep reading, but with a new fd? (y/n): ");
        scanf(" %c", &choice);
        getchar(); 
    }while (choice == 'y' || choice == 'Y');

    return 0;
}