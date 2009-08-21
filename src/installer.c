#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "lzn.h"

#define DEFAULT_MODEM_DEVICE "/dev/ttyS0"
#define BAUDRATE B115200

/* Function prototypes */

int open_modem();
void parse_command_line(int, char **);
void usage(int, char **);
int find_modem();
int find_basis();
int settings_modem(unsigned char *);
int settings_basis(unsigned char *);
int update_basis();
int update_modem();
int set_filter_basis();
int set_filter_modem();
int reboot_basis();
int reboot_modem();
int send_to_modem(unsigned char *, unsigned int);
int receive_from_modem(unsigned char *, unsigned int);
void display(char *, unsigned char *, unsigned int);
int finish_up(int i);
int hangup(int, char *);
void msr_display(int);

/* Global variables */

int modem_fd = -1;
struct termios oldtio, newtio;
char modem_device[256];
int modem_set = 0;
int debug = 0;
int ignore_errors = 0;

unsigned char basis_eth[6];
unsigned char modem_eth[6];

int main(int argc, char *argv[])
{
	int ret_val = -1;
	int tries = 0;
	unsigned char settings[2];

	parse_command_line(argc, argv);

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = open_modem();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error opening modem, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	printf("Step 1 Find the modem\n");

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = find_modem();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error finding modem, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Steps 2-4 Get the modem settings

	while (tries < 3) {
		printf("Step %d get the modem settings\n", tries + 2);
		tries++;
		ret_val = settings_modem(settings);
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error getting modem settings, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;


	if (settings[0] != 0x03 || settings[1] != 0x03) {
		if (debug)
			printf("Looks like modem needs resetting\n");
		reboot_modem();
		settings_modem(settings);
		settings_modem(settings);
	}

	ret_val = -1;

	// Step 5 Update basis

	printf("Step 5 updating the basis\n");

	while (ret_val != 0 && tries < 2) {
		tries++;
		ret_val = update_basis();
	}

	tries = 0;

	ret_val = -1;


	// Step 6 Update Modem

	printf("Step 6 updating the modem\n");

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = update_modem();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error updating modem, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 7 Get modem settings

	printf("Step 7 getting the modem settings\n");

	while (tries < 1) {
		tries++;
		ret_val = settings_modem(settings);
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error getting modem settings, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 8 Get basis settings

	printf("Step 8 getting the basis settings\n");

	while (ret_val != 0 && tries < 2) {
		tries++;
		ret_val = settings_basis(settings);
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error getting basis settings, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 9 and 10 Get modem settings

	while (tries < 2) {
		printf("Step %d, getting the modem settings\n", tries + 9);
		tries++;
		ret_val = settings_modem(settings);
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error getting modem settings, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 11 Set basis filter

	printf("Step 11 setting the basis filter\n");

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = set_filter_basis();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error setting basis filter, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 12 Set modem filter

	printf("Step 12 setting the modem filter\n");

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = set_filter_modem();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error setting modem filter, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 13 Reboot basis

	printf("Step 13 rebooting basis \n");

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = reboot_basis();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error rebooting basis, please run the program again\n");
		return finish_up(-1);
	}

	ret_val = -1;

	// Step 14 Reboot modem

	printf("Step 14, rebooting the modem \n");

	while (ret_val != 0 && tries < 1) {
		tries++;
		ret_val = reboot_modem();
	}

	tries = 0;

	if (ret_val && !ignore_errors) {
		printf
		    ("Error rebooting modem, please run the program again\n");
		return finish_up(-1);
	}

	return finish_up(0);
}

int open_modem()
{
	unsigned char inbuf[4096];
	int msr = 0;

	if (debug)
		printf("Opening modem %s\n", modem_device);

	modem_fd = open(modem_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (modem_fd < 0) {
		perror(modem_device);
		return -1;
	}

	if ((ioctl(modem_fd, TIOCMGET, &msr)) == -1) {
		printf("ioctl(TIOCMGET) failed, (%s)\n", strerror(errno));
		//return errno;
	}

	msr_display(msr);


	fcntl(modem_fd, F_SETFL, FASYNC);

	tcgetattr(modem_fd, &oldtio);
	bzero(&newtio, sizeof(newtio));
	newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;	// removed CRTSCTS
	//      newtio.c_iflag = IGNPAR | BRKINT;               // seems to work better without the BRKINT
	newtio.c_iflag = IGNPAR;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;
	newtio.c_cc[VMIN] = 4;
	newtio.c_cc[VTIME] = 0;
	tcflush(modem_fd, TCIOFLUSH);
	tcsetattr(modem_fd, TCSANOW, &newtio);

	msr = TIOCM_RTS;

	ioctl(modem_fd, TIOCMBIS, &msr);

	receive_from_modem(inbuf, 4095);

	return 0;
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

int basisdecode(char *basis)
{
	int devno;

	devno = EncodeLZN(basis);
	if (devno == 0)
		return -1;
	if (debug)
		printf("Basis devno is %d or 0x%.4x\n", devno, devno);
	basis_eth[0] = 0x00;
	basis_eth[1] = 0x00;
	basis_eth[2] = 0x04;
	basis_eth[3] = 0x5e;
	basis_eth[4] = devno;
	basis_eth[5] = devno >> 8;
	if (debug)
		printf("Basis address: 0x%.2x%.2x%.2x%.2x%.2x%.2x\n",
		       basis_eth[0], basis_eth[1], basis_eth[2],
		       basis_eth[3], basis_eth[4], basis_eth[5]);

	return 0;
}

int modemdecode(char *modem)
{
	int devno;

	devno = EncodeLZN(modem);
	if (devno == 0)
		return -1;
	if (debug)
		printf("Modem devno is %d or 0x%.4x\n", devno, devno);
	modem_eth[0] = 0x00;
	modem_eth[1] = 0x00;
	modem_eth[2] = 0x04;
	modem_eth[3] = 0x5e;
	modem_eth[4] = devno;
	modem_eth[5] = devno >> 8;
	if (debug)
		printf("Modem address: 0x%.2x%.2x%.2x%.2x%.2x%.2x\n",
		       modem_eth[0], modem_eth[1], modem_eth[2],
		       modem_eth[3], modem_eth[4], modem_eth[5]);
	return 0;
}

void parse_command_line(int argc, char *argv[])
{
	int i;
	char opt;
	char basis[13];
	char modem[13];
	int basis_set = 0;
	int serial_port_set = 0;
	char wordflag[128];

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
			case 'h':
				usage(argc, argv);
				exit(0);
				break;
			case 'i':
				ignore_errors = 1;
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
					strncpy(basis, argv[i + 1], 13);
					if (basisdecode(basis)) {
						printf
						    ("Basis %s is invalid\n",
						     basis);
						exit(-1);
					}
					basis_set = 1;
				} else if (!strcmp(wordflag, "debug")) {
					debug = 1;
				} else if (!strcmp(wordflag, "ignore")) {
					ignore_errors = 1;
				} else if (!strcmp(wordflag, "modem")) {
					if (i + 1 >= argc) {
						printf
						    ("--modem requires a parameter\n");
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
				} else if (!strcmp(wordflag, "help")) {
					usage(argc, argv);
					exit(0);
				} else if (!strcmp(wordflag, "serial")) {
					if (i + 1 >= argc) {
						printf
						    ("--serial requires a parameter\n");
						usage(argc, argv);
						exit(-1);
					}
					strncpy(modem_device, argv[i + 1],
						256);
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
				printf("No such option -%c\n", opt);
				usage(argc, argv);
				exit(-1);
			}
		}
	}
	if (!serial_port_set)
		strcpy(modem_device, DEFAULT_MODEM_DEVICE);
	if (!basis_set) {
		printf
		    ("The basis license number must be provided with the -b flag\n");
		usage(argc, argv);
		exit(-1);
	}
}

void usage(int argc, char *argv[])
{

	printf
	    ("usage: %s -m modem -b basis [-s serial_port] [-h] [-d] [-i]\n",
	     argv[0]);
	printf("\nMandatory flags:\n");
	printf
	    ("-b, --basis\t\tSpecify basis license number. Example: --basis FGBCA953587A.\n");
	printf
	    ("-m, --modem\t\tSpecify modem license number. Example: --modem FGBCA96B36ZC.\n");
	printf("\nOptional flags:\n");
	printf("-d, --debug\t\tEnable extra debugging output.\n");
	printf("-i, --ignore\t\tIgnore errors.\n");
	printf("-h, --help\t\tPrint this usage screen.\n");
	printf
	    ("-s, --serial\t\tSpecify the actual serial port that the PolyModem is attached to. Default: /dev/ttyS0.\n");
}

int find_modem()
{
	unsigned char outbuf[128];
	unsigned char inbuf[256];
	unsigned int i;
	int res = 0;

	if (debug)
		printf("Finding modem\n");

	// Recipient
	for (i = 0; i < 6; i++) {
		outbuf[i] = 0xff;
	}
	// Sender
	for (i = 6; i < 12; i++) {
		outbuf[i] = 0x00;
	}
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0x41;

	send_to_modem(outbuf, 15);

	res = receive_from_modem(inbuf, 128);

	if (debug)
		printf("receive_from_modem returned %d\n", res);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	if (res < 15) {
		printf
		    ("Insufficient bytes read, please run the program again\n");
		return -1;
	}
	if (modem_set) {
		for (i = 1; i < 6; i++) {
			if (inbuf[i - 1] != modem_eth[i]) {
				printf
				    ("Error recipient address (0x%.2x%.2x%.2x%.2x%.2x%.2x) ",
				     0, inbuf[0], inbuf[1],
				     inbuf[2], inbuf[3], inbuf[4]);
				printf
				    ("does not match supplied address (0x%.2x%.2x%.2x%.2x%.2x%.2x)\n",
				     modem_eth[0], modem_eth[1],
				     modem_eth[2], modem_eth[3],
				     modem_eth[4], modem_eth[5]);
				printf("Please run the program again\n");
				return -1;
			}
		}
		for (i = 1; i < 6; i++) {
			if (inbuf[i - 1] != modem_eth[i]) {
				printf
				    ("Error modem detected (0x%.2x%.2x%.2x%.2x%.2x%.2x) ",
				     0, inbuf[6],
				     inbuf[7], inbuf[8],
				     inbuf[9], inbuf[10]);
				printf
				    ("does not match supplied address (0x%.2x%.2x%.2x%.2x%.2x%.2x)\n",
				     modem_eth[0], modem_eth[1],
				     modem_eth[2], modem_eth[3],
				     modem_eth[4], modem_eth[5]);
				printf("Please run the program again\n");
				return -1;
			}
		}
	} else {
		modem_eth[0] = 0;
		for (i = 0; i < 6; i++) {
			modem_eth[i] = inbuf[i + 5];
		}
		for (i = 1; i < 6; i++) {
			if (inbuf[i - 1] != modem_eth[i]) {
				printf
				    ("Error recipient address (0x%.2x%.2x%.2x%.2x%.2x%.2x) ",
				     0x00, inbuf[0],
				     inbuf[1], inbuf[2],
				     inbuf[3], inbuf[4]);
				printf
				    ("does not match detected sender address (0x%.2x%.2x%.2x%.2x%.2x%.2x)\n",
				     modem_eth[0], modem_eth[1],
				     modem_eth[2], modem_eth[3],
				     modem_eth[4], modem_eth[5]);
				printf("Please run the program again\n");
				return -1;
			}
		}
		if (debug)
			printf
			    ("Modem address found: 0x%.2x%.2x%.2x%.2x%.2x%.2x\n",
			     modem_eth[0], modem_eth[1], modem_eth[2],
			     modem_eth[3], modem_eth[4], modem_eth[5]);
	}
	if (inbuf[14] != 0x42) {
		printf
		    ("Incorrect response from modem, 0x%.2x should be 0x42 please run the program again\n",
		     inbuf[14]);
		return -1;
	}
	printf("Found modem OK\n");
	return 0;
}

int find_basis()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Finding basis\n");

	// Recipient
	for (i = 0; i < 6; i++) {
		outbuf[i] = basis_eth[i];
	}
	// Sender
	for (i = 6; i < 12; i++) {
		outbuf[i] = modem_eth[i];
	}
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0x81;

	send_to_modem(outbuf, 15);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	printf("Found basis OK\n");

	return 0;
}

int settings_modem(unsigned char *settings)
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Checking modem settings\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = modem_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}

	outbuf[11] = 0x00;

	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0xc0;

	send_to_modem(outbuf, 15);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	if (inbuf[14] == 0xfe || inbuf[14] == 0x42) {
		if (debug)
			printf
			    ("Read answer from old command, rereading\n");
		res = receive_from_modem(inbuf, 128);

		if (res < 1)
			return -1;

		if (debug)
			display("inbuf", inbuf, res);
	}

	if (inbuf[14] != 0xc1)
		return -1;

	if (debug) {
		printf("Read modem settings OK:\n");
		printf("\tHardware version:\t0x%.2x%.2x\n", inbuf[15],
		       inbuf[16]);
		printf("\tDSP type:\t\t0x%.2x%.2x\n", inbuf[17],
		       inbuf[18]);
		printf("\tFlash type:\t\t0x%.2x%.2x\n", inbuf[19],
		       inbuf[20]);
		printf("\tFirmware:\t\t0x%.2x%.2x\n", inbuf[21],
		       inbuf[22]);
	}

	settings[0] = inbuf[21];
	settings[1] = inbuf[22];

	return 0;
}

int settings_basis(unsigned char *settings)
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Checking basis settings\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = basis_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0xc0;

	send_to_modem(outbuf, 15);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	if (inbuf[14] != 0xc1)
		return -1;


	if (debug) {
		printf("Read basis settings OK:\n");
		printf("\tHardware version:\t0x%.2x%.2x\n", inbuf[15],
		       inbuf[16]);
		printf("\tDSP type:\t\t0x%.2x%.2x\n", inbuf[17],
		       inbuf[18]);
		printf("\tFlash type:\t\t0x%.2x%.2x\n", inbuf[19],
		       inbuf[20]);
		printf("\tFirmware:\t\t0x%.2x%.2x\n", inbuf[21],
		       inbuf[22]);
	}

	settings[0] = inbuf[21];
	settings[1] = inbuf[22];

	return 0;
}

int update_basis()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Updating basis\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = basis_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0xc8;

	send_to_modem(outbuf, 15);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	if (inbuf[14] == 0xc1) {
		if (debug)
			printf("Read result of old command, rereading\n");
		res = receive_from_modem(inbuf, 128);
	}

	if (inbuf[14] != 0xfe)
		return -1;

	printf("Updated basis OK\n");

	return 0;
}

int update_modem()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Updating modem\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = modem_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0xc8;

	send_to_modem(outbuf, 15);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	if (inbuf[14] == 0xc1) {
		if (debug)
			printf("Read result of old command, rereading\n");
		res = receive_from_modem(inbuf, 128);
	}

	if (inbuf[14] != 0xfe)
		return -1;

	printf("Updated modem OK\n");

	return 0;
}

int set_filter_basis()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Setting the basis filter\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = basis_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of command

	outbuf[14] = 0xcf;
	outbuf[15] = 0x02;

	// Gateway goes in here
	for (i = 0; i < 5; i++) {
		outbuf[i + 16] = basis_eth[i + 1];
	}

	outbuf[21] = 0x00;

	// Blank space

	outbuf[22] = 0x00;
	outbuf[23] = 0x00;

	// Modem goes here
	for (i = 0; i < 5; i++) {
		outbuf[i + 24] = modem_eth[i + 1];
	}

	outbuf[29] = 0x00;

	// Blank space

	outbuf[30] = 0x00;
	outbuf[31] = 0x00;

	send_to_modem(outbuf, 32);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	printf("Set basis filter OK\n");

	return 0;
}

int set_filter_modem()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Setting the modem filter\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = modem_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of command

	outbuf[14] = 0xcf;
	outbuf[15] = 0x02;

	// Gateway goes in here
	for (i = 0; i < 6; i++) {
		outbuf[i + 16] = basis_eth[i];
	}

	// Blank space

	outbuf[22] = 0x00;
	outbuf[23] = 0x00;

	// Modem goes here
	for (i = 0; i < 6; i++) {
		outbuf[i + 24] = modem_eth[i];
	}

	// Blank space

	outbuf[30] = 0x00;
	outbuf[31] = 0x00;

	send_to_modem(outbuf, 32);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	printf("Set modem filter OK\n");

	return 0;
}

int reboot_basis()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Rebooting basis\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = basis_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0xc7;
	outbuf[15] = 0x00;

	send_to_modem(outbuf, 16);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	printf("Rebooted basis OK\n");

	return 0;
}

int reboot_modem()
{
	unsigned char outbuf[128];
	unsigned int i;
	int res = 0;
	unsigned char inbuf[256];

	if (debug)
		printf("Rebooting modem\n");

	// Recipient
	for (i = 0; i < 5; i++) {
		outbuf[i] = modem_eth[i + 1];
	}
	outbuf[5] = 0x00;
	// Sender
	for (i = 6; i < 11; i++) {
		outbuf[i] = modem_eth[i - 5];
	}
	outbuf[11] = 0x00;
	// Ethernet type always 0x88 0x89
	outbuf[12] = 0x88;
	outbuf[13] = 0x89;

	// Rest of data

	outbuf[14] = 0xc7;
	outbuf[15] = 0x00;

	send_to_modem(outbuf, 16);

	res = receive_from_modem(inbuf, 128);

	if (res < 1)
		return -1;

	if (debug)
		display("inbuf", inbuf, res);

	printf("Rebooted modem OK\n");

	return 0;
}

int send_to_modem(unsigned char *buffer, unsigned int bytes)
{
	unsigned char outbuf[256];
	unsigned int i;
	int res;
	int msr;
	char errorbuf[256];

	if (debug)
		printf("Sending to modem %d bytes\n", bytes);

	outbuf[0] = 0x55;
	outbuf[1] = bytes;
	outbuf[2] = 0x00;

	for (i = 0; i < bytes; i++) {
		outbuf[3 + i] = buffer[i];
	}

	msr = TIOCM_RTS;

	ioctl(modem_fd, TIOCMBIS, &msr);

	msr = 0;

	while (!(msr & TIOCM_CTS)) {

		if ((ioctl(modem_fd, TIOCMGET, &msr)) == -1) {
			sprintf(errorbuf, "ioctl(TIOCMGET) failed, (%s)",
				strerror(errno));
			printf("%s\n", errorbuf);
			//return errno;
		}
		if (debug)
			msr_display(msr);

		// We cant do it if its 4146 it must be 4126
	}

	tcsendbreak(modem_fd, 0);

	if (debug)
		display("send", outbuf, i + 3);

	res = write(modem_fd, outbuf, i + 3);

	if (debug)
		printf("send_to_modem about to return %d\n", res);

	return res;
}

int receive_from_modem(unsigned char *buffer, unsigned int bytes)
{
	unsigned char inbuf[bytes + 5];
	int res = 0;
	int i, msr;
	fd_set mwait;
	struct timeval timeout;

	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	//      tcsendbreak(modem_fd, 0);

	FD_ZERO(&mwait);
	FD_SET(modem_fd, &mwait);
	if (debug)
		printf("Waiting for data...\n");
	if (select(modem_fd + 1, &mwait, NULL, NULL, &timeout) < 0) {
		perror("select");
		return -1;
	}
	if (FD_ISSET(modem_fd, &mwait)) {
		if (debug)
			printf("Reading data from modem\n");
		res = read(modem_fd, inbuf, bytes + 4);

		if (debug)
			printf("read returned %d bytes\n", res);

		if (res < 1)
			return -1;

		msr = TIOCM_RTS;

		ioctl(modem_fd, TIOCMBIC, &msr);

		if (res > 1 && debug)
			display("recv", inbuf, res);

		for (i = 0; i < res - 4 && i < bytes; i++) {
			buffer[i] = inbuf[4 + i];
		}
		if (debug)
			printf("receive_from_modem about to return %d\n",
			       i);

		return i;
	}
	if (debug)
		printf("returning -1 from receive_from_modem\n");

	return -1;
}


void display(char *heading, unsigned char *buffer, unsigned int bytes)
{
	unsigned int line, byte;
	unsigned int lines = (bytes / 16) + 1;

	printf("%s:\n", heading);
	for (line = 0; line < lines; line++) {
		for (byte = 0; byte < 16; byte++) {
			if (((16 * line) + byte) < bytes) {
				printf(" %.2x",
				       buffer[(16 * line) + byte]);
			} else {
				printf("   ");
			}
		}
		printf(" ");
		for (byte = 0; (byte < 16) && ((line * 16) + byte < bytes);
		     byte++) {
			if (((16 * line) + byte) < bytes) {
				if (buffer[(16 * line) + byte] < 32) {
					printf(".");
				} else {
					printf("%c",
					       buffer[(16 * line) + byte]);
				}
			} else {
				printf(" ");
			}

		}
		printf("\n");
	}
}

int finish_up(int i)
{

	int msr = 0;
	printf("Finishing up\n");

	tcsetattr(modem_fd, TCSANOW, &oldtio);

	msr = TIOCM_RTS;

	ioctl(modem_fd, TIOCMBIS, &msr);


	if ((ioctl(modem_fd, TIOCMGET, &msr)) == -1) {
		printf("ioctl(TIOCMGET) failed, (%s)\n", strerror(errno));
		//return errno;
	}

	msr_display(msr);

	close(modem_fd);
	return i;
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
	printf("%s\n", line);
}
