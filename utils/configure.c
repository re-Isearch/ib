#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
# include <sys/utsname.h>
#endif

#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)

static void write_string_define(
    FILE *file,
    const char *name,
    const char *value)
{
    const unsigned char *p;

    fprintf(file, "#define %s \"", name);

    for (p = (const unsigned char *)value; *p != '\0'; ++p)
    {
        switch (*p)
        {
        case '\\':
            fputs("\\\\", file);
            break;

        case '"':
            fputs("\\\"", file);
            break;

        case '\n':
            fputs("\\n", file);
            break;

        case '\r':
            fputs("\\r", file);
            break;

        case '\t':
            fputs("\\t", file);
            break;

        default:
            if (*p < 0x20 || *p == 0x7f)
                fprintf(file, "\\x%02X", (unsigned int)*p);
            else
                fputc(*p, file);
            break;
        }
    }

    fputs("\"\n", file);
}

static const char *detected_compiler(void)
{
#if defined(__clang__)
    return __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(_MSC_VER)
    return "Microsoft C/C++ "
           STRINGIFY(_MSC_VER);
#else
    return "Unknown compiler";
#endif
}

static void detected_platform(char *buffer, size_t buffer_size)
{
#if defined(__unix__) || defined(__APPLE__)
    struct utsname info;

    if (uname(&info) == 0)
    {
        /*
         * Match the historical `uname -msr` order:
         * machine, system name, release.
         */
        snprintf(
            buffer,
            buffer_size,
            "%s %s %s",
            info.machine,
            info.sysname,
            info.release);
        return;
    }
#endif

#if defined(_WIN32)
    snprintf(buffer, buffer_size, "Windows");
#else
    snprintf(buffer, buffer_size, "Unknown platform");
#endif
}


int main(int argc, char **argv)
{
    const char *output_name = argc > 1 ? argv[1] : "../src/conf.h.inc";

    const char *platform_argument = argc > 2 && argv[2][0] != '\0' ? argv[2] : NULL;

    const char *compiler_argument = argc > 3 && argv[3][0] != '\0' ? argv[3] : NULL;

    char platform_buffer[512];
    FILE *f = fopen(output_name, "w");

    if (f == NULL)
    {
        perror(output_name);
        return 1;
    }

    detected_platform(platform_buffer, sizeof(platform_buffer));

    fprintf(
        f,
        "#ifndef CONF_H_INC\n"
        "#define CONF_H_INC\n\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n\n");

    fprintf(f, "#define SIZEOF_CHAR %u\n",
            (unsigned int)sizeof(signed char));
    fprintf(f, "#define SIZEOF_UCHAR %u\n",
            (unsigned int)sizeof(unsigned char));

    fprintf(f, "#define SIZEOF_SHORT_INT %u\n",
            (unsigned int)sizeof(short int));
    fprintf(f, "#define SIZEOF_INT %u\n",
            (unsigned int)sizeof(int));
    fprintf(f, "#define SIZEOF_LONG_INT %u\n",
            (unsigned int)sizeof(long int));
    fprintf(f, "#define SIZEOF_LONG_LONG_INT %u\n",
            (unsigned int)sizeof(long long int));

    fprintf(f, "#define SIZEOF_FLOAT %u\n",
            (unsigned int)sizeof(float));
    fprintf(f, "#define SIZEOF_DOUBLE %u\n",
            (unsigned int)sizeof(double));
    fprintf(f, "#define SIZEOF_LONG_DOUBLE %u\n\n",
            (unsigned int)sizeof(long double));

    {
        const uint16_t value = 1;

        if (*(const unsigned char *)&value == 0)
            fprintf(f, "#define IS_BIG_ENDIAN 1\n");
        else
            fprintf(f, "#define IS_LITTLE_ENDIAN 1\n");
    }

    fputc('\n', f);

    write_string_define(
        f,
        "TARGET_PLATFORM",
        platform_argument != NULL
            ? platform_argument
            : platform_buffer);

    write_string_define(
        f,
        "TARGET_COMPILER",
        compiler_argument != NULL
            ? compiler_argument
            : detected_compiler());

    /*
     * Compatibility with existing source code using the historical names.
     * For native builds, host and target describe the same platform.
     */
    fprintf(
        f,
        "#define HOST_PLATFORM TARGET_PLATFORM\n"
        "#define HOST_COMPILER TARGET_COMPILER\n\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n\n"
        "#endif /* CONF_H_INC */\n");

    if (fclose(f) != 0)
    {
        perror(output_name);
        return 1;
    }

    return 0;
}
