#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>

// DEFINES //

#define EDITOR_VER "0.0.1"

// Ctrl key macro; sets upper 3 bits to 0
// Ctrl key strips bits 5 and 6 from `key` and sends that 
// ex) Ctrl+A: A is 0x1100001; A & 0x1F yields 0x1
#define CTRL_KEY(key) ((key) & 0x1F)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY,
};

// DATA //

struct editorConfig {
    int cx, cy;
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
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        // errno is a global variable set by most C library functions when they fail
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    // map arrow keys to cursor movement
    if (c == '\x1b') {
        char seq[3]; // length is 3 to handle longer escape sequences in the future

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;      // \x1b[A
                    case 'B': return ARROW_DOWN;    // \x1b[B
                    case 'C': return ARROW_RIGHT;   // \x1b[C
                    case 'D': return ARROW_LEFT;    // \x1b[D
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
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
        // print welcome message on screen
        if (y == E.screenRows / 3) {
            char welcome[80];
            int welcomeLen = snprintf(
                welcome, 
                sizeof(welcome), 
                "EDITOR -- VERSION %s", 
                EDITOR_VER
            );
            // truncate message if longer than terminal width
            if (welcomeLen > E.screenCols) welcomeLen = E.screenCols;

            // add padding to center the message
            int padding = (E.screenCols - welcomeLen) / 2;
            if (padding) {
                abufAppend(ab, "~", 1);
                padding--; // to account for the "~" we just added
            }
            while (padding--) abufAppend(ab, " ", 1);
            abufAppend(ab, welcome, welcomeLen);
        } else {
            abufAppend(ab, "~", 1);
        }

        // erase line to the right of the cursor
        abufAppend(ab, "\x1b[K", 3);
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

    // hide cursor in case cursor is displayed in the middle of screen while drawing to screen
    abufAppend(&ab, "\x1b[?25l", 6);

    // reset cursor to top left of terminal
    abufAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    // re-position cusor after rendering rows
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1); // cursor pos is 1-indexed
    abufAppend(&ab, buf, strlen(buf));

    // show cursor after refresh is complete
    abufAppend(&ab, "\x1b[?25h", 6);

    // flush buffer contents to stdout
    write(STDOUT_FILENO, ab.b, ab.len);
    abufFree(&ab);
}

// INPUT //

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) E.cx--;
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screenCols - 1) E.cx++;
            break;
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenRows - 1) E.cy++;
            break;
    }
}

/**
 * waits for a keypress and then handles it accordingly
 * ex) Ctrl+Q exits the program
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_DOWN:
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN: 
            {
                int times = E.screenRows;
                while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screenCols - 1;
            break;
    }
}

// INIT //

void initEditor() {
    E.cx = 0;
    E.cy = 0;
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
