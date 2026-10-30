/* test_args.c --- elib_cli_argc / elib_cli_argv contract */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "elib_cli.h"

static void test_argc_basic(void)
{
    printf("Test: argc counts space separated tokens... ");
    assert(elib_cli_argc(NULL) == 0u);
    assert(elib_cli_argc("") == 0u);
    assert(elib_cli_argc(" ") == 0u);
    assert(elib_cli_argc("    ") == 0u);
    assert(elib_cli_argc("a") == 1u);
    assert(elib_cli_argc("ab") == 1u);
    assert(elib_cli_argc("a b") == 2u);
    assert(elib_cli_argc("a b c") == 3u);
    printf("PASSED\n");
}

static void test_argc_ignores_runs(void)
{
    printf("Test: runs of spaces do not create empty arguments... ");
    assert(elib_cli_argc("  a   b  ") == 2u);
    assert(elib_cli_argc(" a  bb  ccc ") == 3u);
    assert(elib_cli_argc("a   ") == 1u);
    assert(elib_cli_argc("   a") == 1u);
    printf("PASSED\n");
}

static void test_argv_positions(void)
{
    printf("Test: argv locates each token by index... ");
    uint32_t len = 0u;
    const char *p;

    p = elib_cli_argv("a b c", 0u, &len);
    assert(p != NULL);
    assert(len == 1u);
    assert(p[0] == 'a');
    assert(p[1] == ' ');                 /* not NUL terminated */

    p = elib_cli_argv("a b c", 1u, &len);
    assert(p != NULL);
    assert(len == 1u);
    assert(p[0] == 'b');

    p = elib_cli_argv("a b c", 2u, &len);
    assert(p != NULL);
    assert(len == 1u);
    assert(p[0] == 'c');
    assert(p[1] == '\0');                /* end of string terminates it */
    printf("PASSED\n");
}

static void test_argv_skips_spaces(void)
{
    printf("Test: argv skips leading and repeated spaces... ");
    uint32_t len = 0u;
    const char *p;

    p = elib_cli_argv("  a  bbb ", 0u, &len);
    assert(p != NULL);
    assert(len == 1u);
    assert(p[0] == 'a');

    p = elib_cli_argv("  a  bbb ", 1u, &len);
    assert(p != NULL);
    assert(len == 3u);
    assert(p[0] == 'b');
    assert(p[1] == 'b');
    assert(p[2] == 'b');
    assert(p[3] == ' ');

    p = elib_cli_argv(" a  bb  ccc ", 2u, &len);
    assert(p != NULL);
    assert(len == 3u);
    assert(strncmp(p, "ccc", 3u) == 0);
    printf("PASSED\n");
}

static void test_argv_out_of_range(void)
{
    printf("Test: argv returns NULL past the last argument and zeroes len... ");
    uint32_t len = 99u;

    assert(elib_cli_argv("a b", 2u, &len) == NULL);
    assert(len == 0u);

    len = 99u;
    assert(elib_cli_argv("a", 1u, &len) == NULL);
    assert(len == 0u);

    len = 99u;
    assert(elib_cli_argv("   ", 0u, &len) == NULL);
    assert(len == 0u);

    len = 99u;
    assert(elib_cli_argv("", 0u, &len) == NULL);
    assert(len == 0u);
    printf("PASSED\n");
}

static void test_argv_null_input(void)
{
    printf("Test: argv(NULL) is NULL and safe... ");
    uint32_t len = 99u;

    assert(elib_cli_argv(NULL, 0u, &len) == NULL);
    assert(len == 0u);
    assert(elib_cli_argv(NULL, 5u, NULL) == NULL);
    assert(elib_cli_argv("a b", 1u, NULL) != NULL);   /* len is optional */
    printf("PASSED\n");
}

static void test_argv_matches_argc(void)
{
    printf("Test: argv agrees with argc for every index... ");
    static const char *samples[] = {
        "", " ", "   ", "a", "a b", "  a   b  ", "a b c", " a  bb  ccc "
    };
    size_t s;

    for (s = 0u; s < sizeof(samples) / sizeof(samples[0]); s++) {
        uint32_t count = elib_cli_argc(samples[s]);
        uint32_t i;

        for (i = 0u; i < count; i++) {
            uint32_t len = 0u;

            assert(elib_cli_argv(samples[s], i, &len) != NULL);
            assert(len > 0u);
        }
        assert(elib_cli_argv(samples[s], count, NULL) == NULL);
    }
    printf("PASSED\n");
}

static void test_argv_single_long_token(void)
{
    printf("Test: a single long token keeps its exact length... ");
    uint32_t len = 0u;
    const char *p = elib_cli_argv("abcdefghij", 0u, &len);

    assert(p != NULL);
    assert(len == 10u);
    assert(p[9] == 'j');
    assert(p[10] == '\0');
    printf("PASSED\n");
}

int main(void)
{
    printf("=== test_args ===\n");
    test_argc_basic();
    test_argc_ignores_runs();
    test_argv_positions();
    test_argv_skips_spaces();
    test_argv_out_of_range();
    test_argv_null_input();
    test_argv_matches_argc();
    test_argv_single_long_token();
    printf("=== test_args: all PASSED ===\n");
    return 0;
}
