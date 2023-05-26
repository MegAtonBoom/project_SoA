#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define WRITE_SYSCALL_NUM 134
#define READ_SYSCALL_NUM 156
#define INVALIDATE_SYSCALL_NUM 174
#define MAX_MESSAGE_SIZE 100

int main() {
    int choice;
    char message[MAX_MESSAGE_SIZE];
    int offset, size;
    int result;

    do {
        printf("Scegli un'opzione:\n");
        printf("1. Scrittura\n");
        printf("2. Lettura\n");
        printf("3. Invalidazione\n");
        printf("0. Esci\n");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                printf("Inserisci il messaggio: ");
                scanf(" %[^\n]", message);

                size = strlen(message);
                printf("messaggio lungo: %ld\n", strlen(message));

                result = syscall(WRITE_SYSCALL_NUM, message, size);
                printf("Risultato scrittura: %d\n", result);

                break;

            case 2:
                printf("Inserisci l'offset: ");
                scanf("%d", &offset);

                printf("Inserisci la dimensione: ");
                scanf("%d", &size);

                char* buffer = calloc(1, size);

                result = syscall(READ_SYSCALL_NUM, offset, buffer, size);
                printf("Risultato lettura: %d\n", result);
                printf("Ho letto: %s\n", buffer);
                fflush(stdout);

                free(buffer);

                break;

            case 3:
                printf("Inserisci l'offset: ");
                scanf("%d", &offset);

                result = syscall(INVALIDATE_SYSCALL_NUM, offset);
                printf("Risultato invalidazione: %d\n", result);

                break;

            case 0:
                printf("Uscita dal programma.\n");
                exit(0);

            default:
                printf("Opzione non valida. Riprova.\n");
                break;
        }

        printf("Vuoi uscire (0) o tornare al menù (1)? ");
        scanf("%d", &choice);

    } while (choice == 1);

    return 0;
}