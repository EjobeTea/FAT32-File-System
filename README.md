# FAT32-File-System
Operating Systems - Spring 2022

## Compile and Execute 
gcc -o mfs mfs.c -std=c99 </br>
mfs

## Commands
Commands         | Explanation
-------------    | -------------
open (FILENAME)  | Opens a fat32 image, where (FILENAME) is the fat32 image.
close            | Closes the fat32 image.
info             | Prints out information about the file system, in both hexadecimal and base 10.
stat (FILENAME)  | Print the attributes and starting cluster number of the file name.
stat (DIRNAME)   | Print the attributes and starting cluster number of the directory name.
get  (FILENAME)  | Retrieves the file from FAT 32's image and puts it in current directory.
cd   (DIRECTORY) | Command changes the current working directory to the directory specified by (DIRECTORY).
ls               | Lists the current directory.
del  (NAME)      | Deletes from the file system, where (NAME) is specified file or directory.
undel (NAME)     | Will undo delete from file system, where (NAME) is specified file or directory.
