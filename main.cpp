#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>        
#include <stdlib.h> 
#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <wiringPi.h>

#define XBUS_CHANNELS 14

int fd=0;

/* This has to live in a different file because it uses asm/temrios.h which breaks this file! */
extern void setBaudRate(int fd);

/* CRC Calculation for XBus messages */
static uint8_t s_crc_array [256] = {
	0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83, 0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41,
	0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e, 0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc,
	0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0, 0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
	0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d, 0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff,
	0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5, 0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07,
	0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58, 0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
	0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6, 0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24,
	0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b, 0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9,
	0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f, 0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
	0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92, 0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50,
	0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c, 0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee,
	0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1, 0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
	0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49, 0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b,
	0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4, 0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16,
	0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a, 0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
	0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7, 0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35, };

static uint8_t crc_table (uint8_t data, uint8_t crc) {
	uint16_t index = (data ^ crc) & 0xff;
	crc = s_crc_array [index];
	return crc;
}

uint8_t crc8 (uint8_t * buffer, uint8_t length) {
	uint8_t crc = 0;
	while (length--> 0)
		crc = crc_table (* buffer ++, crc);
	return crc;
}


/* Previous state of RC channels */
int pwmLast;
bool sw1Last = false;
void processChannels(double channels[XBUS_CHANNELS]) {
	/* Aux2 switch is 3 position, only interested in top one (off) or bottom two (on),
	 * pos1 = 1900, pos2 = 1500, so use a threshold of 1700. */
	bool sw1 = channels[6] < 1700;
	
	/* If switch position has changed, toggle camera recording */
	if (sw1 != sw1Last) {
		if (sw1) {
			#ifdef DEBUG
			printf("Video control: Enabled (%.0f)\n", channels[6]);
			#endif
			/* Simplest way to record video is to launch raspivid
			 * Do this with sudo to ensure file is user-deletable
			 * (Because this executable has to run as root due to wiringPi) */
			system("sudo -u pi raspivid -w 1920 -h 1080 -fps 30 -vf -hf -n -t 3600000 -o /home/pi/videos/$(date +\"%Y%m%d_%H%M%S\").h264 &");
		} else {
			#ifdef DEBUG
			printf("Video control: Disabled (%.0f)\n", channels[6]);
			#endif
			/* Stop recording by killing the raspivid process */
			system("killall raspivid");
			/* Make sure video is saved to card (should be safe to depower) */
			system("sync");
		}
		sw1Last = sw1;
	}
	
	/* Calculate required wiringPi PWM output to drive gimbal from 0 to -90 degrees based on
	 * position of Aux3 slider. Arbitrary constants come from converting from microseconds
	 * (1100-1900) to a fairly small range of wiringPi PWM outputs corresponding to 0 and
	 * -90 degrees. These were determined experimentally to be 76 (0 degrees) and 57 (-90 degrees) */
	int pwmOut = 57 + 19 * (ceil(channels[7])-1100)/800;
	
	/* If the PWM value has changed, update the output (no point doing this otherwise) */
	if (pwmOut != pwmLast) {
		#ifdef DEBUG
		printf("Gimbal pitch: PWM In %.0f -> PWM Out %d\n",ceil(channels[7]), pwmOut);
		#endif
		pwmWrite(1,pwmOut);
		pwmLast = pwmOut;
	}
}

/* Read XBus data from serial port */
void  readSerialPort(void) {
	/* Serial port and XBus message buffers */
	unsigned char buff;
	unsigned int byte = 0;
	unsigned char message[XBUS_CHANNELS*4 + 7];
	
	/* Read XBus input indefinitely */
	while (1) {
		/* Get a character from the serial port */
		int n = read(fd, &buff, 1);
		
		/* If we have new data, process it */
		if (n == 1) {
			/* Check for start character */
			if (byte == 0 && buff == 0xA4) {
				message[byte++] = buff;
				continue;
			}
			/* Assimilate the rest of the message */
			if (byte > 0) {
				message[byte++] = buff;
				
				/* Full message seems to be two bytes (+ checksum) longer than length stated in the 2nd byte */
				if (byte==message[1]+3) {
					// Couldn't figure out which bytes are actually being checksummed, so gave up trying.
					// TODO: Figure this out before doing any interface to flight controls (i.e safety critical stuff!)
					/* uint8_t crcCalc = crc8(message,message[1]);
					if (message[byte-1] == crcCalc) {
						// Process message
					} else {
						#ifdef DEBUG
						printf("XBus CRC Fail!\n");
						#endif
					}
					*/
					// Assume that when we have a full packets worth of bytes (from start char), we can decode channels
					
					/* Assume a maximum of 14 channels (XG14 Tx) */
					double channels[XBUS_CHANNELS];
					for (int i=0; i<XBUS_CHANNELS; i++) {
						/* Conversion from JR Documentation */
						channels[i] = 800.0 + (1400.0*(message[4 + i*4 + 2]*256+message[4 + i*4 + 3]))/(double(0xFFFF));
					}
					
					/* Process the channel data */
					processChannels(channels);
					
					/* Message has been deatl with, reset for next one */
					byte = 0;
				}
			}
		}
	}
}

/* Open serial port */
void openSerialPort(void) {
	fd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY |O_NDELAY );
	if (fd <0) {
		perror("Error opening serial port!");
	}
	struct termios newtp;
	fcntl(fd,F_SETFL,0);
	bzero(&newtp, sizeof(newtp));
	newtp.c_cflag = B57600 | CRTSCTS | CS8 | CLOCAL | CREAD;
	newtp.c_iflag = IGNPAR | ICRNL;

	tcflush(fd, TCIFLUSH);
	tcsetattr(fd,TCSANOW,&newtp);
	
	/* Set custom baud rate */
	setBaudRate(fd);
}

/* Set up PWM output */
void setupPWM() {
	if (wiringPiSetup () == -1)
		exit (1);
	
	pinMode(1, PWM_OUTPUT);
	pwmSetMode(PWM_MODE_MS);// Use Mark-Space mode for constant frequency output
	pwmSetClock(384);   	// This gives a clock of 50kHz
	pwmSetRange(1000);  	// This divides the clock to 50Hz (RC compatible)
}

int main() {
	openSerialPort();
	setupPWM();
	readSerialPort();
	return 0;
}