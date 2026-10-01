#include <stdio.h>
#include <stdlib.h>
#include "sqlite3.h"
#include "sqlite-vec.h"
#include "toml.h"
#include "llama.h"

int main(void)
{
    printf("Librarian build test...\n");
    printf("  • SQLite3 version: %s\n", sqlite3_libversion());

    int rc = sqlite3_auto_extension((void (*)(void))sqlite3_vec_init);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to register sqlite-vec auto extension\n");
        return 1;
    }
    printf("  • sqlite-vec auto extension registered: OK\n");

    llama_backend_init();
    printf("  • llama.cpp backend initialized: OK\n");
    llama_backend_free();

    printf("✨ All vendor subsystems linked successfully!\n");
    return 0;
}
