#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stropts.h>
#include <asm/termios.h>

/* Simple function to set custom baud rate
 * For this to work on a Pi init_uart_clock=16000000 needs to be set in /boot/config.txt */
void setBaudRate(int fd) {
	struct termios2 tio;

	ioctl(fd, TCGETS2, &tio);
	tio.c_cflag &= ~CBAUD;
	tio.c_cflag |= BOTHER;
	tio.c_ispeed = 250000;
	tio.c_ospeed = 250000;
	ioctl(fd, TCSETS2, &tio);
}