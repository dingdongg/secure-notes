#include <stdio.h>
#include <termios.h>
#include <unistd.h>

void enableRawMode() {
    // variable to hold terminal attributes
    struct termios raw;
    
    // get terminal attributes
    tcgetattr(STDIN_FILENO, &raw);

    // disable the echo functionality
    // on by default in canonical mode
    // echo just prints user input to stdout
    raw.c_lflag = raw.c_lflag & ~(ECHO);

    // update terminal attributes
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {
    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
    return 0;
}
