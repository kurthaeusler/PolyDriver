#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <syslog.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "lzn.h"

#define DEFAULT_MODEM_DEVICE "/dev/ttyS0"
#define DEFAULT_PTY_NAME "/dev/ptyp1"
#define BAUDRATE B115200

int allocate_pty();
int becomeDaemon(int, int);
int closeall(int);
void start_loop();
void handle_pty();
void open_modem();
void handle_modem();
void user_to_modem(unsigned char *, unsigned int);
void modem_to_user(unsigned char *, unsigned int);
void parse_command_line(int, char **);
void usage(int, char **);
void print(char *);
void display(char *, unsigned char *, int);
int hangup(int, char *);
void msr_display(int);
void display_errors();

char pty_name[256];
int keep_running = 1;
fd_set fdescriptors;
int pty_fd = -1, modem_fd = -1, max_fd = -1;
int modem_open = 0;
int reset = 0;
struct termios oldtio, newtio;
int balancer = 0;
int ppp_mode = 0;
char modem_device[256];

unsigned char basis_eth[6];
unsigned char modem_eth[6];
int foreground = 0;
int debug = 0;

unsigned int errors[6] = { 0, 0, 0, 0, 0, 0 };

int main(int argc, char *argv[])
{
	int ret_val = 0;

	openlog(argv[0], 0, LOG_DAEMON);

	parse_command_line(argc, argv);

	if (!foreground) {
		ret_val = becomeDaemon(0, 0);
		if (ret_val)
			return ret_val;
	}
	reset = 1;
	while (reset) {
		reset = 0;
		pty_fd = allocate_pty();
		if (pty_fd >= 0) {
			if (pty_fd > max_fd) {
				max_fd = pty_fd;
			}
			keep_running = 1;
			start_loop();
		}
		close(pty_fd);
		sleep(1);
	}
	return ret_val;
}

int allocate_pty()
{
	struct stat stb;
	int fd;
	char error_message[128];

	print("ALLOCATE_PTY\n");

	if (stat(pty_name, &stb) < 0) {
		sprintf(error_message, "could not stat %s\n", pty_name);
		syslog(LOG_ERR, error_message);
		return -1;
	}

	fd = open(pty_name, O_RDWR | O_NONBLOCK, 0);
	if (fd >= 0) {
		pty_name[5] = 't';	/* change /dev/pty?? to /dev/tty?? */
		if (access(pty_name, W_OK | R_OK) != 0) {
			sprintf(error_message,
				"access on %s seemed to fail, exiting\n",
				pty_name);
			syslog(LOG_ERR, error_message);
			close(fd);
			pty_name[5] = 'p';
			return -1;
		}
		sprintf(error_message, "listening on device %s\n",
			pty_name);
		syslog(LOG_NOTICE, error_message);
		pty_name[5] = 'p';
		return fd;
	}
	pty_name[5] = 'p';
	return -1;
}

int becomeDaemon(int nochdir, int noclose)
{
	print("BECOMEDAEMON\n");

	switch (fork()) {
	case 0:
		break;
	case -1:
		return -1;
	default:
		_exit(0);
	}

	if (setsid() < 0)
		return -1;

	switch (fork()) {
	case 0:
		break;
	case -1:
		return -1;
	default:
		_exit(0);
	}

	if (!nochdir)
		chdir("/");

	if (!noclose) {
		closeall(0);
		open("/dev/null", O_RDWR);
		dup(0);
		dup(0);
	}

	return 0;
}

int closeall(int fd)
{
	int fdlimit = sysconf(_SC_OPEN_MAX);

	print("CLOSEALL\n");

	while (fd < fdlimit)
		close(fd++);
	return 0;
}

void start_loop()
{
	print("START_LOOP\n");

	while (keep_running) {
		FD_ZERO(&fdescriptors);
		FD_SET(pty_fd, &fdescriptors);
		if (modem_open) {
			FD_SET(modem_fd, &fdescriptors);
		}
		if (select
		    (max_fd + 1, &fdescriptors, NULL, NULL, NULL) < 0) {
			print("error in select\n");
		}

		if (!balancer) {
			balancer = 1;
			if (modem_open
			    && FD_ISSET(modem_fd, &fdescriptors)) {
				handle_modem();
			}

			if (FD_ISSET(pty_fd, &fdescriptors)) {
				handle_pty();
			}
		} else {
			balancer = 0;
			if (FD_ISSET(pty_fd, &fdescriptors)) {
				handle_pty();
			}
			if (modem_open
			    && FD_ISSET(modem_fd, &fdescriptors)) {
				handle_modem();
			}
		}
		display_errors();
	}
}

void handle_pty()
{
	int res = 0, msr = 0;
	unsigned char inbuf[4096];
	unsigned char hang_up_command[10];
	unsigned char error_message[256];

	print("HANDLE_PTY()\n");
	res = read(pty_fd, inbuf, 1496);
	print("Completed read, we have from pty:\n");
	display("handle_pty just read:", inbuf, res);
	if (res == -1) {
		if (errno == EIO) {
			print("EIO, resetting\n");
			keep_running = 0;
			reset = 1;
			ppp_mode = 0;
			if (modem_open) {
				syslog(LOG_NOTICE, "closing modem\n");
				sprintf(hang_up_command, "+++ATH");
				hang_up_command[6] = 0x0d;
				hang_up_command[7] = 0x00;
				user_to_modem(hang_up_command, 7);
				hangup(modem_fd, inbuf);
				if ((ioctl(modem_fd, TIOCMGET, &msr)) ==
				    -1) {
					sprintf(error_message,
						"ioctl(TIOCMGET) failed, (%s)\n",
						strerror(errno));
					print(error_message);
				}
				msr_display(msr);


				close(modem_fd);
				modem_open = 0;
				modem_fd = -1;
				max_fd = pty_fd;
			}
			sleep(3);
		}
		return;
	}
	if (!modem_open)
		open_modem();

	if (!modem_open) {
		syslog(LOG_ERR, "couldn't seem to open modem\n");
		keep_running = 0;
		reset = 1;
		return;
	}


	inbuf[res] = 0;

	// Finally pass what we read to the modem...

	user_to_modem(inbuf, res);
}

void open_modem()
{
	unsigned char outbuf[256];
	unsigned char inbuf[4096];
	int res = 0, res2 = 0;
	fd_set mwait;
	int limit = 0, loop = 0;
	char error_message[128];
	int msr = 0;
	struct timeval timeout;

	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	print("OPEN_MODEM()\n");

	modem_fd = open(modem_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (modem_fd < 0) {
		sprintf(error_message, "error opening modem %s\n",
			modem_device);
		syslog(LOG_ERR, error_message);
		return;
	}
	if (modem_fd > max_fd) {
		max_fd = modem_fd;
	}

	if ((ioctl(modem_fd, TIOCMGET, &msr)) == -1) {
		sprintf(error_message,
			"ioctl(TIOCMGET) failed, (%s)\n", strerror(errno));
		print(error_message);
	}

	msr_display(msr);


	fcntl(modem_fd, F_SETFL, FASYNC);

	tcgetattr(modem_fd, &oldtio);
	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
	newtio.c_iflag = IGNPAR;	// Took out BRKINT
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	newtio.c_cc[VMIN] = 4;	// Was 0
	newtio.c_cc[VTIME] = 1;
	tcflush(modem_fd, TCIOFLUSH);
	tcsetattr(modem_fd, TCSANOW, &newtio);

	msr = TIOCM_RTS;

	ioctl(modem_fd, TIOCMBIS, &msr);




	outbuf[0] = 0x55;
	outbuf[1] = 0x10;
	outbuf[2] = 0x00;

	outbuf[3] = 0xff;
	outbuf[4] = 0xff;
	outbuf[5] = 0xff;
	outbuf[6] = 0xff;
	outbuf[7] = 0xff;
	outbuf[8] = 0xff;

	outbuf[9] = modem_eth[1];
	outbuf[10] = modem_eth[2];
	outbuf[11] = modem_eth[3];
	outbuf[12] = modem_eth[4];
	outbuf[13] = modem_eth[5];

	outbuf[14] = 0x00;

	outbuf[15] = 0x88;
	outbuf[16] = 0x89;

	outbuf[17] = 0x81;

	outbuf[18] = 0x00;

	FD_ZERO(&mwait);

	for (loop = 0; loop < 3 && !FD_ISSET(modem_fd, &mwait); loop++) {

		msr = 0;

		ioctl(modem_fd, TIOCMGET, &msr);

		msr_display(msr);
		/*
		   while (!(msr & TIOCM_CTS)) {

		   if ((ioctl(modem_fd, TIOCMGET, &msr)) == -1) {
		   sprintf(error_message,
		   "ioctl(TIOCMGET) failed, (%s)\n",
		   strerror(errno));
		   print(error_message);
		   }
		   if (debug) {
		   msr_display(msr);
		   }
		   }

		 */

		tcsendbreak(modem_fd, 0);

		print("About to write\n");

		if (debug)
			display("open_write", outbuf, 19);

		res = write(modem_fd, outbuf, 19);

		print("Returned from write\n");

		timeout.tv_sec = 3;
		timeout.tv_usec = 0;


		FD_ZERO(&mwait);

		FD_SET(modem_fd, &mwait);
		print("In open, about to select\n");
		if ((res = select(modem_fd + 1, &mwait, NULL, NULL, NULL)) < 0) {	// Should have &timeout in here
			print("error in select\n");
		}

		sprintf(error_message, "Select returned.%d\n", res);
		print(error_message);
		if (FD_ISSET(modem_fd, &mwait)) {
			print
			    ("Data available to read, i hope this dont loop again\n");
		}

	}

	print("After for loop\n");


	if (!FD_ISSET(modem_fd, &mwait)) {
		print("PolyBasis not answering\n");
		return;

	}

	print("About to read after command 81\n");

	res = read(modem_fd, inbuf, 4);

	print("Returned from 2nd read\n");

	if (debug)
		display("open_read2", inbuf, res);

	limit = 0;
	while ((res < inbuf[2] + 4) && limit < 10000) {
		limit++;
		print
		    ("About to 3rd read ( this is in case we didnt read enough first time.\n");
		res2 = read(modem_fd, inbuf + res, inbuf[2] + 4 - res);
		print("Returned from 3rd read\n");
		if (debug)
			display("subsent read:", inbuf + res, res2);
		if (res2 >= 0) {
			res += res2;
		}
	}

	display
	    ("total read after any subsequents, should have an 82 in there somewhere:",
	     inbuf, res);

	// Should check here for correct answer from modem

	if (inbuf[18] != 0x82) {
		print("PolyBasis not ready\n");
		return;
	}

	print("PolyBasis is ready!\n");

	outbuf[0] = 0x55;
	outbuf[1] = 0x0f;

	outbuf[2] = basis_eth[0];
	outbuf[3] = basis_eth[1];
	outbuf[4] = basis_eth[2];
	outbuf[5] = basis_eth[3];
	outbuf[6] = basis_eth[4];
	outbuf[7] = basis_eth[5];

	outbuf[8] = modem_eth[0];
	outbuf[9] = modem_eth[1];
	outbuf[10] = modem_eth[2];
	outbuf[11] = modem_eth[3];
	outbuf[12] = modem_eth[4];
	outbuf[13] = modem_eth[5];

	outbuf[14] = 0x00;

	outbuf[15] = 0x88;
	outbuf[16] = 0x89;

	outbuf[17] = 0x84;

	FD_ZERO(&mwait);

	for (loop = 0; loop < 3 && !FD_ISSET(modem_fd, &mwait); loop++) {

		msr = TIOCM_RTS;

		ioctl(modem_fd, TIOCMBIS, &msr);

		msr = 0;

		while (!(msr & TIOCM_CTS)) {

			if ((ioctl(modem_fd, TIOCMGET, &msr)) == -1) {
				sprintf(error_message,
					"ioctl(TIOCMGET) failed, (%s)\n",
					strerror(errno));
				print(error_message);
			}
			if (debug) {
				msr_display(msr);
			}
		}

		tcsendbreak(modem_fd, 2);

		print("About to write command 84\n");

		display("Command 84:", outbuf, 18);

		res = write(modem_fd, outbuf, 18);

		print("Returned from write\n");


		timeout.tv_sec = 10;
		timeout.tv_usec = 0;


		FD_SET(modem_fd, &mwait);
		if (select(modem_fd + 1, &mwait, NULL, NULL, &timeout)
		    < 0) {
			print("Error in select\n");
		}

	}

	if (!FD_ISSET(modem_fd, &mwait)) {
		print("No answer from modem to command 84\n");
		return;
	}

	print("About to read after command 84\n");

	res = read(modem_fd, inbuf, 4);

	display("command 84 read part 1", inbuf, res);

	while (res < inbuf[2] + 4) {
		print("subsequent read");
		printf(".");
		res2 = read(modem_fd, inbuf + res, inbuf[2] + 4 - res);
		display("subsequent part:", inbuf + res, res2);
		if (res2 >= 0) {
			res += res2;
		}
	}

	display("final check in open_modem", inbuf, res);

	// Should check here for correct answer from modem

	if (inbuf[18] != 0x87 || inbuf[19] != 0x84) {
		print
		    ("Wrong answer from modem. we wanted 0x8784 at inbuf[18 and 19]\n");
		return;
	}

	syslog(LOG_NOTICE, "Modem initialization complete\n");
	modem_open = 1;
}

void handle_modem()
{
	unsigned char inbuf[4096];
	int res, res2;
	unsigned int real_total;
	int limit = 0;

	print("HANDLE_MODEM()\n");

	print("About to read polytrax header\n");

	res = read(modem_fd, inbuf, 4);

	print("header read completed:\n");
	display("handle_modem, header read", inbuf, res);

	if (inbuf[0] != 0x00 || inbuf[1] != 0x55) {

		// This really is an error condition, when the windows version expects a 4 byte header, it really gets one every time 

		errors[0]++;
		printf
		    ("Either inbuf[0] was not 0x00, (it is 0x%.2x), or inbuf[1] was not 0x55, (it was 0x%.2x)\n",
		     inbuf[0], inbuf[1]);
		printf
		    ("About to write directly to pty without stripping and return i hope theres no headers in there\n");
		res2 = write(pty_fd, inbuf, res);
		return;
	}

	real_total = inbuf[2];
	printf("real_total is now %d\n", real_total);

	if ((inbuf[3] > 0x05) || ((inbuf[3] == 0x05) && (inbuf[2] > 0xe8))) {
		printf("ERROR: header contains size too large!\n");
		errors[5]++;
	}

	if (inbuf[3] != 0) {
		real_total = (256 * inbuf[3]) + inbuf[2];
		printf
		    ("inbuf[3] was not 0 it was %d, so real_total is now %d\n",
		     inbuf[3], real_total);
	}

	limit = 0;

	// I think here we need one read, that waits till the whole packet is in.

	while ((res < real_total + 4) && limit < 10000) {
		limit++;
		printf
		    ("Res was less that real_total+4 (which should always happen) (%d (should be 4) is less than %d) so reading more...\n",
		     res, real_total + 4);
		res2 = read(modem_fd, inbuf + res, (real_total + 4) - res);
		printf("Read %d more bytes...\n", res2);
		display("subsequent read", inbuf + res, res2);
		res += res2;
	}

	if (inbuf[15] != 0x00) {
		errors[1]++;
		printf
		    ("Inbuf[15] is not 0x00 (it is 0x%.2x) so returning\n",
		     inbuf[15]);

		return;
	}

	if ((inbuf[16] != 0x88) || (inbuf[17] != 0x89)) {
		errors[2]++;
		printf
		    ("inbuf[16] and [17] incorrect, it should be 0x8889 but it is 0x%.2x%.2x returning\n",
		     inbuf[16], inbuf[17]);
		return;
	}

	if (inbuf[18] == 0x4b) {	// Should be 0xbf
		errors[3]++;
		printf("inbuf[18] is 0x4b nack, so returning\n");
		return;
	}

	if (inbuf[18] != 0xbf) {
		errors[4]++;
		printf("inbuf[18] is not 0xbf, so returning\n");
		return;
	}
	//      inbuf[res] = 0;

	printf("About to pass the following to modem_to_user:\n");
	display("about_to_pass", inbuf, res);

	modem_to_user(inbuf, res);
}

void user_to_modem(unsigned char *buffer, unsigned int bytes)
{
	unsigned char outbuf[4096];
	int loop = 0, res;

	unsigned char msb = 0, lsb = 0;

	print("USER_TO_MODEM()\n");

	outbuf[0] = 0x55;
	outbuf[1] = 16 + bytes;

	outbuf[2] = 0x00;

	if (16 + bytes > 255) {
		msb = (bytes + 16) / 256;
		lsb = (bytes + 16) % 256;
		outbuf[1] = lsb;
		outbuf[2] = msb;
	}

	outbuf[3] = basis_eth[1];
	outbuf[4] = basis_eth[2];
	outbuf[5] = basis_eth[3];
	outbuf[6] = basis_eth[4];
	outbuf[7] = basis_eth[5];

	outbuf[8] = modem_eth[0];
	outbuf[9] = modem_eth[1];
	outbuf[10] = modem_eth[2];
	outbuf[11] = modem_eth[3];
	outbuf[12] = modem_eth[4];
	outbuf[13] = modem_eth[5];

	outbuf[14] = 0x00;

	outbuf[15] = 0x88;
	outbuf[16] = 0x89;

	outbuf[17] = 0xbf;
	outbuf[18] = 0xff;

	for (loop = 0; loop < bytes; loop++) {
		outbuf[19 + loop] = buffer[loop];
	}


	// Shouldnt we wait on CTS here like in open_modem?

	tcsendbreak(modem_fd, 0);

	if (debug)
		display("send", outbuf, 19 + bytes);

	res = write(modem_fd, outbuf, 19 + bytes);
}

void modem_to_user(unsigned char *buffer, unsigned int bytes)
{
	unsigned char outbuf[4096];
	int loop = 0, res = 0, totallength = 0, actualdata = 0, loop2 = 0;

	print("MODEM_TO_USER()\n");

	display("modem_to_user got", buffer, bytes);

	totallength = bytes - 4;
	actualdata = totallength - 16;

	printf("totallength is now %d and actualdata is now %d\n",
	       totallength, actualdata);

	for (loop = 20; loop < actualdata + 20; loop++) {
		outbuf[loop2++] = buffer[loop];
	}

	print("outbuf now:");
	display("outbuf now", outbuf, loop2);

	if (strstr(outbuf, "CONNECT")) {
		printf("Connect hack...\n");
		outbuf[loop2++] = 0x0d;
		outbuf[loop2++] = 0x0a;
		outbuf[loop2] = 0;
		ppp_mode = 1;
	}

	if (debug)
		display("recv", outbuf, loop2);

	res = write(pty_fd, outbuf, loop2);
	print("write returned, and now modem_to_user will return.\n");
}

int basisdecode(char *basis)
{
	int devno;
	char error_message[128];

	devno = EncodeLZN(basis);
	if (devno == 0)
		return -1;
	basis_eth[0] = 0x00;
	basis_eth[1] = 0x00;
	basis_eth[2] = 0x04;
	basis_eth[3] = 0x5e;
	basis_eth[4] = devno;
	basis_eth[5] = devno >> 8;
	sprintf(error_message,
		"Basis address: 0x%.2x%.2x%.2x%.2x%.2x%.2x\n",
		basis_eth[0], basis_eth[1], basis_eth[2], basis_eth[3],
		basis_eth[4], basis_eth[5]);
	print(error_message);

	return 0;
}

int modemdecode(char *modem)
{
	int devno;
	char error_message[128];

	devno = EncodeLZN(modem);
	if (devno == 0)
		return -1;
	modem_eth[0] = 0x00;
	modem_eth[1] = 0x00;
	modem_eth[2] = 0x04;
	modem_eth[3] = 0x5e;
	modem_eth[4] = devno;
	modem_eth[5] = devno >> 8;
	sprintf(error_message,
		"Modem address: 0x%.2x%.2x%.2x%.2x%.2x%.2x\n",
		modem_eth[0], modem_eth[1], modem_eth[2], modem_eth[3],
		modem_eth[4], modem_eth[5]);
	print(error_message);
	return 0;
}

void parse_command_line(int argc, char *argv[])
{
	int i;
	char opt;
	char basis[13];
	char modem[13];
	int basis_set = 0;
	int modem_set = 0;
	int serial_port_set = 0;
	int pty_name_set = 0;
	char wordflag[256];

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			opt = argv[i][1];
			switch (opt) {
			case 'b':
				if (i + 1 >= argc) {
					printf
					    ("-b requires a parameter\n");
					usage(argc, argv);
					exit(-1);
				}
				strncpy(basis, argv[i + 1], 13);
				if (basisdecode(basis)) {
					printf
					    ("Basis %s is invalid\n",
					     basis);
					exit(-1);
				}
				basis_set = 1;
				break;
			case 'd':
				debug = 1;
				break;
			case 'f':
				foreground = 1;
				break;
			case 'h':
				usage(argc, argv);
				exit(0);
				break;
			case 'm':
				if (i + 1 >= argc) {
					printf
					    ("-m requires a parameter\n");
					usage(argc, argv);
					exit(-1);
				}
				strncpy(modem, argv[i + 1], 13);
				if (modemdecode(modem)) {
					printf
					    ("Modem %s is invalid\n",
					     modem);
					exit(-1);
				}
				modem_set = 1;
				break;
			case 'p':
				if (i + 1 >= argc) {
					printf
					    ("-p requires a parameter\n");
					usage(argc, argv);
					exit(-1);
				}
				strncpy(pty_name, argv[i + 1], 256);
				pty_name_set = 1;
				break;
			case 's':
				if (i + 1 >= argc) {
					printf
					    ("-s requires a parameter\n");
					usage(argc, argv);
					exit(-1);
				}
				strncpy(modem_device, argv[i + 1], 256);
				serial_port_set = 1;
				break;
			case '-':
				if (strlen(argv[i]) < 3) {
					printf("Illegal flag: %s\n",
					       argv[i]);
					usage(argc, argv);
					exit(-1);
				}
				strncpy(wordflag, argv[i] + 2, 128);
				if (!strcmp(wordflag, "basis")) {
					if (i + 1 >= argc) {
						printf
						    ("--basis requires a parameter\n");
						usage(argc, argv);
						exit(-1);
					}
					strncpy(basis, argv[i + 1], 13);	// Switch to strncpy for option parsing!
					if (basisdecode(basis)) {
						printf
						    ("Basis %s is invalid\n",
						     basis);
						exit(-1);
					}
					basis_set = 1;
				} else if (!strcmp(wordflag, "modem")) {
					if (i + 1 >= argc) {
						printf
						    ("--modem requires a parameter\n");
						usage(argc, argv);
						exit(-1);
					}
					strncpy(modem, argv[i + 1], 13);	// Switch to strncpy for option parsing!
					if (modemdecode(modem)) {
						printf
						    ("Modem %s is invalid\n",
						     modem);
						exit(-1);
					}
					modem_set = 1;
				} else if (!strcmp(wordflag, "debug")) {
					debug = 1;
				} else if (!strcmp(wordflag, "foreground")) {
					foreground = 1;
				} else if (!strcmp(wordflag, "help")) {
					usage(argc, argv);
					exit(0);
				} else if (!strcmp(wordflag, "pty")) {
					if (i + 1 >= argc) {
						printf
						    ("--pty requires a parameter\n");
						usage(argc, argv);
						exit(-1);
					}
					strncpy(pty_name, argv[i + 1],
						256);
					pty_name_set = 1;
				} else if (!strcmp(wordflag, "serial")) {
					if (i + 1 >= argc) {
						printf
						    ("--serial requires a parameter\n");
						usage(argc, argv);
						exit(-1);
					}
					strncpy(modem_device, argv[i + 1],
						13);
					serial_port_set = 1;
				} else {
					printf
					    ("Flag --%s not recognized\n",
					     wordflag);
					usage(argc, argv);
					exit(-1);
				}
				break;
			default:
				printf("No such option %c\n", opt);
				usage(argc, argv);
				exit(-1);
			}
		}
	}
	if (!serial_port_set)
		strcpy(modem_device, DEFAULT_MODEM_DEVICE);
	if (!pty_name_set)
		strcpy(pty_name, DEFAULT_PTY_NAME);
	if (!(modem_set && basis_set)) {
		printf
		    ("The modem and basis license numbers must be provided with the -m and -b flags respectively\n");
		usage(argc, argv);
		exit(-1);
	}
}

void usage(int argc, char *argv[])
{

	printf
	    ("usage: %s -m modem -b basis [-s serial_port] [-h] [-p pty_name] [-f] [-d]\n",
	     argv[0]);
	printf("\nMandatory flags:\n");
	printf
	    ("-b, --basis\t\tSpecify basis license number. Example: --basis FGBCA953587A.\n");
	printf
	    ("-m, --modem\t\tSpecify modem license number. Example: --modem FGBCA96B36ZC.\n");
	printf("\nOptional flags:\n");
	printf("-d, --debug\t\tDebug mode.\n");
	printf
	    ("-f, --foreground\tDon't become a daemon, stay in foreground attached to the terminal.\n");
	printf("-h, --help\t\tPrint this usage screen.\n");
	printf
	    ("-p, --pty\t\tSpecify the pty file name to try. Default: /dev/ptyp1.\n");
	printf
	    ("-s, --serial\t\tSpecify the actual serial port that the PolyModem is attached to. Default: /dev/ttyS0.\n");
}

void print(char *message)
{
	if (debug) {
		syslog(LOG_DEBUG, message);
		printf("%s", message);
	}
}

void display(char *heading, unsigned char *buffer, int bytes)
{
	unsigned int line, byte;
	unsigned int lines = (bytes / 16) + 1;
	char output[256];
	char partial[128];

	if (bytes < 1)
		return;


	sprintf(output, "%s: %d (0x%.2x) bytes\n", heading, bytes, bytes);
	print(output);

	for (line = 0; line < lines; line++) {
		output[0] = 0;
		for (byte = 0; byte < 16; byte++) {
			if (((16 * line) + byte) < bytes) {
				sprintf(partial, " %.2x",
					buffer[(16 * line) + byte]);
				strcat(output, partial);
			} else {
				strcat(output, "   ");
			}
		}
		strcat(output, " ");
		for (byte = 0; (byte < 16) && ((line * 16) + byte < bytes);
		     byte++) {
			if (((16 * line) + byte) < bytes) {
				if ((buffer[(16 * line) + byte] < 32)
				    || (buffer[(16 * line) + byte] >
					126)) {
					strcat(output, ".");
				} else {
					sprintf(partial, "%c",
						buffer[(16 * line) +
						       byte]);
					strcat(output, partial);
				}
			} else {
				strcat(output, " ");
			}

		}
		strcat(output, "\n");
		//      printf("%s\n", output);
		print(output);
	}
}

int hangup(int fd, char *buffer)
{
	struct termios *modem;
	speed_t speed;
	modem = (struct termios *) buffer;

	if ((ioctl(fd, TCGETS, modem)) == -1) {
		sprintf(buffer, "ioctl(TCGETS) failed, (%s)",
			strerror(errno));
		printf("%s\n", buffer);
		return errno;
	}
	speed = modem->c_cflag;
	modem->c_cflag &= ~CBAUD;
	modem->c_cflag |= B0;
	if ((ioctl(fd, TCSETS, modem)) == -1) {
		sprintf(buffer, "ioctl(TCSETS) failed, (%s)",
			strerror(errno));
		printf("%s\n", buffer);
		return errno;
	}
	(void) sleep(5);
	modem->c_cflag = speed;
	(void) ioctl(fd, TCSETS, modem);
	(void) sleep(1);
	return 0;
}

void msr_display(int msr)
{
	char line[256];

	line[0] = 0;

	if (msr & TIOCM_LE)
		strcat(line, "LE  ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_DTR)
		strcat(line, "DTR ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_RTS)
		strcat(line, "RTS ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_ST)
		strcat(line, "ST  ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_SR)
		strcat(line, "SR  ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_CTS)
		strcat(line, "CTS ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_CAR)
		strcat(line, "CAR ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_RNG)
		strcat(line, "RNG ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_DSR)
		strcat(line, "DSR ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_CD)
		strcat(line, "CD  ");
	else
		strcat(line, "    ");
	if (msr & TIOCM_RI)
		strcat(line, "RI  ");
	else
		strcat(line, "    ");
	print(line);
}

void display_errors()
{
	char line[256];

	sprintf(line, "%d\t%d\t%d\t%d\t%d\t%d\n", errors[0], errors[1],
		errors[2], errors[3], errors[4], errors[5]);
	print(line);
}
