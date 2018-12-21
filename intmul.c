/**
 * @file intmul.c
 * @author Klaus Hahnenkamp <e11775823@student.tuwien.ac.at>
 * @date 15.12.2018
 *
 * @brief Main program module.
 * 
 * The program takes two hexadecimal integers A and B with an equal number of digits as input, multiplies them and prints the result.
 *
 **/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define NUM_CHILDS 4                 /*!< the default number of childs the process forks into */
#define DEFAULT_INPUT_BUFFER_SIZE 64 /*!< the default starting size of the potentially growing dynamic input buffer*/

static const char *pgrm_name = NULL; /*!< is set to argv[0] at program start */

static void print_product_and_exit(char *, char *);
static void send_child_data(int, const char *, const char *);
static char *input_string(FILE *, size_t);
static int hextodec(char);
static char dectohex(int);
static char *ptr_str_end(char *);
static char *addhexstr(char *, char *);
static void revarr(char *, size_t);
static char *hexlsh(char *, int);
static void exit_error(const char *);
static void sanitize_input(char *, char *, size_t *, size_t *);
static void validate_input(const char *, const char *, size_t, size_t);
static int wait_for_termination(pid_t);
static void add_leading_zero(char *);

/**
 * Main entry point of the program
 * @brief This function reads in two arbitrary sized hexadecimal strings from stdin, multiplies
 * them if they both contain a single digit and prints the results to stdout, otherwise
 * both strings are split in half, the process forks itself recursively into four child processes,
 * allocating two unnamed pipes per child for IPC
 * @param[in]  argc     argument count
 * @param[in]  argv     argument vector
 * @returns returns EXIT_SUCCESS
 * @details global variables: pgrm_name
 */
int main(int argc, char **argv)
{
    pgrm_name = argv[0];

    //program must be executed without any options or arguments per synopsis
    if (argc != 1)
    {
        fprintf(stderr, "[%s]: correct usage: intmul\n", pgrm_name);
        exit_error("invalid number of arguments");
    }

    //input data can be any size and is stored in a dynamically growing buffer
    char *A_buffer = input_string(stdin, DEFAULT_INPUT_BUFFER_SIZE);
    char *B_buffer = input_string(stdin, DEFAULT_INPUT_BUFFER_SIZE);

    if (A_buffer == NULL || B_buffer == NULL)
        exit_error("error reading data from stdin");

    size_t A_digits_read = strlen(A_buffer);
    size_t B_digits_read = strlen(B_buffer);

    sanitize_input(A_buffer, B_buffer, &A_digits_read, &B_digits_read);
    validate_input(A_buffer, B_buffer, A_digits_read, B_digits_read);

    //recursion basecase, if input are two single digits, multiply them, print the result to stdout and exit
    if (A_digits_read == 1)
        print_product_and_exit(A_buffer, B_buffer);

    //here starts the recursively executed code path

    if ((A_digits_read % 2) != 0 || (B_digits_read % 2) != 0)
        exit_error("input is not even");

    size_t n = A_digits_read;
    size_t nhalf = n / 2;

    //two pipes for each child
    int pout_cin_pipefd[2 * NUM_CHILDS];
    int pin_cout_pipefd[2 * NUM_CHILDS];

    //initializing pipes
    for (size_t i = 0; i < NUM_CHILDS; i++)
    {
        if (pipe(&pout_cin_pipefd[2 * i]) < 0 || pipe(&pin_cout_pipefd[2 * i]) < 0)
            exit_error("creating pipes failed");
    }

    //stores pids of child processes
    pid_t pids[NUM_CHILDS] = {0};

    //data is divided into 4 parts, one for each child process
    char Ah[nhalf + 1];
    char Al[nhalf + 1];
    char Bh[nhalf + 1];
    char Bl[nhalf + 1];

    memset(Ah, 0, nhalf + 1);
    memset(Al, 0, nhalf + 1);
    memset(Bh, 0, nhalf + 1);
    memset(Bl, 0, nhalf + 1);

    strncpy(Ah, A_buffer, nhalf);
    strncpy(Al, &A_buffer[nhalf], nhalf);
    strncpy(Bh, B_buffer, nhalf);
    strncpy(Bl, &B_buffer[nhalf], nhalf);

    free(A_buffer);
    free(B_buffer);

    //sends data into pipes and forks childs
    for (size_t i = 0; i < NUM_CHILDS; i++)
    {
        char *A_data = NULL;
        char *B_data = NULL;

        switch (i)
        {
        case 0:
            A_data = Ah;
            B_data = Bh;
            break;
        case 1:
            A_data = Ah;
            B_data = Bl;
            break;
        case 2:
            A_data = Al;
            B_data = Bh;
            break;
        case 3:
            A_data = Al;
            B_data = Bl;
            break;
        default:
            //not reachable
            assert(0);
            break;
        }

        send_child_data(pout_cin_pipefd[2 * i + 1], A_data, B_data);

        if ((pids[i] = fork()) == 0)
        {
            //this is executed in the child process

            //override stdin, stdout file descriptors
            if (dup2(pout_cin_pipefd[2 * i], STDIN_FILENO) < 0)
                exit_error("failed to dup2");

            if (dup2(pin_cout_pipefd[2 * i + 1], STDOUT_FILENO) < 0)
                exit_error("failed to dup2");

            //execute recursively
            if (execlp(pgrm_name, pgrm_name, NULL) < 0)
                exit_error("exec failed");

            perror("should never see this");
            assert(0);
        }
        else if (pids[i] < 0)
        {
            exit_error("fork failed");
        }

        //parent can close read end of pout pipe
        if (close(pout_cin_pipefd[2 * i]) < 0)
            exit_error("failed to close pipe");

        //parent can close write end of pin pipe
        if (close(pin_cout_pipefd[2 * i + 1]) < 0)
            exit_error("failed to close pipe");
    }

    //from here on we are in the parent process

    /*  results array stores data written by childs into the return pipe
        results[0]: Ah*Bh
        results[1]: Ah*Bl
        results[2]: Al*Bh
        results[3]: Al*Bl
    */
    char results[4][nhalf * 2 + 1];
    memset(results, 0, sizeof(results[0][0]) * 4 * (nhalf * 2 + 1));

    //data from childs is read into results array
    for (size_t i = 0; i < NUM_CHILDS; i++)
    {
        FILE *in_stream;
        if ((in_stream = fdopen(pin_cout_pipefd[2 * i], "r")) == NULL)
            exit_error("failed to open fd");

        if (fgets(results[i], nhalf * 2 + 1, in_stream) == NULL)
            exit_error("failed to read results from child");

        fclose(in_stream);
    }

    //remove trailing \n characters
    for (size_t i = 0; i < NUM_CHILDS; i++)
    {
        size_t len = strlen(results[i]);
        if (results[i][len - 1] == '\n')
        {
            results[i][len - 1] = '\0';
        }
    }

    //perform the base 16 left shifts
    char *r1 = hexlsh(results[0], n);     //Ah*Bh*16^n
    char *r2 = hexlsh(results[1], nhalf); //Ah*Bl*16^n/2
    char *r3 = hexlsh(results[2], nhalf); //Al*Bh*16^n/2

    //add up results
    char *a1 = addhexstr(r1, r2);
    char *a2 = addhexstr(a1, r3);
    char *a3 = addhexstr(a2, results[3]);

    //leading zeros must be added if result has odd digit count
    add_leading_zero(a3);

    //final result is written to stdout
    if (printf("%s\n", a3) < 0)
        exit_error("failed to print final string to stdout");

    free(r1);
    free(r2);
    free(r3);
    free(a1);
    free(a2);
    free(a3);

    //pipes already closed at this point

    //wait for childs to terminate
    for (size_t i = 0; i < NUM_CHILDS; i++)
    {
        if (wait_for_termination(pids[i]) != EXIT_SUCCESS)
            exit_error("wait for child failed");
    }

    return EXIT_SUCCESS;
}

/**
 * Multiplies two hex digits
 * @brief Multiplies the hexstrings in abuf and bbuf, prints the result to stdout and exits with code 0
 * @param[in]   abuf  first hex digit
 * @param[in]   bbuf  second hex digit
 */
static void print_product_and_exit(char *abuf, char *bbuf)
{
    int A;
    int B;

    if ((A = strtol(abuf, NULL, 16)) < 0 || (B = strtol(bbuf, NULL, 16)) < 0)
        exit_error("couldn't parse single digit");

    if (printf("%x\n", A * B) < 0)
        exit_error("failed to print single digit mult result to stdout");

    fflush(stdout);
    exit(EXIT_SUCCESS);
}

/**
 * Sends data to child processes
 * @brief Sends two splitted hex strings to a child process over a pipe specified in argument
 * @param[in]   A_data  first hex string
 * @param[in]   B_data  second hex string
 */
static void send_child_data(int fd, const char *A_data, const char *B_data)
{
    FILE *out_stream;
    if ((out_stream = fdopen(fd, "w")) == NULL)
        exit_error("failed to open fd");

    if (fprintf(out_stream, "%s\n%s\n", A_data, B_data) < 0)
        exit_error("failed to send data to child");

    fflush(out_stream);
    if (fclose(out_stream) == EOF)
        exit_error("failed to close stream");
}

/**
 * Returns a pointer to the input data read in over stdin
 * @brief Reads in input data of arbitrary length into a dynamically growing buffer
 * and returns a pointer to this buffer
 * @param[in]   fp  file descriptor from which the data is read
 * @param[in]   size  starting size of the dynamic array
 * @returns pointer to input data
 */
static char *input_string(FILE *fp, size_t size)
{
    char *str;
    int ch;
    size_t len = 0;

    if ((str = realloc(NULL, size)) == NULL)
        exit_error("realloc failed in inputString");

    while ((ch = fgetc(fp)) != EOF && ch != '\n')
    {
        str[len++] = ch;
        if (len == size)
        {
            if ((str = realloc(str, sizeof(char) * (size <<= 1))) == NULL)
                exit_error("realloc failed in inputString");
        }
    }
    str[len++] = '\0';

    if ((str = realloc(str, sizeof(char) * len)) == NULL)
        exit_error("realloc failed in inputString");

    return str;
}

/**
 * Prints an error message and exits with code 1
 * @brief This function prints the error message specified as argument, prints
 * it to stderr with additionally information if errno is set
 * @param[in]   s  error message to be printed
 */
static void exit_error(const char *s)
{
    if (errno == 0)
        fprintf(stderr, "[%s]: %s\n", pgrm_name, s);
    else
        fprintf(stderr, "[%s]: %s, Error: %s\n", pgrm_name, s, strerror(errno));

    exit(EXIT_FAILURE);
}

/**
 * Converts a base16 character to a base10 character
 * @brief Converts a base16 character to a base10 character
 * @param[in]   c  character to be converted
 * @returns the converted character
 */
static int hextodec(char c)
{
    switch (c)
    {
    case '0':
        return 0;
    case '1':
        return 1;
    case '2':
        return 2;
    case '3':
        return 3;
    case '4':
        return 4;
    case '5':
        return 5;
    case '6':
        return 6;
    case '7':
        return 7;
    case '8':
        return 8;
    case '9':
        return 9;
    case 'a':
        return 10;
    case 'A':
        return 10;
    case 'b':
        return 11;
    case 'B':
        return 11;
    case 'c':
        return 12;
    case 'C':
        return 12;
    case 'd':
        return 13;
    case 'D':
        return 13;
    case 'e':
        return 14;
    case 'E':
        return 14;
    case 'f':
        return 15;
    case 'F':
        return 15;
    default:
        //this case is reachable
        return -1;
    }
}

/**
 * Converts a base10 character to a base16 character
 * @brief Converts a base10 character to a base16 character
 * @param[in]   c  character to be converted
 * @returns the converted character
 */
static char dectohex(int i)
{
    switch (i)
    {
    case 0:
        return '0';
    case 1:
        return '1';
    case 2:
        return '2';
    case 3:
        return '3';
    case 4:
        return '4';
    case 5:
        return '5';
    case 6:
        return '6';
    case 7:
        return '7';
    case 8:
        return '8';
    case 9:
        return '9';
    case 10:
        return 'a';
    case 11:
        return 'b';
    case 12:
        return 'c';
    case 13:
        return 'd';
    case 14:
        return 'e';
    case 15:
        return 'f';
    default:
        //not reachable
        assert(0);
        break;
    }
}

/**
 * Returns pointer to end of a string
 * @brief Takes a string as parameter and returns a pointer to end of that string
 * including the null terminator
 * @param[in]   s  the string
 * @returns pointer to end of the string
 */
static char *ptr_str_end(char *s)
{
    while (*s != '\0')
    {
        s++;
    }
    return s;
}

/**
 * Adds two hexadecimal strings and returns a pointer to the result
 * @brief Adds two hexadecimal strings and returns a pointer to the result
 * @param[in]   s1  first operand string
 * @param[in]   s2  second operand string
 * @returns pointer to result of addition
 */
static char *addhexstr(char *s1, char *s2)
{
    char *s1_start = s1;
    char *s2_start = s2;
    char *s1_end = ptr_str_end(s1) - 1; //points to last char
    char *s2_end = ptr_str_end(s2) - 1;

    size_t out_buffer_len = DEFAULT_INPUT_BUFFER_SIZE;
    size_t buffer_counter = 0;
    char *out_buffer = (char *)malloc(out_buffer_len + 5);

    if (out_buffer == NULL)
        exit_error("malloc failed in addhexstr");

    memset(out_buffer, 0, out_buffer_len + 5);

    int carry = 0;

    while (s1_start <= s1_end || s2_start <= s2_end)
    {
        char cur_hchar1 = s1_end < s1_start ? '0' : *(s1_end--);
        char cur_hchar2 = s2_end < s2_start ? '0' : *(s2_end--);

        int cur_ichar1 = hextodec(cur_hchar1);
        int cur_ichar2 = hextodec(cur_hchar2);

        int addresult = cur_ichar1 + cur_ichar2 + carry;

        carry = addresult > 15 ? 1 : 0;

        if (buffer_counter == out_buffer_len)
        {
            out_buffer = realloc(out_buffer, (out_buffer_len <<= 1) + 5);
            memset(out_buffer + buffer_counter, 0, 4);
            if (out_buffer == NULL)
                exit_error("realloc failed in addhexstr");
        }

        out_buffer[buffer_counter++] = dectohex(addresult % 16);
        out_buffer[buffer_counter] = '\0';
    }

    if (carry == 1)
        out_buffer[buffer_counter++] = '1';

    revarr(out_buffer, strlen(out_buffer) + 1);

    out_buffer[buffer_counter] = '\0';
    return out_buffer;
}

/**
 * Reverses a given array
 * @brief This function reverses the order of all the elements of the array passed to this function
 * @param[in]   a1      array the order of should be reversed
 * @param[in]   size    the size of the array
 */
static void revarr(char *a1, size_t size)
{
    char buffer[size];
    memset(buffer, 0, sizeof(buffer));

    for (int i = 0; i < size - 1; i++)
    {
        buffer[i] = a1[size - 2 - i];
    }

    buffer[size - 1] = '\0';
    strcpy(a1, buffer);
}

/**
 * Performs a base16 left shift
 * @brief This function takes a string parameter s representing a hexadecimal number and
 * performs a left shift of n hexadecimal places
 * @param[in]   s   the string to be shifted
 * @param[in]   n   number of places the string should be leftshifted
 * @returns pointer to shifted string
 */
static char *hexlsh(char *s, int n)
{
    char *ret = (char *)malloc(strlen(s) + n + 1);

    if (ret == NULL)
        exit_error("malloc failed in hexlsh");

    memset(ret, 0, strlen(s) + n + 1);

    strcpy(ret, s);

    for (size_t i = 0; i < n; i++)
    {
        strcat(ret, "0");
    }
    return ret;
}

/**
 * Deletes trailing occurrences of newline characters
 * @brief This function deletes trailing occurrences of newline characters in both
 * input strings
 * @param[in]   A_buffer        first string to be checked
 * @param[in]   B_buffer        second string to be checked
 * @param[in]   A_digits_read   pointer to size of input A
 * @param[in]   B_digits_read   pointer to size of input B
 */
static void sanitize_input(char *A_buffer, char *B_buffer, size_t *A_digits_read, size_t *B_digits_read)
{
    if (*A_digits_read > 0 && A_buffer[*A_digits_read - 1] == '\n')
        A_buffer[--(*A_digits_read)] = '\0';

    if (*B_digits_read > 0 && B_buffer[*B_digits_read - 1] == '\n')
        B_buffer[--(*B_digits_read)] = '\0';
}

/**
 * Checks if input is valid
 * @brief This function checks both input string for validity, e.g. no input,
 * different sized strings, invalid characters
 * @param[in]   A_buffer        first string to be checked
 * @param[in]   B_buffer        second string to be checked
 * @param[in]   A_digits_read   size of input A
 * @param[in]   B_digits_read   size of input B
 */
static void validate_input(const char *A_buffer, const char *B_buffer, size_t A_digits_read, size_t B_digits_read)
{
    if (A_digits_read == 0 || B_digits_read == 0)
        exit_error("no input given");

    if (A_digits_read != B_digits_read)
        exit_error("A and B don't have equal length");

    //check for invalid characters
    for (size_t i = 0; i < A_digits_read; i++)
    {
        if (hextodec(A_buffer[i]) == -1 || hextodec(B_buffer[i]) == -1)
            exit_error("input contained invalid character");
    }
}

/**
 * Blocks until child with given pid terminates
 * @brief This function waits for a child process identified with a pid to terminate
 * with exit code 0, if the child exits with exit code other than 0, the program terminates
 * with an error
 * @param[in]   p    pid of child process that should be waited on
 * @returns the exit code of the child process
 */
static int wait_for_termination(pid_t p)
{
    int status = 0;
    if (waitpid(p, &status, 0) == -1)
        exit_error("waitpid failed");

    return WEXITSTATUS(status);
}

/**
 * Adds leading 0 character in front of a given string when digit count is odd
 * @brief This function adds a leading 0 character in front of it, for when
 * the digit count is odd
 * @param[in]   p    pid of child process that should be waited on
 */
static void add_leading_zero(char *s)
{
    size_t len = strlen(s);
    if ((len % 2) != 0)
    {
        if ((s = realloc(s, len + 2)) == NULL)
            exit_error("realloc failed in add_leading_zero");

        strcpy(s + 1, s);
        s[0] = '0';
        s[len + 1] = '\0';
    }
}
