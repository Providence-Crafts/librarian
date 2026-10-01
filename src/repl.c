#include "repl.h"

#include "ui.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define REPL_LINE_CAP_INIT 256
#define REPL_HISTORY_CAP_INIT 64
#define REPL_MAX_MENU_ITEMS 16

typedef enum {
    KEY_NONE = 0,
    KEY_ENTER,
    KEY_TAB,
    KEY_SHIFT_TAB,
    KEY_BACKSPACE,
    KEY_DELETE,
    KEY_ARROW_UP,
    KEY_ARROW_DOWN,
    KEY_ARROW_LEFT,
    KEY_ARROW_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_CTRL_C,
    KEY_CTRL_D,
    KEY_CTRL_L,
    KEY_ESC,
    KEY_CHAR
} repl_key_t;

typedef struct {
    char *insert_text;
    char *display_text;
    bool is_dir;
} candidate_item_t;

typedef struct {
    candidate_item_t *items;
    size_t count;
    size_t cap;
    size_t replace_from;
} candidate_list_t;

struct repl_context {
    char *history_path;
    char **history;
    size_t history_count;
    size_t history_cap;

    /* Line buffer */
    char *line_buf;
    size_t line_len;
    size_t line_cap;
    size_t cursor;

    /* Temporary scratch buffer when navigating history */
    char *scratch_buf;

    /* Terminal state */
    bool is_raw;
    struct termios orig_termios;
    int tty_fd;
};

static volatile sig_atomic_t g_in_raw_mode = 0;
static struct termios g_saved_termios;
static int g_saved_fd = -1;

static void repl_sig_handler(int sig)
{
    if (g_in_raw_mode && g_saved_fd >= 0) {
        tcsetattr(g_saved_fd, TCSANOW, &g_saved_termios);
        g_in_raw_mode = 0;
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void install_signals(int fd, const struct termios *orig)
{
    g_saved_fd = fd;
    g_saved_termios = *orig;
    g_in_raw_mode = 1;

    signal(SIGINT, repl_sig_handler);
    signal(SIGTERM, repl_sig_handler);
    signal(SIGQUIT, repl_sig_handler);
}

static void restore_signals(void)
{
    g_in_raw_mode = 0;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

static bool enable_raw_mode(repl_context_t *repl)
{
    if (!isatty(repl->tty_fd)) {
        return false;
    }
    if (tcgetattr(repl->tty_fd, &repl->orig_termios) == -1) {
        return false;
    }

    struct termios raw = repl->orig_termios;
    /* Input modes: disable break signal, CR to NL, parity check, strip 8th bit, flow control */
    raw.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output modes: disable post-processing */
    raw.c_oflag |= (tcflag_t)(OPOST | ONLCR);
    /* Control modes: 8-bit chars */
    raw.c_cflag |= (tcflag_t)(CS8);
    /* Local modes: disable echoing, canonical mode, extended input processing, signal chars */
    raw.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    /* Control characters: return each byte as it arrives */
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(repl->tty_fd, TCSANOW, &raw) == -1) {
        return false;
    }

    install_signals(repl->tty_fd, &repl->orig_termios);
    repl->is_raw = true;
    return true;
}

static void disable_raw_mode(repl_context_t *repl)
{
    if (repl->is_raw) {
        tcsetattr(repl->tty_fd, TCSANOW, &repl->orig_termios);
        restore_signals();
        repl->is_raw = false;
    }
}

static repl_key_t read_key(int fd, char *out_char)
{
    unsigned char c = 0;
    ssize_t n = read(fd, &c, 1);
    if (n <= 0) {
        return KEY_NONE;
    }

    if (c == '\r' || c == '\n')
        return KEY_ENTER;
    if (c == '\t')
        return KEY_TAB;
    if (c == 0x7f || c == 0x08)
        return KEY_BACKSPACE;
    if (c == 0x03)
        return KEY_CTRL_C;
    if (c == 0x04)
        return KEY_CTRL_D;
    if (c == 0x01)
        return KEY_HOME; /* Ctrl-A */
    if (c == 0x05)
        return KEY_END; /* Ctrl-E */
    if (c == 0x0c)
        return KEY_CTRL_L; /* Ctrl-L */

    if (c == 0x1b) {
        /* Check if further escape sequence bytes follow */
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        if (poll(&pfd, 1, 50) <= 0) {
            return KEY_ESC;
        }

        unsigned char seq[8] = {0};
        if (read(fd, &seq[0], 1) <= 0)
            return KEY_ESC;

        if (seq[0] == '[') {
            if (poll(&pfd, 1, 50) <= 0)
                return KEY_ESC;
            if (read(fd, &seq[1], 1) <= 0)
                return KEY_ESC;

            if (seq[1] == 'A')
                return KEY_ARROW_UP;
            if (seq[1] == 'B')
                return KEY_ARROW_DOWN;
            if (seq[1] == 'C')
                return KEY_ARROW_RIGHT;
            if (seq[1] == 'D')
                return KEY_ARROW_LEFT;
            if (seq[1] == 'H')
                return KEY_HOME;
            if (seq[1] == 'F')
                return KEY_END;
            if (seq[1] == 'Z')
                return KEY_SHIFT_TAB;

            if (seq[1] >= '0' && seq[1] <= '9') {
                if (poll(&pfd, 1, 50) > 0) {
                    unsigned char seq2 = 0;
                    if (read(fd, &seq2, 1) > 0 && seq2 == '~') {
                        if (seq[1] == '1' || seq[1] == '7')
                            return KEY_HOME;
                        if (seq[1] == '3')
                            return KEY_DELETE;
                        if (seq[1] == '4' || seq[1] == '8')
                            return KEY_END;
                    }
                }
            }
        } else if (seq[0] == 'O') {
            if (poll(&pfd, 1, 50) <= 0)
                return KEY_ESC;
            if (read(fd, &seq[1], 1) <= 0)
                return KEY_ESC;
            if (seq[1] == 'H')
                return KEY_HOME;
            if (seq[1] == 'F')
                return KEY_END;
        }
        return KEY_ESC;
    }

    if (out_char) {
        *out_char = (char)c;
    }
    return KEY_CHAR;
}

static void candidate_list_init(candidate_list_t *list, size_t replace_from)
{
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
    list->replace_from = replace_from;
}

static void candidate_list_add(candidate_list_t *list, const char *insert_text,
                               const char *display_text, bool is_dir)
{
    if (list->count >= list->cap) {
        size_t new_cap = list->cap == 0 ? 8 : list->cap * 2;
        candidate_item_t *grown =
            (candidate_item_t *)realloc(list->items, new_cap * sizeof(candidate_item_t));
        if (!grown) {
            return;
        }
        list->items = grown;
        list->cap = new_cap;
    }
    list->items[list->count].insert_text = strdup(insert_text);
    list->items[list->count].display_text = strdup(display_text ? display_text : insert_text);
    list->items[list->count].is_dir = is_dir;
    if (list->items[list->count].insert_text && list->items[list->count].display_text) {
        list->count++;
    } else {
        free(list->items[list->count].insert_text);
        free(list->items[list->count].display_text);
    }
}

static void candidate_list_free(candidate_list_t *list)
{
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].insert_text);
        free(list->items[i].display_text);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int candidate_cmp(const void *a, const void *b)
{
    const candidate_item_t *ca = (const candidate_item_t *)a;
    const candidate_item_t *cb = (const candidate_item_t *)b;
    if (ca->is_dir != cb->is_dir) {
        return ca->is_dir ? -1 : 1;
    }
    return strcmp(ca->display_text, cb->display_text);
}

static size_t common_prefix_length(const candidate_list_t *list)
{
    if (!list || list->count == 0) {
        return 0;
    }
    size_t len = 0;
    while (1) {
        char ch = list->items[0].insert_text[len];
        if (ch == '\0') {
            break;
        }
        for (size_t i = 1; i < list->count; i++) {
            if (list->items[i].insert_text[len] != ch) {
                return len;
            }
        }
        len++;
    }
    return len;
}

static void generate_candidates(const char *buf, size_t cursor, candidate_list_t *list)
{
    /* If starts with /ingest, do zsh-style filesystem path autocompletion */
    if (strncmp(buf, "/ingest", 7) == 0 && (buf[7] == ' ' || buf[7] == '\0')) {
        const char *arg = buf + 7;
        while (*arg == ' ') {
            arg++;
        }

        size_t arg_offset = (size_t)(arg - buf);
        candidate_list_init(list, arg_offset);

        char fs_dir[1024];
        char file_prefix[256];
        char disp_prefix[1024];

        fs_dir[0] = '\0';
        file_prefix[0] = '\0';
        disp_prefix[0] = '\0';

        bool has_tilde = (arg[0] == '~' && (arg[1] == '/' || arg[1] == '\0'));
        if (has_tilde) {
            const char *home = getenv("HOME");
            if (!home) {
                home = "/";
            }
            const char *after = arg + 1;
            if (*after == '/') {
                after++;
            }
            const char *last_slash = strrchr(after, '/');
            if (last_slash) {
                size_t sublen = (size_t)(last_slash - after) + 1;
                char sub_dir[512];
                if (sublen >= sizeof(sub_dir)) {
                    sublen = sizeof(sub_dir) - 1;
                }
                strncpy(sub_dir, after, sublen);
                sub_dir[sublen] = '\0';

                snprintf(fs_dir, sizeof(fs_dir), "%s/%s", home, sub_dir);
                snprintf(disp_prefix, sizeof(disp_prefix), "~/%s", sub_dir);
                strncpy(file_prefix, last_slash + 1, sizeof(file_prefix) - 1);
            } else {
                snprintf(fs_dir, sizeof(fs_dir), "%s", home);
                snprintf(disp_prefix, sizeof(disp_prefix), "~/");
                strncpy(file_prefix, after, sizeof(file_prefix) - 1);
            }
        } else {
            const char *last_slash = strrchr(arg, '/');
            if (last_slash) {
                size_t dlen = (size_t)(last_slash - arg) + 1;
                if (dlen >= sizeof(fs_dir)) {
                    dlen = sizeof(fs_dir) - 1;
                }
                strncpy(fs_dir, arg, dlen);
                fs_dir[dlen] = '\0';
                strncpy(disp_prefix, fs_dir, sizeof(disp_prefix) - 1);
                strncpy(file_prefix, last_slash + 1, sizeof(file_prefix) - 1);
            } else {
                snprintf(fs_dir, sizeof(fs_dir), ".");
                disp_prefix[0] = '\0';
                strncpy(file_prefix, arg, sizeof(file_prefix) - 1);
            }
        }
        file_prefix[sizeof(file_prefix) - 1] = '\0';
        disp_prefix[sizeof(disp_prefix) - 1] = '\0';
        fs_dir[sizeof(fs_dir) - 1] = '\0';

        DIR *d = opendir(fs_dir);
        if (d) {
            const struct dirent *de;
            size_t plen = strlen(file_prefix);
            while ((de = readdir(d)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
                    continue;
                }
                if (de->d_name[0] == '.' && file_prefix[0] != '.') {
                    continue;
                }
                if (strncmp(de->d_name, file_prefix, plen) == 0) {
                    char stat_path[2048];
                    snprintf(stat_path, sizeof(stat_path), "%s/%s", fs_dir, de->d_name);
                    struct stat st;
                    bool is_dir = (stat(stat_path, &st) == 0 && S_ISDIR(st.st_mode));

                    char insert_text[2048];
                    snprintf(insert_text, sizeof(insert_text), "%s%s%s", disp_prefix, de->d_name,
                             is_dir ? "/" : " ");

                    char display_text[512];
                    snprintf(display_text, sizeof(display_text), "%s%s", de->d_name,
                             is_dir ? "/" : "");

                    candidate_list_add(list, insert_text, display_text, is_dir);
                    if (list->count >= REPL_MAX_MENU_ITEMS) {
                        break;
                    }
                }
            }
            closedir(d);
            if (list->count > 1) {
                qsort(list->items, list->count, sizeof(candidate_item_t), candidate_cmp);
            }
        }
        return;
    }

    /* Otherwise, slash command completion */
    candidate_list_init(list, 0);

    /* Find prefix up to cursor */
    size_t word_start = 0;
    while (word_start < cursor && isspace((unsigned char)buf[word_start])) {
        word_start++;
    }

    const char *prefix = buf + word_start;
    size_t prefix_len = (cursor > word_start) ? (cursor - word_start) : 0;

    static const char *default_commands[] = {"/help",   "/ingest ", "/stats",
                                             "/config", "/clear",   "/exit"};
    static const size_t num_default = sizeof(default_commands) / sizeof(default_commands[0]);

    if (prefix_len <= 1) {
        /* General Tab / empty / just "/" -> show default commands (includes /exit, excludes /quit)
         */
        for (size_t i = 0; i < num_default; i++) {
            if (prefix_len == 0 || strncmp(default_commands[i], prefix, prefix_len) == 0) {
                candidate_list_add(list, default_commands[i], default_commands[i], false);
            }
        }
    } else {
        /* Specific prefix typed: check all commands including /quit */
        static const char *all_commands[] = {"/help",  "/ingest ", "/stats", "/config",
                                             "/clear", "/exit",    "/quit"};
        static const size_t num_all = sizeof(all_commands) / sizeof(all_commands[0]);

        for (size_t i = 0; i < num_all; i++) {
            if (strncmp(all_commands[i], prefix, prefix_len) == 0) {
                candidate_list_add(list, all_commands[i], all_commands[i], false);
            }
        }
    }
}

static void redraw_prompt_and_line(const char *prompt, const char *buf, size_t len, size_t cursor)
{
    /* Clear line, print prompt and buffer, reposition cursor */
    printf("\r\x1b[K%s", prompt);
    if (len > 0) {
        fwrite(buf, 1, len, stdout);
    }
    if (cursor < len) {
        printf("\x1b[%dD", (int)(len - cursor));
    }
    fflush(stdout);
}

static void clear_menu_display(size_t rows_drawn)
{
    if (rows_drawn == 0) {
        return;
    }
    for (size_t r = 0; r < rows_drawn; r++) {
        printf("\r\n\x1b[K");
    }
    /* Move back up to prompt line */
    printf("\x1b[%dA", (int)rows_drawn);
    fflush(stdout);
}

static size_t render_completion_menu(const candidate_list_t *list, size_t selected,
                                     size_t *out_cols)
{
    if (!list || list->count == 0) {
        if (out_cols) {
            *out_cols = 1;
        }
        return 0;
    }

    int term_w = 80;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 20) {
        term_w = ws.ws_col;
    }

    size_t max_len = 0;
    for (size_t i = 0; i < list->count; i++) {
        size_t l = strlen(list->items[i].display_text);
        if (l > max_len) {
            max_len = l;
        }
    }

    size_t cellw = max_len + 4;
    if (cellw < 12) {
        cellw = 12;
    }
    if ((int)cellw > term_w - 4) {
        cellw = (size_t)(term_w - 4);
    }

    size_t cols = (size_t)term_w / cellw;
    if (cols < 1) {
        cols = 1;
    }
    if (cols > 6) {
        cols = 6;
    }
    if (cols > list->count) {
        cols = list->count;
    }
    if (out_cols) {
        *out_cols = cols;
    }

    size_t rows = (list->count + cols - 1) / cols;
    size_t drawn_rows = 0;

    for (size_t r = 0; r < rows; r++) {
        printf("\r\n\x1b[K");
        drawn_rows++;
        for (size_t c = 0; c < cols; c++) {
            size_t idx = r * cols + c;
            if (idx >= list->count) {
                break;
            }

            const char *disp = list->items[idx].display_text;
            bool is_dir = list->items[idx].is_dir;

            if (idx == selected) {
                /* Selected: Vibrant purple pill highlight */
                printf("\x1b[38;2;18;18;24;48;2;189;147;249;1m %-*s \x1b[0m", (int)(cellw - 2), disp);
            } else {
                /* Unselected: clean gray text (with cyan accent for directories) */
                if (is_dir) {
                    printf("\x1b[38;2;139;190;255m  %-*s\x1b[0m", (int)(cellw - 2), disp);
                } else {
                    printf("\x1b[38;2;140;145;165m  %-*s\x1b[0m", (int)(cellw - 2), disp);
                }
            }
        }
    }

    printf("\r\n\x1b[K\x1b[38;5;244m  (Tab/Arrows: navigate • Enter: select • Esc: cancel)\x1b[0m");
    drawn_rows++;

    /* Move cursor back up to prompt line */
    printf("\x1b[%dA", (int)drawn_rows);
    (void)fflush(stdout);

    return drawn_rows;
}

static size_t menu_move_down(size_t cur, size_t count, size_t cols)
{
    if (count <= 1 || cols == 0) {
        return 0;
    }
    size_t col = cur % cols;
    size_t next = cur + cols;
    if (next < count) {
        return next;
    }
    return col;
}

static size_t menu_move_up(size_t cur, size_t count, size_t cols)
{
    if (count <= 1 || cols == 0) {
        return 0;
    }
    size_t col = cur % cols;
    if (cur >= cols) {
        return cur - cols;
    }
    size_t total_rows = (count + cols - 1) / cols;
    size_t last_row = total_rows - 1;
    size_t target = last_row * cols + col;
    if (target >= count) {
        if (last_row > 0) {
            target = (last_row - 1) * cols + col;
        } else {
            target = cur;
        }
    }
    return target;
}

static void repl_history_push(repl_context_t *repl, const char *line)
{
    if (!repl || !line || line[0] == '\0')
        return;

    /* Don't add duplicate of previous entry */
    if (repl->history_count > 0 && strcmp(repl->history[repl->history_count - 1], line) == 0) {
        return;
    }

    if (repl->history_count >= repl->history_cap) {
        size_t new_cap = repl->history_cap == 0 ? REPL_HISTORY_CAP_INIT : repl->history_cap * 2;
        char **grown = (char **)realloc(repl->history, new_cap * sizeof(char *));
        if (!grown)
            return;
        repl->history = grown;
        repl->history_cap = new_cap;
    }

    repl->history[repl->history_count] = strdup(line);
    if (repl->history[repl->history_count]) {
        repl->history_count++;
    }
}

repl_context_t *repl_init(const char *history_path)
{
    repl_context_t *repl = (repl_context_t *)calloc(1, sizeof(repl_context_t));
    if (!repl)
        return NULL;

    repl->tty_fd = STDIN_FILENO;
    repl->line_cap = REPL_LINE_CAP_INIT;
    repl->line_buf = (char *)malloc(repl->line_cap);
    repl->scratch_buf = (char *)malloc(repl->line_cap);
    if (!repl->line_buf || !repl->scratch_buf) {
        free(repl->line_buf);
        free(repl->scratch_buf);
        free(repl);
        return NULL;
    }
    repl->line_buf[0] = '\0';
    repl->scratch_buf[0] = '\0';
    repl->line_len = 0;
    repl->cursor = 0;

    if (history_path) {
        repl->history_path = strdup(history_path);
    } else {
        const char *home = getenv("HOME");
        char default_path[512];
        if (home) {
            snprintf(default_path, sizeof(default_path), "%s/.local/state/librarian/history", home);
        } else {
            snprintf(default_path, sizeof(default_path), "data/history.txt");
        }
        repl->history_path = strdup(default_path);
    }

    /* Load history from file */
    if (repl->history_path) {
        FILE *fp = fopen(repl->history_path, "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
                    line[--l] = '\0';
                }
                if (l > 0) {
                    repl_history_push(repl, line);
                }
            }
            fclose(fp);
        }
    }

    return repl;
}

void repl_history_add(repl_context_t *repl, const char *line)
{
    if (!repl || !line || line[0] == '\0')
        return;

    /* Don't add duplicate of previous entry */
    if (repl->history_count > 0 && strcmp(repl->history[repl->history_count - 1], line) == 0) {
        return;
    }

    repl_history_push(repl, line);

    /* Append to file */
    if (repl->history_path) {
        char dir_buf[512];
        strncpy(dir_buf, repl->history_path, sizeof(dir_buf) - 1);
        dir_buf[sizeof(dir_buf) - 1] = '\0';
        char *slash = strrchr(dir_buf, '/');
        if (slash) {
            *slash = '\0';
            mkdir(dir_buf, 0755);
        }

        FILE *fp = fopen(repl->history_path, "a");
        if (fp) {
            fprintf(fp, "%s\n", line);
            fclose(fp);
        }
    }
}

char *repl_readline(repl_context_t *repl, const char *prompt)
{
    if (!repl)
        return NULL;

    /* Fallback for non-interactive / piped environments */
    if (!isatty(repl->tty_fd)) {
        printf("%s", prompt);
        fflush(stdout);
        char buf[4096];
        if (!fgets(buf, sizeof(buf), stdin)) {
            return NULL;
        }
        size_t l = strlen(buf);
        while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) {
            buf[--l] = '\0';
        }
        if (repl->line_cap <= l) {
            char *grown = (char *)realloc(repl->line_buf, l + 64);
            if (!grown)
                return NULL;
            repl->line_buf = grown;
            repl->line_cap = l + 64;
        }
        memcpy(repl->line_buf, buf, l + 1);
        repl->line_len = l;
        repl->cursor = l;
        return repl->line_buf;
    }

    if (!enable_raw_mode(repl)) {
        return NULL;
    }

    repl->line_len = 0;
    repl->cursor = 0;
    repl->line_buf[0] = '\0';
    if (repl->scratch_buf) {
        repl->scratch_buf[0] = '\0';
    }

    redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);

    int history_index = -1; /* -1 means working on new/current line */
    bool menu_active = false;
    size_t menu_sel = 0;
    size_t menu_rows = 0;
    size_t menu_cols = 1;
    candidate_list_t candidates;
    candidate_list_init(&candidates, 0);

    while (1) {
        char ch = 0;
        repl_key_t key = read_key(repl->tty_fd, &ch);

        if (menu_active) {
            if (key == KEY_TAB || key == KEY_ARROW_RIGHT) {
                if (candidates.count > 0) {
                    menu_sel = (menu_sel + 1) % candidates.count;
                    clear_menu_display(menu_rows);
                    menu_rows = render_completion_menu(&candidates, menu_sel, &menu_cols);
                    redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
                }
                continue;
            } else if (key == KEY_SHIFT_TAB || key == KEY_ARROW_LEFT) {
                if (candidates.count > 0) {
                    menu_sel = (menu_sel == 0) ? candidates.count - 1 : menu_sel - 1;
                    clear_menu_display(menu_rows);
                    menu_rows = render_completion_menu(&candidates, menu_sel, &menu_cols);
                    redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
                }
                continue;
            } else if (key == KEY_ARROW_DOWN) {
                if (candidates.count > 0) {
                    menu_sel = menu_move_down(menu_sel, candidates.count, menu_cols);
                    clear_menu_display(menu_rows);
                    menu_rows = render_completion_menu(&candidates, menu_sel, &menu_cols);
                    redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
                }
                continue;
            } else if (key == KEY_ARROW_UP) {
                if (candidates.count > 0) {
                    menu_sel = menu_move_up(menu_sel, candidates.count, menu_cols);
                    clear_menu_display(menu_rows);
                    menu_rows = render_completion_menu(&candidates, menu_sel, &menu_cols);
                    redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
                }
                continue;
            } else if (key == KEY_ENTER) {
                /* Accept selected completion */
                if (candidates.count > 0 && menu_sel < candidates.count) {
                    const char *choice = candidates.items[menu_sel].insert_text;
                    size_t from = candidates.replace_from;
                    size_t clen = strlen(choice);

                    size_t needed = from + clen + 1;
                    if (needed >= repl->line_cap) {
                        size_t ncap = (needed + 64) * 2;
                        char *grown = (char *)realloc(repl->line_buf, ncap);
                        if (grown) {
                            repl->line_buf = grown;
                            repl->line_cap = ncap;
                        }
                    }
                    memcpy(repl->line_buf + from, choice, clen);
                    repl->line_len = from + clen;
                    repl->line_buf[repl->line_len] = '\0';
                    repl->cursor = repl->line_len;
                }
                clear_menu_display(menu_rows);
                menu_rows = 0;
                menu_active = false;
                candidate_list_free(&candidates);
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
                continue;
            } else if (key == KEY_ESC || key == KEY_CTRL_C) {
                /* Cancel completion */
                clear_menu_display(menu_rows);
                menu_rows = 0;
                menu_active = false;
                candidate_list_free(&candidates);
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
                continue;
            } else {
                /* Dismiss menu and process key normally */
                clear_menu_display(menu_rows);
                menu_rows = 0;
                menu_active = false;
                candidate_list_free(&candidates);
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            }
        }

        if (key == KEY_ENTER) {
            printf("\r\n");
            fflush(stdout);
            disable_raw_mode(repl);
            return repl->line_buf;
        }

        if (key == KEY_CTRL_D) {
            if (repl->line_len == 0) {
                printf("\r\n");
                fflush(stdout);
                disable_raw_mode(repl);
                return NULL;
            }
            /* Delete character under cursor if not empty */
            key = KEY_DELETE;
        }

        if (key == KEY_CTRL_C) {
            printf("^C\r\n");
            repl->line_len = 0;
            repl->cursor = 0;
            repl->line_buf[0] = '\0';
            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            history_index = -1;
            continue;
        }

        if (key == KEY_CTRL_L) {
            printf("\x1b[2J\x1b[H");
            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            continue;
        }

        if (key == KEY_ARROW_LEFT) {
            if (repl->cursor > 0) {
                repl->cursor--;
                printf("\x1b[D");
                fflush(stdout);
            }
            continue;
        }

        if (key == KEY_ARROW_RIGHT) {
            if (repl->cursor < repl->line_len) {
                repl->cursor++;
                printf("\x1b[C");
                fflush(stdout);
            }
            continue;
        }

        if (key == KEY_HOME) {
            repl->cursor = 0;
            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            continue;
        }

        if (key == KEY_END) {
            repl->cursor = repl->line_len;
            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            continue;
        }

        if (key == KEY_BACKSPACE) {
            if (repl->cursor > 0) {
                memmove(repl->line_buf + repl->cursor - 1, repl->line_buf + repl->cursor,
                        repl->line_len - repl->cursor + 1);
                repl->cursor--;
                repl->line_len--;
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            }
            continue;
        }

        if (key == KEY_DELETE) {
            if (repl->cursor < repl->line_len) {
                memmove(repl->line_buf + repl->cursor, repl->line_buf + repl->cursor + 1,
                        repl->line_len - repl->cursor);
                repl->line_len--;
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            }
            continue;
        }

        /* History navigation */
        if (key == KEY_ARROW_UP) {
            if (repl->history_count == 0)
                continue;

            if (history_index == -1) {
                /* Save current input to scratch buffer */
                strncpy(repl->scratch_buf, repl->line_buf, repl->line_cap - 1);
                history_index = (int)repl->history_count - 1;
            } else if (history_index > 0) {
                history_index--;
            }

            const char *entry = repl->history[history_index];
            size_t elen = strlen(entry);
            if (elen >= repl->line_cap) {
                char *grown = (char *)realloc(repl->line_buf, elen + 64);
                if (grown) {
                    repl->line_buf = grown;
                    repl->line_cap = elen + 64;
                }
            }
            memcpy(repl->line_buf, entry, elen + 1);
            repl->line_len = elen;
            repl->cursor = elen;
            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            continue;
        }

        if (key == KEY_ARROW_DOWN) {
            if (history_index == -1) {
                continue;
            }

            if (history_index < (int)repl->history_count - 1) {
                history_index++;
                const char *entry = repl->history[history_index];
                size_t elen = strlen(entry);
                if (elen >= repl->line_cap) {
                    size_t ncap = elen + 64;
                    char *grown = (char *)realloc(repl->line_buf, ncap);
                    if (grown) {
                        repl->line_buf = grown;
                        repl->line_cap = ncap;
                    }
                }
                memcpy(repl->line_buf, entry, elen + 1);
                repl->line_len = elen;
                repl->cursor = elen;
            } else {
                history_index = -1;
                size_t slen = strlen(repl->scratch_buf);
                memcpy(repl->line_buf, repl->scratch_buf, slen + 1);
                repl->line_len = slen;
                repl->cursor = slen;
            }
            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            continue;
        }

        /* Tab Completion Trigger */
        if (key == KEY_TAB) {
            generate_candidates(repl->line_buf, repl->cursor, &candidates);
            if (candidates.count == 0) {
                /* No match */
                printf("\a");
                (void)fflush(stdout);
            } else if (candidates.count == 1) {
                /* Single match -> insert directly */
                const char *choice = candidates.items[0].insert_text;
                size_t from = candidates.replace_from;
                size_t clen = strlen(choice);

                size_t needed = from + clen + 1;
                if (needed >= repl->line_cap) {
                    size_t ncap = (needed + 64) * 2;
                    char *grown = (char *)realloc(repl->line_buf, ncap);
                    if (grown) {
                        repl->line_buf = grown;
                        repl->line_cap = ncap;
                    }
                }
                memcpy(repl->line_buf + from, choice, clen);
                repl->line_len = from + clen;
                repl->line_buf[repl->line_len] = '\0';
                repl->cursor = repl->line_len;
                candidate_list_free(&candidates);
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            } else {
                /* Multiple matches -> expand common prefix if any */
                size_t common_len = common_prefix_length(&candidates);
                size_t from = candidates.replace_from;
                size_t typed_len = (repl->cursor > from) ? (repl->cursor - from) : 0;

                if (common_len > typed_len) {
                    size_t needed = from + common_len + 1;
                    if (needed >= repl->line_cap) {
                        size_t ncap = (needed + 64) * 2;
                        char *grown = (char *)realloc(repl->line_buf, ncap);
                        if (grown) {
                            repl->line_buf = grown;
                            repl->line_cap = ncap;
                        }
                    }
                    memcpy(repl->line_buf + from, candidates.items[0].insert_text, common_len);
                    repl->line_len = from + common_len;
                    repl->line_buf[repl->line_len] = '\0';
                    repl->cursor = repl->line_len;
                }

                /* Open completion menu */
                menu_active = true;
                menu_sel = 0;
                menu_cols = 1;
                menu_rows = render_completion_menu(&candidates, menu_sel, &menu_cols);
                redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
            }
            continue;
        }

        /* Regular printable character insertion */
        if (key == KEY_CHAR) {
            if (repl->line_len + 2 >= repl->line_cap) {
                size_t new_cap = repl->line_cap * 2;
                char *grown = (char *)realloc(repl->line_buf, new_cap);
                if (!grown) {
                    continue;
                }
                repl->line_buf = grown;
                repl->line_cap = new_cap;
            }

            if (repl->cursor < repl->line_len) {
                memmove(repl->line_buf + repl->cursor + 1, repl->line_buf + repl->cursor,
                        repl->line_len - repl->cursor);
            }
            repl->line_buf[repl->cursor] = ch;
            repl->cursor++;
            repl->line_len++;
            repl->line_buf[repl->line_len] = '\0';

            redraw_prompt_and_line(prompt, repl->line_buf, repl->line_len, repl->cursor);
        }
    }
}

size_t repl_history_count(const repl_context_t *repl)
{
    return repl ? repl->history_count : 0;
}

void repl_free(repl_context_t *repl)
{
    if (!repl) {
        return;
    }
    disable_raw_mode(repl);

    free(repl->history_path);
    if (repl->history) {
        for (size_t i = 0; i < repl->history_count; i++) {
            free(repl->history[i]);
        }
        free((void *)repl->history);
    }
    free(repl->line_buf);
    free(repl->scratch_buf);
    free(repl);
}
