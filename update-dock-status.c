// update-dock-status.c - Set initial dock status for vdockd

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

// requires ec_sys module to be loaded, ensured by systemd service
#define EC_SYSFS_PATH "/sys/kernel/debug/ec/ec0/io"

// tested for Thinkpad W520 (no idea what these are for other models)
#define DOCKED_BYTE 0x48
#define DOCKED_BIT  5

static void PrintError(const char *msg)
{
	fprintf(stderr, "%s: %s (%d)\n", msg, strerror(errno), errno);
}

int main()
{
	char ec_mem[256] = {0};
	int fd, rv;

	fd = open(EC_SYSFS_PATH, O_RDONLY);
	if (fd == -1) {
		PrintError("open()");
		return 1;
	}

	rv = read(fd, ec_mem, sizeof(ec_mem));
	close(fd);
	if (rv == -1) {
		PrintError("read()");
		return 1;
	}

	if (rv != sizeof(ec_mem)) {
		fprintf(stderr, "WARNING: Expected %d bytes of EC memory, got %d!\n", (int)sizeof(ec_mem), rv);
	}

	char *const argv[] = { "vdockd", ec_mem[DOCKED_BYTE] & (1 << DOCKED_BIT) ? "DOCK" : "UNDOCK" };
	execv(argv[0], argv);
	PrintError("execv()");

	return 1;
}
