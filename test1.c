/*
 *  Basic program to test the syscalls in non concurrent way
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define WRITE_SYSCALL_NUM 134
#define READ_SYSCALL_NUM 156
#define INVALIDATE_SYSCALL_NUM 174
#define MAX_MESSAGE_SIZE 4096

int main() {
    int choice;
    //only to test values > than the max allowed message size
    char message[MAX_MESSAGE_SIZE];
    int offset, size;
    int result;

    do {
        printf("Pick an option:\n");
        printf("1. Write\n");
        printf("2. Read\n");
        printf("3. Invalidate\n");
        printf("0. Quit\n");
        scanf("%d", &choice);

        switch (choice) {
            case 1:
                memset(message, 0, sizeof(message));
                printf("Please insert the message: ");
                scanf(" %[^\n]", message);              
                size = strlen(message);
                printf("message len: %d\n", size);

                result = syscall(WRITE_SYSCALL_NUM, message, size);
                if(result<0){
                    printf("write result: %d; that means the message could not be inserted \n", result);
                }
                else{
                    printf("write result (offset of the message): %d\n", result);
                }
                fflush(stdout);

                
                break;

            case 2:
                printf("Tell me the offset: ");
                scanf("%d", &offset);

                printf("Tell me the size: ");
                scanf("%d", &size);

                memset(message, 0, sizeof(message));

                result = syscall(READ_SYSCALL_NUM, offset, message, size);
                if(result<0){
                    printf("The message at given offset can't be read\n");
                }
                else{
                    printf("Read result: %d\n", result);
                    printf("I've read: %s\n", message);
                }
                fflush(stdout);

                

                break;

            case 3:
                printf("Tell me the offset: ");
                scanf("%d", &offset);

                result = syscall(INVALIDATE_SYSCALL_NUM, offset);
                if(result<0){
                    printf("The message at given offset can't be invalidate\n");
                }
                printf("Invalidate result: %d\n", result);

                break;

            case 0:
                printf("Quitting.\n");
                exit(0);

            default:
                printf("Invalid option.\n");
                break;
        }

        printf("Quit (0) or go back to the menu (1)? ");
        scanf("%d", &choice);

    } while (choice == 1);

    return 0;
}
