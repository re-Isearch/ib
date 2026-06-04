#include <stdio.h>
#include <string.h>
#include <strings.h> // Required for strcasecmp on POSIX systems

int _Iindex_main(int argc, char *argv[]);
int _Isearch_main(int argc, char *argv[]);
int _Iutil_main(int argc, char *argv[]);


static const char *_prog(const char *name) {
  char *tcp = strrchr(name, '/');
  if (tcp) return ++tcp;
  return name;
}



int main(int argc, char *argv[]) {
    const char *prognam = _prog(argv[0]);

    // 1. Handle global help or no arguments
    if (argc < 2 || strcasecmp(argv[1], "help") == 0) {
        printf("\
CoreQuarry CLI:\n\
Global Usage: %s [index|search|util] [args]\n\
 index  -> Data Preparation: Building structures, indexing documents/ingestion.\n\
 search -> Query Processing: Searching through the indices and returning results.\n\
 util   -> Maintenance/Admin: Database metadata, Index optimization, configuration management\n\
Type '%s [command] help' for specific module instructions.\n", prognam, prognam);
        return 0;
    }

    char *command = argv[1];

    if (strstr("index", prognam)) command = "index";
    else if (strstr("search", prognam)) command = "search";
    else if (strstr("util", prognam)) command = "util";
    
    /* Shift logic:
       sub_argv[0] becomes the command name ("index")
       sub_argc is the count starting from that command
    */
    int sub_argc = argc - 1;
    char **sub_argv = &argv[1];

    // 2. The "Help Trap"
    // If the word following the command is "help", force argc to 1
    if (argc >= 3 && strcasecmp(argv[2], "help") == 0) {
        sub_argc = 1; 
    }

    // 3. Dispatching
    if (strcmp(command, "index") == 0) {
        return _Iindex_main(sub_argc, sub_argv);
    } 
    else if (strcmp(command, "search") == 0) {
        return _Isearch_main(sub_argc, sub_argv);
    } 
    else if (strcmp(command, "util") == 0) {
        return _Iutil_main(sub_argc, sub_argv);
    }

    fprintf(stderr, "Error: Unknown command '%s'\n", command);
    return 1;
}
