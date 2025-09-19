#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <poll.h>

// Must match the definitions in the driver
#define TSADC_MAGIC 'T'
#define TSADC_SET_CHANNEL _IOW(TSADC_MAGIC, 1, int)
#define TSADC_GET_CHANNEL _IOR(TSADC_MAGIC, 2, int)
#define TSADC_SET_INT_THRESHOLD _IOW(TSADC_MAGIC, 3, int)

int main(int argc, const char* argv[]) {
    int fd;
    char buffer[16];
    int channel, threshold;
    struct pollfd pfd;
    int ret;

    fd = open("/dev/rk3588_tsadc", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    // --- Test 1: Set channel and read temperature ---
    printf("--- Test 1: Basic Read ---\n");
    if(argc > 1)
	channel = atoi(argv[1]);
    else
    	channel = 0; // Use channel 0
    
    if (ioctl(fd, TSADC_SET_CHANNEL, &channel) < 0) {
        perror("ioctl TSADC_SET_CHANNEL failed");
    }
    printf("Set channel to %d\n", channel);

    printf("Reading temperature for 5 seconds...\n");
    for (int i = 0; i < 5; ++i) {
        lseek(fd, 0, SEEK_SET); // Reset read pointer
        if (read(fd, buffer, sizeof(buffer)) > 0) {
            printf("Temperature: %sC", buffer);
        }
        sleep(1);
    }


    close(fd);
    return 0;
}
