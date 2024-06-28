#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

// DEFINES //

// Ctrl key macro; sets upper 3 bits to 0
// Ctrl key strips bits 5 and 6 from `key` and sends that 
// ex) Ctrl+A: A is 0x1100001; A & 0x1F yields 0x1
#define CTRL_KEY(key) ((key) & 0x1F)

// DATA //

struct termios og_termios;

// TERMINAL //

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawmode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &og_termios) == -1) die("tcgetattr");
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

    // # of bytes to required before read() returns
    raw.c_cc[VMIN] = 0;

    // # max amount of time to wait before read() returns (in 100ms)
    raw.c_cc[VTIME] = 10;

    // update terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/**
 * wait for keypress and return it
 */
char editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // errno is a global variable set by most C library functions when they fail
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

// OUTPUT //

void editorRefreshScreen() {
    // write escape sequence to the terminal
    // - escape sequences are used to instruct terminal to do text formatting tasks
    //   like coloring text, moving cursor, clearing screen
    // comprehensive VT100 escape sequence docs: https://vt100.net/docs/vt100-ug/chapter3.html
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // reset cursor to top left of terminal
    write(STDOUT_FILENO, "\x1b[H", 3);
}

// INPUT //

/**
 * waits for a keypress and then handles it accordingly
 * ex) Ctrl+Q exits the program
 */
void editorProcessKeypress() {
    char c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}

// INIT //

int main() {
    enableRawMode();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
