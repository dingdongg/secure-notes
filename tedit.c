#include <ctype.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

struct termios og_termios;

void disableRawmode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios);
}

void enableRawMode() {
    tcgetattr(STDIN_FILENO, &og_termios);
    atexit(disableRawmode); // revert to original terminal settings after program exits

    // variable to hold terminal attributes
    struct termios raw = og_termios;

    // disable transmission of "Ctrl+S" and "Ctrl+Q" (software flow control)
    // ICRNL disables translating '\r' into '\n'
    // optional flags below don't rly apply to modern terminals,
    // but are still disabled for the sake of convention
    // BRKINT (optional): sent SIGINT to process
    // INPCK (optional): parity checking
    // ISTRIP (optional): set 8th bit to 0, probs already turned off
    raw.c_iflag = raw.c_iflag & ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);
    // disable the canonical mode + transmission of several control characters:
    // - IEXTEN: Ctrl+V, which sends the next character literally
    //      - MacOS: Ctrl+O too, which discards that control character
    // - ISIG  : Ctrl+C/Ctrl+Z, which causes program to exit/suspend
    raw.c_lflag = raw.c_lflag & ~(ECHO | ICANON | IEXTEN | ISIG);

    // set character size to 8 bits
    raw.c_cflag = raw.c_cflag | (CS8);

    // turn off the translation from '\n' to '\r\n'
    raw.c_oflag = raw.c_oflag & ~(OPOST);

    // update terminal attributes
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() {

    enableRawMode();

    char c;
    while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
        // print ascii code only if it's a control character (ie. non-printable)
        if (iscntrl(c)) {
            printf("%d\r\n", c);
        } else {
            printf("%d ('%c')\r\n", c, c);
        }
    }

    return 0;
}
