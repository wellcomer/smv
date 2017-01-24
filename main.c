/*
  Copyright 2016 Sergey V. Katunin

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <argp.h>
#include <stdbool.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <locale.h>
#include <fnmatch.h>
#include "tinydir.h"

// Default values
#define DEFAULT_DELIMITER "%"
#define OFFSET_DELIMITER  ","
#define OFFSET_START_COUNT_FROM 1

// Internal error codes
#define ERR_RENAME_FAILED -1
#define ERR_HELPER_FAILED  -1

// Exit error codes
#define EXIT_ERR_NOT_FOUND 1
#define EXIT_ERR_HELPER_FAILED 2
#define EXIT_ERR_BAD_PATTERN 3
#define EXIT_ERR_RENAME_FAILED 4

#define VARTABLE_SIZE 100
#define HELPER_OUTPUT_BYTES_MAX 65536

const char *argp_program_version = "0.9";
const char *argp_program_bug_address = "<sergey.blaster@gmail.com>";

static char docs[] = "Smart MV - mv with helpers.\n\nOptions:";
static char args_docs[] = "[SOURCE_PATTERN] [DESTINATION_PATTERN]";

static struct argp_option ops[] = {
    { "delimiter", 'd', "character", 0, "A delimiter character of the DESTINATION_PATTERN (default is %)"},
    { "dry-run", 'n', 0, 0, "Perform a trial run with no changes made"},
    { "helper", 'h', "\"name args\"", 0, "The helper name and arguments"},
    { "ignore-case", 'i', 0, 0, "Case insensitive match for the SOURCE_PATTERN"},
    { "make-path", 'p', 0, 0, "Make path for output file"},
    { "mv-flags", 'm', "\"flags\"", 0, "mv flags"},
    { "quiet", 'q', 0, 0, "Be quiet (no output)"},
    { "verbose", 'v', 0, 0, "Be verbose"},
    { 0 }
};

struct arguments {
    char *delimiter;
    bool dry_run;
    char *helper;
    bool ignore_case;
    bool make_path;
    char *mv_flags;
    bool quiet_mode;
    bool verbose;
    char *args[2];
};

struct offset {
    size_t start;
    size_t length;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct arguments *arguments = state->input;

    switch (key) {
        case 'd':
            arguments->delimiter = arg;
            break;
        case 'n':
            arguments->dry_run = true;
            break;
        case 'h':
            arguments->helper = arg;
            break;
        case 'i':
            arguments->ignore_case = true;
            break;
        case 'm':
            arguments->mv_flags = arg;
            break;
        case 'p':
            arguments->make_path = true;
            break;
        case 'q':
            arguments->quiet_mode = true;
            break;
        case 'v':
            arguments->verbose = true;
            break;
        case ARGP_KEY_ARG:
            if (state->arg_num >= 2){
                argp_usage(state);
            }
            arguments->args[state->arg_num] = arg;
            break;
        case ARGP_KEY_END:
            if (state->arg_num < 2){
                argp_usage (state);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { ops, parse_opt, args_docs, docs, 0, 0, 0 };

static char filterOutput[HELPER_OUTPUT_BYTES_MAX]; // Buffer for RunHelper
static char *varTable[VARTABLE_SIZE]; // Table of DEST_PATTERN's variables (%0%..%N%)
static int varTableItemsCount;

static bool verbose;

// http://stackoverflow.com/questions/7298059/how-to-count-characters-in-a-unicode-string-in-c
// returns the number of utf8 code points in the buffer at s

size_t utf8len(char *s)
{
    size_t len = 0;
    for (; *s; ++s) if ((*s & 0xC0) != 0x80) ++len;
    return len;
}

// returns a pointer to the beginning of the pos'th utf8 codepoint
// in the buffer at s

char *utf8index(char *s, size_t pos)
{
    ++pos;
    for (; *s; ++s) {
        if ((*s & 0xC0) != 0x80) --pos;
        if (pos == 0) return s;
    }
    return NULL;
}

// converts codepoint indexes start and end to byte offsets in the buffer at s

void utf8slice(char *s, size_t *start, size_t *end)
{
    char *p = utf8index(s, *start);
    *start = p ? p - s : 0;
    p = utf8index(s, *end);
    *end = p ? p - s : strlen(s);
}

// https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950
// mkdir -p

int mkdir_p(const char *path)
{
    /* Adapted from http://stackoverflow.com/a/2336245/119527 */
    const size_t len = strlen(path);
    char _path[PATH_MAX];
    char *p;

    errno = 0;

    /* Copy string so its mutable */
    if (len > sizeof(_path)-1) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(_path, path);

    /* Iterate the string */
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            /* Temporarily truncate */
            *p = '\0';

            if (mkdir(_path, S_IRWXU|S_IRWXG|S_IRWXO) != 0) {
                if (errno != EEXIST)
                    return -1;
            }

            *p = '/';
        }
    }

    if (mkdir(_path, S_IRWXU|S_IRWXG|S_IRWXO) != 0) {
        if (errno != EEXIST)
            return -1;
    }

    return 0;
}

void debug (const char *format, ...)
{
    if (!verbose)
        return;

    va_list argptr;
    va_start(argptr, format);
    vfprintf(stdout, format, argptr);
    va_end(argptr);
}

// Check for a match of a fileName

bool MatchPattern (char *pattern, char *fileName, bool ignoreCase)
{
    int flags = FNM_EXTMATCH;

    if (ignoreCase)
        flags |= FNM_CASEFOLD;

    if (fnmatch(pattern, fileName, flags)==0)
        return true;

    return false;
}

// Split by space
// returns tokens count

int Split (char *str, char *arr[], int maxCount)
{
    free(arr[0]);
    memset(arr,0,sizeof(*arr));

    arr[0] = strdup(str); // %@%
    char *token = strtok(str, " "); // split by space

    debug("\nSplit.token[@]: %s\n", arr[0]);

    int tokenCount = 1;

    while (token != NULL && tokenCount < maxCount) {
        debug("Split.token[%d]: %s\n", tokenCount, token);
        arr[tokenCount++] = token;  // %1..%N
        token = strtok (NULL, " ");
    }
    return tokenCount;
}

// Escape quote char (") inside the string with (\")
// returns a new allocated string

char* EscapeChars (char *str)
{
    if (str == NULL)
        return NULL;

    size_t newstrlen = strlen(str) + 3;

    char *newstr = realloc(NULL, newstrlen);

    if (newstr == NULL)
        return NULL;

    newstr[0] = '"';

    int i, j = 1;

    for (i = 0; str[i] != '\0'; i++){
        if (str[i] == '"') {
            newstr = realloc(newstr, ++newstrlen);
            if (newstr == NULL)
                return NULL;
            newstr[j++] = '\\';
        }
        newstr[j] = str[i];
        j++;
    }
    newstr[j++] = '"';
    newstr[j] = 0;

    return newstr;
}

// Run --helper program

int RunHelper(char *helper, char *filePath, char *helperOutput)
{
    helperOutput[0] = 0;

    if (helper == NULL)
        return 0;

    char *helperCmdLine = malloc(strlen(helper) + strlen(filePath) + 4);

    if (helperCmdLine == NULL)
        return ERR_HELPER_FAILED;

    char *filePathEsc = EscapeChars(filePath);

    if (filePathEsc == NULL)
        return ERR_HELPER_FAILED;

    sprintf(helperCmdLine, "%s %s", helper, filePathEsc);

    debug ("\nRunHelper.helperCmdLine: %s\n", helperCmdLine);

    fflush(NULL);
    FILE *fp = popen(helperCmdLine, "r");

    free(helperCmdLine);
    free(filePathEsc);

    if (fp == NULL){
        perror("RunHelper open failed");
        return ERR_HELPER_FAILED;
    }

    if (fgets(helperOutput, HELPER_OUTPUT_BYTES_MAX, fp))
        helperOutput = strtok(helperOutput,"\n");

    int status = pclose(fp);

    debug("RunHelper.status: %d\n", WEXITSTATUS(status));

    if (status != 0)
        return ERR_HELPER_FAILED;

    int helperOutputLength = (int)strlen(helperOutput);

    debug("RunHelper.helperOutput: %s\nRunHelper.helperOutputLength: %d\n", helperOutput, helperOutputLength);

    return helperOutputLength;
}

// Run stock mv

int RunMV (char *flags, char *oldPath, char *newPath)
{
    if (oldPath == NULL || newPath == NULL)
        return -1;

    if (flags == NULL)
        flags = "";

    char *mvCmdLine = malloc(strlen(oldPath) + strlen(newPath) + strlen(flags) + 10);

    if (mvCmdLine == NULL)
        return -1;

    char *args[2];

    args[0] = EscapeChars(oldPath);
    args[1] = EscapeChars(newPath);

    if (args[0] == NULL || args[1] == NULL)
        return -1;

    sprintf(mvCmdLine, "mv %s %s %s", flags, args[0], args[1]); // 10 add. symbols here: mv+3spaces+4quotes+zero char

    debug ("\nRunMV.mvCmdLine: %s\n", mvCmdLine);

    FILE *fp = popen(mvCmdLine, "r");

    free(mvCmdLine);
    free(args[0]);
    free(args[1]);

    if (fp == NULL){
        perror("RunMV open failed");
        return -1;
    }

    int status = WEXITSTATUS(pclose(fp));

    debug("RunMV.status: %d\n", status);

    return status;
}

// Find offset in the pattern token

bool FindOffset (char *str, size_t startPosition, struct offset *offt)
{
    memset(offt, 0, sizeof(struct offset));

    char *token = strstr(str+startPosition, OFFSET_DELIMITER);

    debug("FindOffset.str: %s\n", token);

    if (token == NULL)
        return false;

    offt->start = (size_t) atoi(token + 1);
    debug("FindOffset.start: %d\n", offt->start);

    token = strstr (token+1, OFFSET_DELIMITER);

    if (token != NULL){
        offt->length = (size_t) atoi(token + 1);
        debug("FindOffset.length: %d\n", offt->length);
    }
    else
        debug("FindOffset.length: 0!\n");

    return true;
}

// Fill the DESTINATION_PATTERN with items from varTable

char *FillPattern (char *pattern, char *delimiter, char *varTable[], char *fileName, char *fileExtension, int varTableItemsCount)
{
    char *pattern_copy = strdup(pattern);

    if (pattern_copy == NULL)
        return NULL;

    char *token = strtok(pattern_copy, delimiter);
    char *output = NULL;

    int itemNumber = 1, varTableIndex;
    struct offset off;

    size_t outputLength = 1;

    debug("\nFillPattern.pattern: %s\nFillPattern.fileName: %s\nFillPattern.fileExtension: %s\nFillPattern.varTableItemsCount: %d\n", pattern, fileName, fileExtension, varTableItemsCount);

    while (token != NULL) {

        debug("\nFillPattern.itemNumber: %d\n", itemNumber);
        debug("FillPattern.item: %s\n", token);

        bool offsetPresent = false;

        if (strncmp(token, "@", 1)==0){
            offsetPresent = FindOffset(token, 1, &off);
            token = varTable[0];
        }
        else if (strncmp(token, "#", 1)==0){
            offsetPresent = FindOffset(token, 1, &off);
            token = varTable[varTableItemsCount - 1];
        }
        else if (strncmp(token, "0", 1)==0){
            offsetPresent = FindOffset(token, 1, &off);
            token = fileName;
        }
        else if (strncmp(token, "$", 1)==0){
            offsetPresent = FindOffset(token, 1, &off);
            token = (fileExtension != NULL) ? fileExtension : "";
        }
        else {
            size_t digitWidth = 0;
            if (isdigit(token[0]))
                digitWidth++;
            if (digitWidth && isdigit(token[1]))
                digitWidth++;
            if (digitWidth){
                varTableIndex = (int)strtol(token, NULL, 10);
                if (varTableIndex != 0 && (varTableIndex < varTableItemsCount)){
                    offsetPresent = FindOffset(token, digitWidth, &off);
                    token = varTable[varTableIndex];
                }
            }
        }

        debug("FillPattern.token: %s\n", token);

        size_t tokenCharsCount = utf8len(token); // utf8 chars count

        if (offsetPresent){

            if (off.start < OFFSET_START_COUNT_FROM || off.length == 0 || off.start >= tokenCharsCount + OFFSET_START_COUNT_FROM){ // offset or length is out of range
                debug("FillPattern.error: offset is out of range!\n");
                debug("FillPattern.OFFSET_START_COUNT_FROM: %d\nFillPattern.tokenCharsCount: %d\n", OFFSET_START_COUNT_FROM, tokenCharsCount);
                free(pattern_copy);
                return NULL;
            }
            if (off.length > tokenCharsCount - off.start)
                off.length = tokenCharsCount - off.start + OFFSET_START_COUNT_FROM; // offset length autocorrection
        }

        size_t start = off.start - OFFSET_START_COUNT_FROM;
        size_t end = start + off.length;

        utf8slice(token, &start, &end);

        size_t tokenLength = end - start; // bytes count

        if (tokenLength == 0) // non utf8 token
            tokenLength = strlen(token); // fix it length

        outputLength += tokenLength;

        debug("FillPattern.tokenLength: %d\n", tokenLength);

        if (output == NULL){
            output = malloc(outputLength);
            memset(output, 0, outputLength);
        }
        else
            output = realloc (output, outputLength);

        if (offsetPresent){
            strncat(output, token+start, tokenLength);
            debug("FillPattern.token+offset: %s\n", output+outputLength-tokenLength-1);
        }
        else
            strcat(output, token);

        debug("FillPattern.output: %s\n", output);
        debug("FillPattern.outputLength: %d\n", outputLength-1);

        token = strtok (NULL, delimiter);
        itemNumber++;
    }

    free(pattern_copy);
    return output;
}

// Magic is here

int Rename(char *mvFlags, char *oldPath, char *newPath, bool dryRun, bool makePath, bool quietMode)
{
    char *dirComponent, *fileComponent, *dirName, *fileName;
    char *newPath2 = newPath;
    int exit_code = 0;

    debug("\n");

    if (makePath && !dryRun){

        dirComponent = strdup(newPath);

        dirName = dirname(dirComponent);
        debug("Rename.dirName: %s\n", dirName);

        exit_code = mkdir_p(dirName);
        debug("Rename.mkdir_p: %d\n", exit_code);

        free(dirComponent);

        if (exit_code != 0)
            return ERR_RENAME_FAILED;
    }

    struct stat sb;

    if (stat(newPath, &sb) == 0 && S_ISDIR(sb.st_mode)){

        fileComponent = strdup(oldPath);
        fileName = basename(fileComponent);

        newPath2 = malloc(strlen(newPath)+1+strlen(fileName)+1);

        if (newPath[strlen(newPath)-1] == '/')
            sprintf(newPath2, "%s%s", newPath, fileName);
        else // add trailing slash to the newPath
            sprintf(newPath2, "%s/%s", newPath, fileName);

        free(fileComponent);
    }

    debug("Rename.mvFlags: %s\nRename.oldPath: %s\nRename.newPath: %s\n", mvFlags, oldPath, newPath2);

    if (!quietMode && !verbose)
        printf("%s >> %s\n", oldPath, newPath2);

    if (!dryRun)
        exit_code = RunMV(mvFlags, oldPath, newPath2);

    if (newPath2 != newPath)
        free(newPath2);

    if (exit_code != 0)
        exit_code = ERR_RENAME_FAILED;

    return exit_code;
}


int main(int argc, char *argv[])
{
    int exit_status = EXIT_ERR_NOT_FOUND;

    setlocale (LC_ALL, "");

    struct arguments arguments;

    // default values
    arguments.delimiter = DEFAULT_DELIMITER;
    arguments.ignore_case = false;
    arguments.make_path = false;
    arguments.dry_run = false;
    arguments.quiet_mode = false;

    argp_parse(&argp, argc, argv, 0, 0, &arguments);

    if (arguments.verbose)
        verbose = true; // set global flag for debug function

    char *srcPattern = arguments.args[0];
    char *dstPattern = arguments.args[1];

    debug ("\nmain.argp_program_version: %s\n", argp_program_version);
    debug ("\nargs.delimiter: %s\nargs.dry_run: %d\nargs.helper: %s\nargs.ignore_case: %d\nargs.make_path: %d\nargs.mv_flags: %s\nargs.SOURCE_PATTERN: %s\nargs.DESTINATION_PATTERN: %s\n", \
           arguments.delimiter, \
           arguments.dry_run, \
           arguments.helper, \
           arguments.ignore_case, \
           arguments.make_path, \
           arguments.mv_flags, \
           srcPattern, \
           dstPattern);

    struct stat sb;
    char *dstPath;

    if (stat(srcPattern, &sb) == 0 && S_ISDIR(sb.st_mode)){ // srcPattern is directory

        debug("\nmain.SOURCE_PATTERN.is_directory\n");

        if (RunHelper(arguments.helper, srcPattern, filterOutput) != ERR_HELPER_FAILED){
            varTableItemsCount = Split(filterOutput, varTable, VARTABLE_SIZE);
            dstPath = FillPattern(dstPattern, arguments.delimiter, varTable, srcPattern, NULL, varTableItemsCount);
            if (dstPath == NULL)
                return EXIT_ERR_BAD_PATTERN;
            if (Rename(arguments.mv_flags, srcPattern, dstPath, arguments.dry_run, arguments.make_path, arguments.quiet_mode) == ERR_RENAME_FAILED){
                exit_status = EXIT_ERR_RENAME_FAILED;
            }
            else
                exit_status = 0;
            free (dstPath);
        }
        else
            exit_status = EXIT_ERR_HELPER_FAILED;
    }
    else {

        char *dirComponent = strdup(srcPattern);
        char *fileComponent = strdup(srcPattern);

        char *dirName = dirname(dirComponent);
        char *fileName = basename(fileComponent);

        debug("\nmain.dirName: %s\n", dirName);

        tinydir_dir dir;
        tinydir_file file;

        tinydir_open(&dir, dirName);

        while (dir.has_next) {

            tinydir_readfile(&dir, &file);

            if (MatchPattern(fileName, file.name, arguments.ignore_case)){

                debug("main.MatchPattern: %s\n", file.name);

                exit_status = 0;

                if (RunHelper(arguments.helper, file.path, filterOutput) != ERR_HELPER_FAILED){
                    varTableItemsCount = Split(filterOutput, varTable, VARTABLE_SIZE);
                    char *fileExtension = NULL;
                    if (strlen(file.extension) > 0){
                        fileExtension = malloc(strlen(file.extension)+2);
                        sprintf(fileExtension, ".%s", file.extension);
                    }
                    dstPath = FillPattern(dstPattern, arguments.delimiter, varTable, file.name, fileExtension, varTableItemsCount);
                    free(fileExtension);
                    if (dstPath == NULL)
                        return EXIT_ERR_BAD_PATTERN;
                    if (Rename(arguments.mv_flags, file.path, dstPath, arguments.dry_run, arguments.make_path, arguments.quiet_mode) == ERR_RENAME_FAILED){
                        exit_status = EXIT_ERR_RENAME_FAILED;
                    }
                    free (dstPath);
                }
                else
                    exit_status = EXIT_ERR_HELPER_FAILED;
            }
            tinydir_next(&dir);
        }
        tinydir_close(&dir);

        free(dirComponent);
        free(fileComponent);
    }

    debug("\nmain.exit_status: %d\n", exit_status);

    return exit_status;
}
