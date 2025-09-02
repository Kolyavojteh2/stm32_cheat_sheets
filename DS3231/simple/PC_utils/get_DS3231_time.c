// get_time.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#define SERIAL_PORT "/dev/ttyUSB0"

int main() {
    int serial = open(SERIAL_PORT, O_RDWR | O_NOCTTY);
    if (serial < 0) {
        perror("open");
        return 1;
    }

    struct termios tty;
    tcgetattr(serial, &tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tcsetattr(serial, TCSANOW, &tty);

    for (int i = 0; i < 5; i++) {
        char *get_cmd = "GET\n";
        write(serial, get_cmd, strlen(get_cmd));

        char buffer[128];
        int n = read(serial, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            printf("Received: %s", buffer);
        } else {
            printf("No response.\n");
        }
    }

    close(serial);
    return 0;
}
