#include <string.h>
#include <stdio.h>
#include <fcntl.h>

/* exit() is in stdlib.h for Linux, but inherent in XENIX 
 * Also, errno was necessary for Linux only.  */

#ifdef __STDC__
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <termios.h>
#else
#include <sgtty.h>
#endif

/*
 *  DLOAD Server  - Serves files on the local filesystem to a Tandy CoCo 1 or 2 via the serial port.
 *  Copyright (C) 2020 Christopher Hyzer
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * -----------------------------------------------------------------------
 * Author's notes: 
 *
 * Intended to compile on very old C implementations
 * (for TRS-XENIX support), therefore newer constructs
 * like 'bool' (and apparently 'const') are not use here.
 * This application is UNSUPPORTED.  Use at your own
 * risk, even if it steals your spouse or breaks
 * your toaster oven.
 *
 * Linux support is hacked in to allow use on systems newer than 35 years old. :-)
 *
 * See LICENSE.TXT for the full terms of the GNU GPL which covers this program.
 *
 */

/* XENIX's C Compiler does not like the const keyword */
/* Misc. Constants */
char VERSION[9] = "01.00.00";

/* Protocol constants */
unsigned char ERROR = 0xBC;
unsigned char PAD_BYTE = 0x20;
unsigned char HS = 0x8A;
unsigned char HS_OK = 0x97;
unsigned char NAME_OK = 0xC8;
unsigned char F_1_OK = 0x01;
unsigned char F_1_NE_ERROR = 0xFF;
unsigned char F_2_OK = 0x01;
unsigned char F_2_FM_ERROR = 0x00;
unsigned char F_3_LAST = 0x00; /* sent after last block. */
unsigned char F_3_BASIC_PRG = 0x80; /* set for each BASIC block sent. */
unsigned char F_3_DS_ERROR = 0xFF;

/* Define the structure of a block */
struct block {

        unsigned int checksum;
        int index;
        char data[129]; /*FLAG_3 is part of data block + 128-bytes for payload */
        int max_size;

};

#ifdef __STDC__
int main(int argc, char *argv[]);
void sendError(int s);
int openSerial(char p[]);
void readSerialByte (int sp, unsigned char *b, int wait);
void writeSerialByte (int pt, unsigned char ch);
void closeSerial(int s);
int sendBlock (int prt, struct block *bl);
int handshake(int spt, char name[]);
void sendFile(char name[], int p);
int blockAck(int p);
void initBlock(struct block *b);
int updateBlock(struct block *b, char c);
void padBlock(struct block *b);
void updateChecksum (struct block *b, char c);
void makePrg(char filename[]);
void header(FILE *f);
void footer(FILE *f);
#endif

/* MS XENIX C compiler does not use separate function prototypes. 
   Note older declaration of function parameters below */
#ifdef __STDC__
int main(int argc, char *argv[])
#else
main(argc, argv)
int argc;
char *argv[];
#endif
{
        /* Variable init. - Note all declarations must be made
        before the first statement.   */
        int result = 0;
        unsigned char b = 0x00;
        char filename[13];
        char portname[127];
	int serial;

	/* Print some GNU GPLv3-style intro text. */
	printf("\ndload_server Version: %s \nA DLOAD server for the TRS-80 Color Computer 1 and 2.  ", VERSION);
	printf("\nCopyright (C) 2020 Christopher Hyzer.\n\n");

	printf("This program comes with ABSOLUTELY NO WARRANTY; for details see LICENSE.TXT.\n");
	printf("This is free software, and you are welcome to redistribute it\n");
	printf("under certain conditions; see LICENSE.TXT for details.\n\n");

	/* Get the only accepted argument: the path to the serial port. */
	if (argc != 2 ) {
		printf("Incorrect number of parameters.\nUsage: dload <path_to_serial_port>\n");
		exit(1);
	} else {
		strcpy(portname, argv[1]);
	}

	/* open the serial port */
	serial = openSerial(portname);

	printf("Awaiting handshake...\n");
	
	/* while true, monitor serial port, and proceed when HS received. */
	while (1) {

		/* re-initialize values. */
		b = 0x00;
		result = 255;
		/* Read a byte. */
		readSerialByte(serial, &b, 1);

		/* printf("%c",b); */

		/* If we get a handshake byte, begin the commencing. */
		/* Otherwise we throw it out and ignore it. */
		if (b == HS) {

			printf("Got handshake byte from client.\n");

			/* result = mode; if mode 0, file; if mode 1, commend; if 255, error. */
			result = handshake(serial, filename);

			switch (result) {

				/* BASIC File XFer */
				case 0:
					printf("Sending file: %s\n", filename);
					sendFile(filename, serial);
					break;
				/* Command mode (future) */
				case 1:
					printf("Command mode: Building BASIC program.\n");
					makePrg(filename);

					if (filename == NULL) {
						printf("Returning error to client.\n");
						writeSerialByte(serial, ERROR);
					} else {
						sendFile(filename, serial);
					}

					break;

				case 255:
					printf("Failure during handshake.  Operation aborted.\n");
					break;
				default:
					printf("Undefined return code received.  Something's very wrong.  Sending ERROR\n");

					/* Call error routine*/
					sendError(serial);
					break;
			}

		}

		/* Back to the top to wait for the next handshake byte. */
	}

	/* Normally we won't get here - abnormal conditions will abort */
	/* with an alternate return code when necessary. */
}

/* TODO - Send an error - this function is currently a stub and likely
 * won't work as-is. */
#ifdef __STDC__
void sendError(int s)
#else
sendError(s)
int s;
#endif
{
	int i;

	/* For the ham-fisted approach, I'm just going to send a bunch
	   of ERROR bytes to the client (more than the block size). */
	for (i=0; i<130; i++) {
		writeSerialByte(s,ERROR);
	}
}

/* Open the serial port - returns a pointer to the port */
/* Abort execution on failure. */
#ifdef __STDC__
int openSerial(char p[])
#else
openSerial(p)
char p[];
#endif
{
#ifndef __STDC__
	struct sgttyb tparam;
#else
	struct termios tty;
#endif
	int fd;
	printf("Opening serial port.\n");

	/* Open the port (for R/W, must use XENIX system call to open file rather
 	than fopen */
#ifndef __STDC__
	fd = open(p, O_RDWR | O_NDELAY);
#else
	fd = open (p, O_RDWR | O_NOCTTY | O_SYNC);
#endif

	/* Abort if we don't have a file handle. */
	if (fd == -1) {
		printf("Failed to open the serial port %s.\n", p);
		exit(2);
	}

	/* Configure the serial port (done differently for XENIX vs Linux */
	printf("Configuring serial port.\n");

#ifdef __STDC__
/* This code borrowed from https://stackoverflow.com/questions/6947413/how-to-open-read-and-write-from-serial-port-in-c */
	if (tcgetattr (fd, &tty) != 0)
        {
               	printf("Got error code %d while getting serial port configuration.  Exiting.\n", errno);
	        exit(1);
        }

	cfsetospeed (&tty, B1200);
        cfsetispeed (&tty, B1200);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  
        tty.c_iflag &= ~IGNBRK;         /* disable break processing */
	tty.c_lflag = 0;                /* no signaling chars, no echo */
                                       	/* no canonical processing */
        tty.c_oflag = 0;                /* no remapping, no delays */
	tty.c_cc[VMIN]  = 0;            
        tty.c_cc[VTIME] = 0;            

	tty.c_iflag &= ~(IXON | IXOFF | IXANY); /* shut off xon/xoff ctrl */

        tty.c_cflag |= (CLOCAL | CREAD); /* ignore modem controls */
                                         	 /* enable reading */
	tty.c_cflag &= ~(PARENB | PARODD);  /* shut off parity */
	tty.c_cflag |= PARENB;
        tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;

 	if (tcsetattr (fd, TCSANOW, &tty) != 0)
        {
       	        printf("Got error code %d while configuring serial port.  Exiting.\n", errno);
               	exit(1);
	}

#else
	ioctl(fd, TIOCGETP, &tparam);  /* get current values */
	tparam.sg_ispeed = tparam.sg_ospeed = B1200;
	tparam.sg_flags = tparam.sg_flags | RAW | EVENP;
	ioctl(fd, TIOCSETP, &tparam);  /* apply changed parameters */

	/* printf("Flags: %x\n", tparam.sg_flags); */
#endif
	printf("Successfully opened and configured serial port.\n");

	return fd;

}

/* Read byte from Serial port - if wait=1, blocks until a byte is available. */
/* If wait=0, does not wait for a byte. Execution will abort if read fails. */
#ifdef __STDC__
void readSerialByte (int sp, unsigned char *b, int wait)
#else
readSerialByte (sp,b,wait) 
int sp;
unsigned char *b;
int wait;
#endif
{
	int a = 0;

/* Linux returns -1 on non-blocking serial I/O, XENIX returns 0. */
#ifdef __STDC__
	if (wait == 1) {
		/* Read a byte. On some platforms, no bytes available returns -1 with EAGAIN (11) */
		while (a == 0 || ((a == -1) && (errno == 11))) {
			a = read(sp, b, 1);
			/* printf("%d ", a); */
		}
	} else {
		a = read(sp, b, 1);
	}

	/* Error check (if we get EAGAIN as errno, this is not a problem, just no bytes available). */
	if (a == -1 && errno != 11) {
		printf("Failed to read from serial port.  Error code %d.  Exiting.\n", errno);	
		exit(1);
	}
#else
        if (wait == 1) {
                /* Read a byte.  */
                while (a == 0) {
                        a = read(sp, b, 1);
                        /* printf("%d ", a); */
                }
        } else {
                a = read(sp, b, 1);
        }

        /* Error check */
        if (a == -1) {
                printf("Failed to read from serial port.  Exiting.\n");
                exit(1);
        }	
#endif

}

/* Write a byte to the Serial Port - errors will be displayed via this function */
/* Execution will abort if write fails. */
#ifdef __STDC__
void writeSerialByte (int pt, unsigned char ch)
#else
writeSerialByte (pt, ch)
int pt;
unsigned char ch;
#endif
{
	int a;

	/* Send the byte */
	a = write(pt, &ch, 1);

	/* Error check */
	if (a == -1) {
		printf("Failed to write to the serial port.  Exiting.\n");
		exit(1);
	}

}

/* Close the serial port. */
#ifdef __STDC__
void closeSerial(int s)
#else
closeSerial(s) 
int s;
#endif
{

	close(s);	

}

/* Send the block to the serial port. */
#ifdef __STDC__
int sendBlock (int prt, struct block *bl)
#else
sendBlock (prt, bl) 
int prt;
struct block *bl;
#endif
{

	char c;
	int i;
	for (i=0; i < bl->index; i++) {
		/* printf("%c",bl->data[i]); */
		writeSerialByte(prt,bl->data[i]);

		/* Do a non-blocking read to see if the CoCo threw an error  */
		/* This must happen before the checksum goes out to avoid interfering with the
 		acknowledgement routine. */
		readSerialByte(prt, &c, 0);
		/* printf("%x ", c); */

		if (c == ERROR) {
			printf("Client returned error during block transission.\n");
			return 1;
		}

	}

	/* Write the checksum. */
	writeSerialByte(prt,bl->checksum);

	return 0;

}

/* Handshake function - when the program gets 0x8A, this function */
/* will be called to init the handshake.  Returns the following: */
/* 0 = BASIC program, 1 = SERVER_COMMAND mode, 255 = error */
#ifdef __STDC__
int handshake(int spt, char name[])
#else
handshake(spt, name) 
int spt;
char name[];
#endif
{

	FILE *f;

	unsigned char c;
	unsigned char remoteChecksum;
	int i;

	struct block fn;
	struct block *filename = &fn;

        struct block f12;
        struct block *flags12 = &f12;

        struct block pf;
        struct block *prefile = &pf;

	initBlock(filename);
	/* Fs 1 and 2 are handled here. */
	/* This function will update the file handle */
	/* referenced by the caller as a result. */

	/* See constants above main function. */
	printf("Beginning handshake...\n");
	writeSerialByte(spt, HS);

	/* printf("Handshake byte %x sent.\n", HS); */

	/* Next 8 bytes should be a filename. */
	for (i=0; i<8; i++) {
		readSerialByte(spt, &c, 1);
		/* printf("%x",c); */
		if (updateBlock(filename, c) != 0) {
			break;
		}
	}

	/* Ensure we have a null terminator to treat the data block in the struct as */
	/* a string downstream.  Will also remove spaces from filename */
	for (i=0; i<filename -> index; i++) {
		if (filename -> data[i] == ' ') {
			filename -> data[i] = 0x00;
			filename -> index = i;
			break;
		}
	}

	/* Client will send checksum byte for filename. */
	/* If it doesn't match what we have, bail. */
	readSerialByte(spt, &remoteChecksum, 1);
	if (remoteChecksum != filename -> checksum){
		printf("Got filename: %s: Client sent checksum %i, but the computed checksum is %i.  Aborting.\n", filename -> data, remoteChecksum, filename->checksum);
		writeSerialByte(spt, ERROR);
		return 255;
	}

	strcpy(name, filename -> data);	

	/* Tell client our checksum matches. */
	printf("Got filename: %s.  Checksum OK.\n", name);
	writeSerialByte(spt, NAME_OK);

	initBlock(flags12);

	/* If file does not exist, set F_1 to return ?NE ERROR */
	/* unless filename preceeded with a hyphen '-' */
	if (name[0] == '-') {
		printf("Command mode request.  Skipping file check.\n");
	} else {
		f = fopen(name, "r");
		if (f == NULL) {
			printf("Failed to open file.  Sending ?NE ERROR to client and aborting.\n");
			i = updateBlock(flags12, F_1_NE_ERROR);
			i = updateBlock(flags12, F_2_OK);
			i = sendBlock(spt, flags12);
			return 255;
		}

		/* Close it, we'll open it for real in sendFile */
		fclose (f);
	}

	/* The name checks out and the file is open - let the client know we're going to proceed. */

	if (updateBlock(flags12, F_1_OK) != 0) {
		return 255;
	}

	if (updateBlock(flags12, F_2_OK) != 0) {
		return 255;
	}

	if (sendBlock(spt, flags12) != 0) {
		return 255;
	}

	/* Host will acknowledge and expect return acknowledgement */
	if (blockAck(spt) != 0) {
		printf("Host block acknowledgement failed during handshake.\n");
		return 255;
	} 

	/* F_3 picks up from here, and is sent in the downstream routines (either file more or command mode functions. */
	if (name[0] == '-') {
		return 1;
	}

	return 0;
}

/* Sends a file with the name specified; CoCo filenames are 8 characters only. */
#ifdef __STDC__
void sendFile(char name[], int p)
#else
sendFile(name, p)
char name[];
int p;
#endif
{
	/* F 3 of the protocol will be managed by the sending function, not by */
	/* the handshake */

	FILE  *fopen(), *hf;
	int result = 0;
	unsigned char h = 0x00;
	int bytecount = 0;
	int blockcount = 0;
	int filesize = 0;
	int fileblocks = 0;
	struct block fileBlock;
	struct block *thisBlock = &fileBlock;

	/* Open the file for reading */
	/* printf("Opening file %s...\n",name); */
	hf = fopen(name, "r");
	bytecount = result = 0;

	/* Determine the file's size to figure out which block is the last */
	fseek(hf, 0L, 2);
	filesize = ftell(hf);
	rewind(hf);

	fileblocks = filesize/128;
	printf("File size: %d bytes, split into %d blocks.\n", filesize, fileblocks + 1);
	
	/* Ensure file is open... */
	if (hf == NULL) {
		printf("Failed to open file.  Sending ERROR to client.\n");
		sendError(p);
	} else {

		initBlock(thisBlock);

		/* FLAG_3 is included in checksum, so it's part of block payload */
		result = updateBlock(thisBlock, F_3_BASIC_PRG);

		printf("Sending payload:.");
		while (fread(&h,1,1,hf) == 1) {

			/* If we get a UNIX newline, change it to \r */
			if (h == '\n') {
				h = '\r';
			}

			/* printf("%x ",h); */
			printf(".");
			/* Update the block. */
			result = updateBlock(thisBlock, h);
			if (result != 0) {
				printf("Block update failed.  Sending ERROR");
				sendError(p);
				break;
			}

			/* Increment the byte count, and create a new block if we have hit the max size. */
			bytecount++;
			if (bytecount == thisBlock -> max_size) {

				blockcount++;
				if (sendBlock(p, thisBlock) != 0) {
					result = 1;
					break;
				}
				if (blockAck(p) != 0) {
					result = 1;
					break;
				}
				initBlock(thisBlock);

				/* Add the FLAG_3 value indicating a continuing BASIC program. */
				result = updateBlock(thisBlock, F_3_BASIC_PRG);
				bytecount = 0;
			}	

		}

		/* Close file */
		fclose(hf);

		/* Send the last block and close the conection. */
		if (result == 0) {

			/* CoCo expects data blocks to always be 128 bytes) */
			padBlock(thisBlock);
			if (sendBlock(p, thisBlock) != 0) {
				return;	
			}

			/* Block Acknowledgement Routine */
			if (blockAck(p) == 0) {

				/* Send the Closing Block */
				initBlock(thisBlock);
				if (updateBlock(thisBlock, F_3_LAST) != 0) {
					return;
				}

				padBlock(thisBlock); 
				if (sendBlock(p, thisBlock) != 0) {
					return;
				}
			}
			
		}

		printf("DONE\n");

	}
}

/* Block acknowledgement routine */
/* TODO - Add a timeout in case the CoCo unexpectedly 
   terminates the connection (as happens with a ?DS ERROR) */
#ifdef __STDC__
int blockAck(int p)
#else
blockAck(p)
int p;
#endif
{
	unsigned char c;
	int i;
	struct block stBlock;
	struct block *st = &stBlock;

	initBlock (st);

	/* Get the host response and acknowledge if HS_OK */
	readSerialByte(p, &c, 1);
	if (c != HS_OK) {
		printf("Host did not acknowledge block.  Got %x.\n", c);
		writeSerialByte(p, ERROR);
		return 1;
	} else {
		writeSerialByte(p, HS_OK);
		for (i=0; i<2; i++) {
			readSerialByte(p, &c, 1);
			if (updateBlock(st, c) != 0) {
				return 1;
			}
		}

		readSerialByte(p, &c, 1);
		if (c != st -> checksum) {
			writeSerialByte(p, ERROR);
			printf("Checksum failed during block acknowledgement.\n");
			return 1;
		} else {
			writeSerialByte(p, NAME_OK);
		}
		
	}

	return 0;

}

/* Initialize a block */
#ifdef __STDC__
void initBlock(struct block *b)
#else
initBlock(b) 
struct block *b;
#endif
{
	int i;
	b->max_size = 128;
	b->index = 0;
	b->checksum = 0;

	/* Fill the block bytes with NULL values. */
	for (i=0; i<129; i++){
		b->data[i] = 0x00;
	}
}

/* Update a given block with the char c provided. */
/* returns 0 on sucess, 1 on failure. */
#ifdef __STDC__
int updateBlock(struct block *b, char c)
#else
updateBlock(b, c)
struct block *b;
char c;
#endif
{

	if (b->index > b->max_size) {
		printf("Block size exceeded.  Max size is %i", b->max_size);
		return 1;
	} else {
		b->data[b->index] = c;
		b->index++;
		updateChecksum(b,c);
	}

	return 0;

}

/* Pad the remainder of the block with spaces */
#ifdef __STDC__
void padBlock(struct block *b)
#else
padBlock(b)
struct block *b;
#endif
{

	int i;

	for (i = b -> index; i <= b -> max_size; i++) {
		/* b->data[i] = PAD_BYTE;
		b->index++;  */
		if (updateBlock(b, PAD_BYTE) != 0) {
			break;
		}
	}

}

/* Update the checksum of the block given the current byte. */
#ifdef __STDC__
void updateChecksum (struct block *b, char c)
#else
updateChecksum (b, c)
struct block *b;
char c;
#endif
{

	b->checksum = (unsigned int)c^b->checksum;

}

/* Based on the command, build a BASIC program to send back to client. */
#ifdef __STDC__
void makePrg (char filename[])
#else
makePrg(filename)
char filename[];
#endif
{
	FILE *fp, *popen();
	FILE *rs, *fopen();
	char result[255];
	char buffer[200];
	int linenum = 35;
	char linestr[5];
	int rescount = 0;
	int linelen = 0;

	/* Open the file containing the results and write the header */
	rs = fopen("dload.tmp","w");

	if (rs == NULL) {
		printf("Failed to open dload.tmp for writing.\n");
#ifdef __STDC__
/* TODO - Determine why this is here */
#else
		strcpy(filename,NULL);
#endif
		return;
	}
	header(rs);

	if (strcmp(filename,"-DIR") == 0) {
		fp = popen("/bin/ls .","r");

		if (fp == NULL) {
			fwrite("30 PRINT\"FAILED TO GET DIRECTORY\"\n",1, 34, rs);
		} else {
			while (fgets(buffer, sizeof(buffer), fp) != NULL) {

				/* fwrite("30 PRINT\"DIRECTORY LISTING OF .\"\n", 1, 33, rs); */
				/* Clear the result */
				result[0] = '\0';

				/* Remove newline from buffer */
				linelen = strlen(buffer) - 1;
				buffer[linelen] = '\0';
	
				/* Generate the line - this is hokey */
				sprintf(linestr, "%d", linenum);
				strcat(result, linestr);
				strcat(result, " PRINT\"");
				strcat(result, buffer);
				strcat(result, "\"\n");
				fwrite(result, 1,strlen(result), rs);

				/* Increment the line number */
				linenum+=5;
				rescount++;

				/* Put a pause in the program when the screen fills */
				if (rescount == 13) {
					result[0] = '\0';
					sprintf(linestr, "%d", linenum);
					strcat(result, linestr);
					strcat(result, " A$=INKEY$:IF A$=\"\" THEN GOTO ");
					strcat(result, linestr);
					strcat(result, "\n");
					fwrite(result, 1, strlen(result), rs);
					rescount = 0;
					linenum+=5;
				}
			}
			fclose(fp);
		}

	} else {
		fwrite("30 PRINT\"** UNSUPPORTED COMMAND **\"\n", 1, 36, rs);
	}

	footer(rs);
	fclose(rs);
	strcpy(filename,"dload.tmp");


}

/* Write the BASIC program header */
#ifdef __STDC__
void header(FILE *f)
#else
header(f)
FILE *f; 
#endif
{

	char line10[41];
	char line20[45];

	strcpy(line10,"10 CLS:PRINT\"DLOAD SERVER COMMAND MODE\"\n");
	strcpy(line20,"20 PRINT\"--------------------------------\";\n");

	fwrite(line10,1,sizeof(line10),f);
	fwrite(line20,1,sizeof(line20),f);

}

/* Write the BASIC program footer */
#ifdef __STDC__
void footer(FILE *f)
#else
footer(f)
FILE *f;
#endif
{
	char line1000[10];
	strcpy(line1000,"1000 END\n");

	fwrite(line1000,1,sizeof(line1000),f);
}
