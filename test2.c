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
            perror("Errore per aprire il file");
            return 1;
        }

        do {
            
            printf("Dimmi il numero di byte da leggere: ");
            scanf("%zu", &buffer_size);
            getchar(); 

            
            bytes_read = read(fd, buffer, buffer_size);
            if (bytes_read == -1) {
                perror("Errore di lettura nel file");
                close(fd);
                return 1;
            }

            
            printf("Bytes letti: %zd\n", bytes_read);
            printf("Contenuto del buffer: %.*s\n", (int)bytes_read, buffer);

        
            printf("Continuare a leggere sullo stesso fd? (y/n): ");
            scanf(" %c", &choice);
            getchar(); 
        } while (choice == 'y' || choice == 'Y');
        close(fd);
        printf("Continuare avviando un nuovo fd? (y/n): ");
        scanf(" %c", &choice);
        getchar(); 
    }while (choice == 'y' || choice == 'Y');

    return 0;
}