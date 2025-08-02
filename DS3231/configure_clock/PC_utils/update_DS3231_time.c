// set_time.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // Format: "SET:SS:MM:HH:DOW:DD:MM:YYYY\n"
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "SET:%02d:%02d:%02d:%d:%02d:%02d:%04d\n",
             t->tm_sec, t->tm_min, t->tm_hour,
             t->tm_wday ? t->tm_wday : 7,  // day of week (1 = Monday, 7 = Sunday)
             t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);

    write(serial, buffer, strlen(buffer));
    printf("Time sent: %s", buffer);

    close(serial);
    return 0;
}
