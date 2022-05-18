/* 
 * Commands:
 
 * open (FILENAME)  -- opens a fat32 image
 * close            -- closes the fat32 image
 * info             -- prints out information about the file system in both hexadecimal and base 10
 * stat (FILENAME)  -- print the attributes and starting cluster number of the file name
 * stat (DIRNAME)   -- print the attributes and starting cluster number of the directory name
 * get  (FILENAME)  -- retrieves the file from FAT 32's image and puts it in your current directory
 * cd   (DIRECTORY) -- command changes the current directory to the given directory
 * ls               -- lists the current directory
 * del  (FILENAME)  -- deletes from the file system
 * undel            -- will undo delete from file system
 
 * read (FILENAME)(POSITION)(NUMBER OF BYTES) -- Reads file at the position, in bytes.                 
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_NUM_ARGUMENTS 4
#define MAX_FILENAME      100

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

int16_t NextLB( uint32_t sector );
int LBAToOffset(int32_t sector);
int equality(char ** token);

//Each record represented by:
struct __attribute__((__packed__)) DirectoryEntry{
    char      DIR_Name[11];
    uint8_t   DIR_Attr;
    uint8_t   Unused1[8];
    uint8_t   Unused2[4];
    uint16_t  DIR_FirstClusterHigh;
    uint16_t  DIR_FirstClusterLow;
    uint32_t  Dir_FileSize;
};

struct DirectoryEntry dir[16];

//Boot Sector:
char BS_OEMName[8]; //Name string. Some system ignore, call it "MSWIN4.1".
char BS_VolLab[11]; //Volume label. Matches 11 byte volume recorded in root directory.

bool open_flag = true; // fp opened for first time. true means available.

//BIOS Parameter Blocks:
int8_t  BPB_SecPerClus;  //Number of sectors per allocation unit. Value is a power of 2.
int8_t  BPB_NumFATs;     //Counts of FAT data structs on the volume. Always value 2 (redundancy).
int16_t BPB_BytesPerSec; //Counts the numbers of bytes per sector. May be 512, 1024, 2048, or 4096.
int16_t BPB_RootEntCnt;  //Has count 32-byte directory entries in root dir. Must be 0.
int16_t BPB_RsvdSecCnt;  //Number of reserved sectors in Reserved region of volume at first sector.
int32_t BPB_FATsz32;     //32 bit count of sectors occpied by ONE FAT.
int32_t BPB_RootClus;    //Set to cluster number of first cluster of root dir, usually 2.

int32_t RootDirSectors       = 0; //Count for root directory sector.
int32_t FirstDataSector      = 0; //Location for first data sector.
int32_t FirstSectorofCluster = 0; //Location for first sector of cluster.


FILE * fp  = NULL;

struct DirectoryEntry dir[16]; 

int root_dir = 0; //Gets set when you "open" fat32-1.img

int main(){
    char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

    char * previous_token = NULL; //Used to detect if file was already opened.

    while( 1 ){
        // Print out the mfs prompt
        printf ("mfs> ");

        // Read the command from the commandline.  The
        // maximum command that will be read is MAX_COMMAND_SIZE
        // This while command will wait here until the user
        // inputs something since fgets returns NULL when there
        // is no input
        while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

        /* Parse input */
        char *token[MAX_NUM_ARGUMENTS];

        int   token_count = 0;                                 
                                                               
        // Pointer to point to the token
        // parsed by strsep
        char *arg_ptr;                                         
                                                               
        char *working_str  = strdup( cmd_str );                

        // we are going to move the working_str pointer so
        // keep track of its original value so we can deallocate
        // the correct amount at the end
        char *working_root = working_str;

        // Tokenize the input stringswith whitespace used as the delimiter
        while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) && 
                  (token_count<MAX_NUM_ARGUMENTS))
        {

            token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
            if( strlen( token[token_count] ) == 0 ){
                token[token_count] = NULL;
            }
            token_count++;
        }

        if(strcmp(token[0], "ls") == 0){

            //Catch if the file system was even open
            if(!fp){
                printf("Error: File system not open.\n");
            }

            //Loop over our directory.
            int ls_i = 0;
            while(ls_i < 16 && (fp)){
                if((dir[ls_i].DIR_Attr == 0x01 || dir[ls_i].DIR_Attr == 0x10 || 
                    dir[ls_i].DIR_Attr == 0x20) && dir[ls_i].DIR_Attr != 0x02){

                    char * name[12]; //11 characters + one for the null

                    //Copy 11 bytes from the dir array we loop over and send it over to the name
                    memcpy(name,dir[ls_i].DIR_Name, 11);

                    //End will null terminate and print it:
                    name[11] = '\0';

                    printf("%s \n", name);
                }
                ++ls_i;
            }

        }
        if(strcmp(token[0], "cd") == 0){
            // User types "cd foo" where foo is the directory.
            // Note: Only handles relative paths, not absolute paths

            if(fp){
                // delimiter:
                char * directory = strtok(token[1], "/");

                //Compare directories & folders with user token:
                int result = -2;

                //Special case for current directory.
                if(strcmp(token[1], ".") != 0){
                    //Else find directory location.
                    result = equality(&token[1]);
                }

                //Special case for changing to previous directory:
                if(strcmp(token[1], "..") == 0){
                    result = 0;
                }

                if(result == -1){
                    printf("Error: Could not find folder or directory in the file image system\n");
                }
                else if(result == -2){
                    //Do nothing. Stay in current directory.
                }
                else if(result >= 0){
                    int cluster = dir[result].DIR_FirstClusterLow;

                    //Special case for changing to previous directory:
                    if(strncmp(token[1], "..", 2) == 0){
                        cluster = 2;
                    }

                    int offset = LBAToOffset(cluster);

                    //Moves us to where directory is located:
                    fseek(fp, offset, SEEK_SET);

                    //Then read up to the directory structure:
                    fread(&dir[0], sizeof(struct DirectoryEntry), 16, fp);
                }
            }
            else{
                printf("Error: File system image must be opened first.\n");
            }
        }
        else if(strcmp(token[0], "open") == 0){
            if(open_flag == true){
                fp = fopen(token[1],"r");
            }

            if( !fp ){
                printf("Error: File system image not found.\n");
            }
            else if(strlen( token[1] ) > MAX_FILENAME){
                printf("Error: File name cannot exceed 100 characters.\n");
            }
            else if(previous_token){
                if(strcmp( previous_token, token[0] ) == 0 ){
                    printf("Error: File system image already opened.\n");
                }
            }
            else{
                //File exists, name does not exceed 100 characters, and not already opened.

                //Since it's already open, set open_flag to false.
                open_flag = false;

                //File information
                fseek(fp, 11, SEEK_SET);
                fread(&BPB_BytesPerSec, 2, 1, fp);

                fseek(fp, 13, SEEK_SET);
                fread(&BPB_SecPerClus, 1, 1, fp);

                fseek(fp, 14, SEEK_SET);
                fread(&BPB_RsvdSecCnt, 2, 1, fp);

                fseek(fp, 16, SEEK_SET);
                fread(&BPB_NumFATs, 1, 1, fp);

                fseek(fp, 36, SEEK_SET);
                fread(&BPB_FATsz32, 4, 1, fp);

                //This is where our root directory starts (start of cluster)
                root_dir = (BPB_NumFATs * BPB_FATsz32 * BPB_BytesPerSec) +(BPB_RsvdSecCnt * BPB_BytesPerSec);

                fseek(fp, root_dir, SEEK_SET);
                fread(dir, 16, sizeof( struct DirectoryEntry), fp);

                printf("Opened file successfully.\n");

                previous_token = token[0]; //Set to previous file name. Used to prevent re-opening.
            }
        }
        else if(strcmp(token[0], "close") == 0){
            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{
                previous_token = NULL; //File is not opened anymore; can be opened again.
                open_flag = true;
                fclose(fp);
                fp = NULL;
            }
        }
        else if(strcmp(token[0], "info") == 0){
            //Prints out info from the file system.
            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{
                printf("Param Block     Decimal\tHexademical\n");
                printf("-----------------------------------\n");

                printf("BPB_BytesPerSec\t   %d\t     %x\n",  BPB_BytesPerSec, BPB_BytesPerSec);
                printf("BPB_SecPerClus\t   %d\t     %x\n",   BPB_SecPerClus, BPB_SecPerClus);
                printf("BPB_RsvdSecCnt\t   %d\t     %x\n",   BPB_RsvdSecCnt, BPB_RsvdSecCnt);
                printf("BPB_NumFATs\t   %d\t     %x\n",      BPB_NumFATs, BPB_NumFATs);
                printf("BPB_FATsz32\t   %d\t     %x\n",      BPB_FATsz32, BPB_FATsz32);
            }

        }
        else if(strcmp(token[0], "stat") == 0){
            //Prints out the directory/file attributes and starting cluster number and size of file.

            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{
                int result = equality(&token[1]);

                if(result >= 0){
                    printf("Attribute Size Cluster\n");
                    printf("----------------------\n");
                    printf("   %d      %d     %d\n", dir[result].DIR_Attr, dir[result].Dir_FileSize, dir[result].DIR_FirstClusterLow  );    
                }
                else{
                    printf("Error: File not found\n");
                }

            }
        }
        else if(strcmp(token[0], "del") == 0){
            //Set first char to 0xe5 && keep in mind for 0x05

            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{

                // Must find if file name in token[1] exits. 
                // Result is the index in where it appears.
                int result = equality(&token[1]);
                if(result != -1){
                    dir[result].DIR_Attr = 0x02;
                }
                else if (result == -1){
                    printf("Error: File does not exist in the file system.\n");
                }
            }

        }
        else if(strcmp(token[0], "undel") == 0){
            //Restore from 0xe5/0x05 to either 0x00
            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{

                // Must find if file name in token[1] exits. 
                // Result is the index in where it appears.

                int result = equality(&token[1]);
                if (result == -1){
                    printf("Error: File does not exist in the file system.\n");
                }
                else{
                    dir[result].DIR_Attr = 0x01;
                }
            }
        }
        else if(strcmp(token[0], "get") == 0){

            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{
                int result = equality(&token[1]);
                if(result != -1){
                    int offset = LBAToOffset(dir[result].DIR_FirstClusterLow);

                    fseek(fp, offset, SEEK_SET);

                    FILE * output = fopen(token[1], "w");

                    uint8_t buffer[512]; //512 comes from "info" command, bytes per sector.

                    int size = dir[result].Dir_FileSize;

                    while(size >= BPB_BytesPerSec){
                        fread(buffer, 512, 1, fp);
                        fwrite(buffer, 512, 1, output);

                        size = size - BPB_BytesPerSec;

                        //new offset
                        int cluster = NextLB(cluster);

                        if(cluster > -1){
                            offset = LBAToOffset(cluster);
                            fseek(fp, offset, SEEK_SET);
                        }
                    }

                    if(size > 0){
                        fread (buffer, size, 1, fp);
                        fwrite(buffer, size, 1, output);
                    }

                    fclose(output);
                }
                else{
                    printf("Error: File not found\n");
                }
            }
        }
        else if(strcmp(token[0], "read") == 0){
            if(!fp){
                printf("Error: File system image must be opened first.\n");
            }
            else{
                int result = equality(&token[1]);
                char * read_file;
                if(result != -1){
                    uint8_t buffer[512]; //512 comes from "info" command, bytes per sector.

                    //User must have provided enough arguments
                    if(token[2] && token[3]){

                        //fseek to the file position
                        int offset = LBAToOffset(dir[result].DIR_FirstClusterLow);
                        fseek(fp, offset, SEEK_SET);

                        //Read the file
                        fseek(fp, atoi(token[2]), SEEK_CUR );
                        fread(buffer, atoi(token[3]), 1, fp);

                        //Print the bytes
                        for(int i = 0; i < atoi(token[3]); i++){
                            printf("%d ", buffer[i]);
                        }
                        printf("\n");
                    }
                }
                else{
                    printf("Error: File not found\n");
                }
            }
        }
    }
    return 0;
}

//Given a logical block address, will look into first FAT & return logical block address of block 
//in the file. If not futher blocks, will return -1.
int16_t NextLB( uint32_t sector ){
    int16_t  val;
    uint32_t FATAddress = (BPB_BytesPerSec * BPB_RsvdSecCnt ) + ( sector * 4);
    fseek( fp, FATAddress, SEEK_SET );
    fread( &val, 2, 1, fp);
    return val;
}

int LBAToOffset(int32_t sector){
    return (( sector -2) * BPB_BytesPerSec) + (BPB_BytesPerSec * BPB_RsvdSecCnt) 
            +(BPB_NumFATs * BPB_FATsz32 * BPB_BytesPerSec);
}

//Function used to compare user input with all the directories and folders available in file system.
int equality(char ** token){
    //If return -1, then file does NOT exist

    //If return any number between 0 and 16, 
    //that's the correct location/index of the folder/dir we needed!

    char * compare;

    char expanded_name[12];

    if(*token){
        for(int i = 0; i < 16; i++){
            //Function to map these in dir: foo.txt <--> FOO______.TXT

            compare = dir[i].DIR_Name;

            if(compare && token){
                memset( expanded_name, ' ', 12 );

                char *tok = strtok( *token, "." );

                if(strncmp(*token, "..", 2) != 0){
                    strncpy( expanded_name, tok, strlen( tok ) );

                    tok = strtok( NULL, "." );

                    if( tok ){
                        strncpy( (char*)(expanded_name+8), tok, strlen(tok ) );
                    }

                    expanded_name[11] = '\0';

                    for( int j = 0; j < 11; j++ ){
                        expanded_name[j] = toupper( expanded_name[j] );
                    }
                }
            }
            if( strncmp( expanded_name, compare, strlen(*token) ) == 0 ){
                return i;
            }
        }
    }
    return -1;
}
