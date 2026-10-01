#ifndef LIBRARIAN_REPL_H
#define LIBRARIAN_REPL_H

#include <stdbool.h>
#include <stddef.h>

typedef struct repl_context repl_context_t;

/* Initialize REPL context with optional history file path */
repl_context_t *repl_init(const char *history_path);

/* Read a single line with interactive arrow navigation, history, and tab completion.
 * Returns NULL on EOF (e.g. Ctrl-D on empty line).
 * Returned pointer is owned by repl_context and valid until next call or repl_free. */
char *repl_readline(repl_context_t *repl, const char *prompt);

/* Add a line to in-memory history and append to history file */
void repl_history_add(repl_context_t *repl, const char *line);

/* Return the number of items currently in history */
size_t repl_history_count(const repl_context_t *repl);

/* Free REPL context and restore terminal state if needed */
void repl_free(repl_context_t *repl);

#endif /* LIBRARIAN_REPL_H */
