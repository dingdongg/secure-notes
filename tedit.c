#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

// DEFINES //

// Ctrl key macro; sets upper 3 bits to 0
// Ctrl key strips bits 5 and 6 from `key` and sends that 
// ex) Ctrl+A: A is 0x1100001; A & 0x1F yields 0x1
#define CTRL_KEY(key) ((key) & 0x1F)

// DATA //

struct editorConfig {
    int screenRows;
    int screenCols;
    struct termios og_termios;
};

struct editorConfig E;

// TERMINAL //

void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawmode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.og_termios) == -1) {
        die("tcsetattr");
    }
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.og_termios) == -1) die("tcgetattr");
    atexit(disableRawmode); // revert to original terminal settings after program exits

    // variable to hold terminal attributes
    struct termios raw = E.og_termios;

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

// calculates cursor positiion. Used as fallback for calculating terminal size
int getCursorPosition(int *rows, int *cols) {
    // stdin buffer
    char buf[32];
    unsigned int i = 0;

    // queries current cursor position
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break; // read until we hit 'R'
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // workaround for systems that don't allow window size requests
        // move cursor to bottom right, then query its current position
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}

// APPEND BUFFER //

// buffer used to make write() calls more efficient by writing in bulk,
// rather than numerous small write() calls
struct appendBuffer {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

// append `s` of length `len` to existing buffer
void abufAppend(struct appendBuffer *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abufFree(struct appendBuffer *ab) {
    free(ab->b);
}

// OUTPUT //

/**
 * draw each row of text file being edited
 */
void editorDrawRows(struct appendBuffer *ab) {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        abufAppend(ab, "~", 1);

        if (y < E.screenRows - 1) {
            abufAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct appendBuffer ab = ABUF_INIT;
    // write escape sequence to the terminal
    // - escape sequences are used to instruct terminal to do text formatting tasks
    //   like coloring text, moving cursor, clearing screen
    // comprehensive VT100 escape sequence docs: https://vt100.net/docs/vt100-ug/chapter3.html
    abufAppend(&ab, "\x1b[2J", 4);

    // reset cursor to top left of terminal
    abufAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // re-position cusor after rendering rows
    abufAppend(&ab, "\x1b[H", 3);

    // flush buffer contents to stdout
    write(STDOUT_FILENO, ab.b, ab.len);
    abufFree(&ab);
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
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

// INIT //

void initEditor() {
    // get terminal's current dimensions
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
