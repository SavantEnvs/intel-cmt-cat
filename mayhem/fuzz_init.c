/*
 * Linked into every libFuzzer harness (and its -standalone twin, whose driver calls it too).
 *
 * The library is built with -DDEBUG so its ASSERTs are checked; one of them, in log_message(),
 * requires log_init() to have run before anything logs, which pqos_init() does first thing. The
 * harnesses enter below pqos_init(), so they install the log here, silent, once. Nothing an input
 * does changes it.
 */
#include "log.h"

#include <stdlib.h>

int
LLVMFuzzerInitialize(int *argc, char ***argv)
{
        (void)argc;
        (void)argv;
        if (log_init(-1, NULL, NULL, LOG_VER_SILENT) != LOG_RETVAL_OK)
                abort();
        return 0;
}
