// vdockd.c - Virtual Dock Daemon

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <stdbool.h>

#include <signal.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <systemd/sd-daemon.h>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

#define DEFAULT_SOCKET "/run/vdockd.socket"
#define DEFAULT_DEVNAME "Virtual Dock"
#define DEFAULT_VENDOR  0x1337
#define DEFAULT_PRODUCT 0x1337

#define _STR(A) #A
#define STRING(A) _STR(A)
#define LENGTH(A) (sizeof(A)/sizeof(*A))

typedef enum {
	DOCK   = 0, // laptop has been docked
	UNDOCK = 1, // laptop has been undocked
	_NUM_EVTS, 
	_UNSET = -1,
} Event_t;

static const char *EventMap[_NUM_EVTS] = {
	[DOCK]   = "DOCK",
	[UNDOCK] = "UNDOCK"
};

typedef struct {
	const char *socket;
	const char *name;
	Event_t     event;
	uint16_t    vendor;
	uint16_t    product;
	bool        daemon;
	bool        verbose;
} Opts_t;

static void PrintError(const char *msg)
{
	fprintf(stderr, "%s: %s (%d)\n", msg, strerror(errno), errno);
}

static bool CaughtSignal;

static int SignalList[] = { SIGINT, SIGTERM };

static void SignalHandler(int sig)
{
	(void)sig;
	CaughtSignal = true;

	// inform systemd we're shutting down
	sd_notify(0, "STOPPING=1");
}

static bool IgnoreSignals()
{
	struct sigaction sa = {0};
	sa.sa_handler = SIG_IGN;
	for (int i = 0; i < (int)LENGTH(SignalList); i++)
	{
		if (sigaction(SignalList[i], &sa, NULL) == -1) {
			PrintError("sigaction()");
			return true;
		}
	}
	return false;
}

static bool AllowSignals()
{
	struct sigaction sa = {0};
	sa.sa_handler = &SignalHandler;
	sigemptyset(&sa.sa_mask);
	CaughtSignal = false;
	for (int i = 0; i < (int)LENGTH(SignalList); i++)
	{
		if (sigaction(SignalList[i], &sa, NULL) == -1) {
			PrintError("sigaction()");
			return true;
		}
	}
	return false;
}

static int ParseInt(const char *str, bool *success)
{
	if (str == NULL) {
		if (success != NULL)
			*success = false;
		return 0;
	}

	int base = 10;
	if (!strncasecmp(str, "0x", 2)) {
		str += 2;	
		base = 16;
	} else if (*str == '0') {
		str += 1;
		base = 8;
	}

	int ret = strtoul(str, NULL, base);
	if (success != NULL)
		*success = errno != EINVAL;
	return ret;
}

static void Usage(const char *progname)
{
	fprintf(stderr, "Virtual Dock Daemon\n");
	fprintf(stderr, "\nUsage: %s [OPTIONS] -d|--daemon | [OPTIONS] EVENT\n", progname);
	fprintf(stderr, "\nWhere EVENT is one of:\n");
	fprintf(stderr, "  DOCK   (Laptop has been docked)\n");
	fprintf(stderr, "  UNDOCK (Laptop has been undocked)\n");
	fprintf(stderr, "\nWhere OPTIONS are:\n");
	fprintf(stderr, "  -d,--daemon        Run as daemon (otherwise, trigger event)\n");
	fprintf(stderr, "  -s,--socket=FILE   Daemon socket (" DEFAULT_SOCKET ")\n");
	fprintf(stderr, "  -n,--name=STRING   Virtual device name (" DEFAULT_DEVNAME ")\n");
	fprintf(stderr, "  -e,--vendor=INT    Virtual device vendor ID (" STRING(DEFAULT_VENDOR) ")\n");
	fprintf(stderr, "  -p,--product=INT   Virtual device product ID (" STRING(DEFAULT_PRODUCT) ")\n");
	fprintf(stderr, "  -v,--verbose       Verbose output\n");
	fprintf(stderr, "  -h,--help          Show this help and exit\n");
	fprintf(stderr, "NOTE: only -v|--verbose and -h|--help are effective without -d|--daemon\n");
}

static bool ParseOptions(char *argv[], int argc, Opts_t *opts, int *ret)
{
	// load default options
	*opts = (Opts_t){
		DEFAULT_SOCKET,
		DEFAULT_DEVNAME,
		_UNSET,
		DEFAULT_VENDOR,
		DEFAULT_PRODUCT,
		false,
		false
	};

	static struct option long_opts[] = {
		{ "daemon" , no_argument      , NULL, 'd'},
		{ "socket" , required_argument, NULL, 's'},
		{ "name"   , required_argument, NULL, 'n'},
		{ "vendor" , required_argument, NULL, 'e'},
		{ "product", required_argument, NULL, 'p'},
		{ "verbose", no_argument      , NULL, 'v'},
		{ "help"   , no_argument      , NULL, 'h'},
		{0} // END
	};

	bool _exit = false;
	while (!_exit)
	{
		bool tmp;
		int index, c;
		c = getopt_long(argc, argv, "ds:n:e:p:vh", long_opts, &index);
		if (c == -1)
			break;
		switch (c)
		{
		case 'd':
			opts->daemon = true;
			break;
		case 's':
			opts->socket = optarg;
			break;
		case 'n':
			opts->name = optarg;
			break;
		case 'e':
			opts->vendor = ParseInt(optarg, &tmp);
			if (!tmp) {
				fprintf(stderr, "Invalid vendor ID '%s'.\n", optarg);
				*ret = 1;
				_exit = true;
			}
			break;
		case 'p':
			opts->product = ParseInt(optarg, &tmp);
			if (!tmp) {
				fprintf(stderr, "Invalid product ID '%s'.\n", optarg);
				*ret = 1;
				_exit = true;
			}
			break;
		case 'v':
			opts->verbose = true;
			break;
		case 'h':
			Usage(argv[0]);
			return true; // early exit
		//case '?':
		default:
			Usage(argv[0]);
			*ret  = 1;
			return true; // early exit
		}
	}

	// extra arguments
	if (optind < argc) {
		if (opts->daemon) {
			fprintf(stderr, "Daemon does not expect non-option arguments.\n");
		} else if (optind == (argc - 1)) {
			for (int i = 0; i < (int)LENGTH(EventMap); i++)
			{
				if (strcasecmp(argv[optind], EventMap[i]) == 0) {
					opts->event = (Event_t)i;
					goto _return;
				}
			}
			fprintf(stderr, "Unknown event '%s'!\n", argv[optind]);
		} else {
			fprintf(stderr, "Expected one non-option argument!\n");
		}
		*ret = 1;
		_exit = true;
	}

	// no event specified
	if (!_exit && !opts->daemon && (opts->event == _UNSET)) {
		Usage(argv[0]);
		fprintf(stderr, "\nNo event specified!\n");
		*ret = 1;
		_exit = true;
	}

_return:
	return _exit;
}

static int RunDaemon(Opts_t *opts)
{
	int fd = -1, rv, ret = 1;

	struct libevdev *dev = NULL;
	struct libevdev_uinput *uidev = NULL;

	struct sockaddr_un addr = {0};

	// prepare address
	if (strlen(opts->socket) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "Socket name '%s' is too long!", opts->socket);
		goto _return;
	}
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, opts->socket, sizeof(addr.sun_path) - 1);

	// create socket
	bool retry = false, unlink_socket = true;
_create_socket:
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		PrintError("socket()");
		goto _return;
	}

	// bind socket
	rv = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rv == -1) {
		if (retry || (errno != EADDRINUSE)) {
			PrintError("bind()");
			goto _return;
		}

		// check if another instance is running
		rv = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
		if (rv != -1) {
			unlink_socket = false;
			fprintf(stderr, "Another instance is running on socket %s\n", opts->socket);
			goto _return;
		}

		rv = unlink(opts->socket);
		if (rv == -1) {
			PrintError("unlink()");
			goto _return;
		}

		retry = true;
		goto _create_socket;
	}

	// only allow daemon user (root) to connect
	if (fchmod(fd, 0600) == -1) {
		PrintError("chmod()");
		goto _return;
	}

	// create event device
	dev = libevdev_new();
	if (dev == NULL) {
		fprintf(stderr, "libevdev_new() failed.\n");
		goto _return;
	}

	// setup virtual device
	libevdev_set_name(dev, opts->name);
	libevdev_enable_event_type(dev, EV_SW);
	libevdev_enable_event_code(dev, EV_SW, SW_DOCK, NULL);
	libevdev_set_id_vendor(dev, opts->vendor);
	libevdev_set_id_product(dev, opts->product);

	// create uinput device
	rv = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
	if (rv != 0) {
		fprintf(stderr, "libevdev_uinput_create_from_device(): error %d\n", rv);
		goto _return;
	}

	if (opts->verbose)
		printf("Created uinput device '%s' at %s.\n", libevdev_get_name(dev), libevdev_uinput_get_devnode(uidev));

	rv = listen(fd, 16);
	if (rv == -1) {
		PrintError("listen()");
		goto _return;
	}

	if (opts->verbose)
		printf("Listening on socket %s.\n", opts->socket);

	if (AllowSignals())
		goto _return;

	// inform systemd we're ready
	sd_notify(0, "READY=1");

	while (!CaughtSignal)
	{
		bool data = false;
		char byte;

		// wait for new client
		int client = accept(fd, NULL, NULL);
		if (client == -1) {
			if (errno != EINTR)
				PrintError("accept()");
			goto _close;
		}

		// timeout after 250 ms
		static const struct timeval tv = {0, 250000};

		// set client timeout (prevent DOS)
		if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == -1) {
			if (errno != EINTR)
				PrintError("setsockopt()");
			goto _close;
		}

		// receive single byte
		if (recv(client, &byte, 1, 0) != 1) {
			if (errno == ETIMEDOUT) {
				fprintf(stderr, "Client timed out.\n");
				goto _close;
			}
		} else {
			data = true;
		}
_close:
		if (client != -1) {
			shutdown(client, SHUT_RDWR);
			close(client);
		}

		if (data) {
			Event_t event = byte;
			if ((event >= _NUM_EVTS) || (event < 0)) {
				if (opts->verbose)
					printf("Received invalid event %d.\n", byte);
				continue;
			}
			if (opts->verbose)
				printf("Received event '%s'.\n", EventMap[event]);
			switch (event)
			{
			case DOCK:
				libevdev_uinput_write_event(uidev, EV_SW, SW_DOCK, 1);
				break;
			case UNDOCK:
				libevdev_uinput_write_event(uidev, EV_SW, SW_DOCK, 0);
				break;
			default:
				break; // make the compiler happy
			}
			libevdev_uinput_write_event(uidev, EV_SYN, SYN_REPORT, 0);
		}	
	}

	if (IgnoreSignals())
		goto _return;

	ret = 0;
_return:
	// cleanup
	if (uidev != NULL) {
		if (opts->verbose)
			printf("Removing uinput device '%s' at %s.\n", libevdev_get_name(dev), libevdev_uinput_get_devnode(uidev));
		libevdev_uinput_destroy(uidev);
	}
	if (dev != NULL) {
		libevdev_free(dev);
	}
	if (fd != -1) {
		close(fd);
		if (unlink_socket)
			unlink(opts->socket);
	}

	return ret;
}

static int DispatchEvent(Opts_t *opts)
{
	int fd = -1, rv, ret = 1;

	struct sockaddr_un addr = {0};

	// prepare address
	if (strlen(opts->socket) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "Socket name '%s' is too long!", opts->socket);
		goto _return;
	}
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, opts->socket, sizeof(addr.sun_path) - 1);

	// create socket
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		PrintError("socket()");
		goto _return;
	}

	// connect to daemon
	rv = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
	if (rv != 0) {
		PrintError("Failed to connect to daemon");
		goto _return;
	}

	// transmit event
	char byte = (char)opts->event;
	if (send(fd, &byte, 1, 0) != 1) {
		PrintError("send()");
		goto _return;
	}

	if (opts->verbose)
		printf("Sent event '%s' to daemon.\n", EventMap[opts->event]);

	ret = 0;
_return:
	if (fd != -1) {
		shutdown(fd, SHUT_RDWR);
		close(fd);
	}

	return ret;
}

int main(int argc, char *argv[])
{
	if (IgnoreSignals())
		return 1;
	
	int ret = 0;
	Opts_t opts;

	if (ParseOptions(argv, argc, &opts, &ret))
		return ret;

	if (opts.daemon)
		return RunDaemon(&opts);
	else
		return DispatchEvent(&opts);
}
