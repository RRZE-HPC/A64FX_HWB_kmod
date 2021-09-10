#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>


int main()
{
	int fd;
	int ret;
	int dummy = 42;
	
	fd = open("/dev/fujitsu_hwb", O_RDONLY);

	ret = ioctl(fd, _IOWR('F', 0x05, int), &dummy);

    close(fd);
	return 0;
}
