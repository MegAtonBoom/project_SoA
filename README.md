# Advanced Operating Systems Project - 2023
## Tor Vergata University
### Author: Adrian Petru Baba ( 0320578 )

### Short description: 

The project implements a file system, a block device with only one file. The file has fixed max. size in number of blocks.

There is a file with three new system calls that implement a read on the file, a write and an invalidate.

There is a file with the read file operation, and the open and release needed to make it work properly.

There is a c formatter, to format the new device: just writes the superblock properly 

there are 2 test programs. They are trivial: **_don't_** try doing things like writing "a" when a buffer size number is requested please!

### How to use:

After downloading it, get in the main directory with the terminal and then:

  - Get in "syscall_table_discovery";
  - Type "make", "sudo sh load.sh"

Go back to the main directory and:

  - Type "make", "make compile", "sudo make insmod", "sudo make create-fs", "sudo make mount-fs"
    -everything shouyld be up and running now: there will be a new "msgfs" directory in the main directory with the only file names "msg-file"

Want to test it?

  - Use test1 to test the system calls, test2 to test the read operation. Follow the instructions.

Want to clear everything?

  - From the main directory type "sudo make unmount-fs", "sudo make rmmod", "make clean"
  - Form the "syscall_table_discovery" directory type "sudo sh unload.sh", "make clean"

Extra informations:

  - The fixed upperbound of supported blocks (as requested) is 20. It can be changed from the Makefile in the main directory, at row 4, changing the EXTRA_CFLAGS value for MAXBLOCKS
  - The fixed value for the device number of blocks is 10 (meaning 8 for user data). It can be changed from the same Makefile, changing the value of "count" parameter in the create-fs at row 24
  - The device write updates might also not be synchronized, to do so just delete the "-D SYNCHRONIZE" in the EXTRA_CFLAGS
