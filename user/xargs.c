#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define LINE_SIZE 512

/*
 * Append the words in one input line to the original command,
 * then execute the resulting command in a child process.
 */
static int
run_line(char *line, int length, int command_argc, char **command_argv)
{
  char *exec_argv[MAXARG];
  char *p;
  int exec_argc;
  int pid;
  int i;

  // Terminate the input line so it can be parsed as a C string.
  line[length] = 0;

  // Copy the original command and its arguments.
  exec_argc = 0;
  for(i = 0; i < command_argc; i++){
    if(exec_argc >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      return -1;
    }

    exec_argv[exec_argc++] = command_argv[i];
  }

  /*
   * Split the input line at spaces or tabs and append
   * every resulting word to the argument array.
   */
  p = line;

  while(*p != 0){
    // Skip and terminate leading whitespace.
    while(*p == ' ' || *p == '\t'){
      *p = 0;
      p++;
    }

    if(*p == 0){
      break;
    }

    if(exec_argc >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      return -1;
    }

    // The beginning of the next word becomes an argument.
    exec_argv[exec_argc++] = p;

    // Move to the end of the current word.
    while(*p != 0 && *p != ' ' && *p != '\t'){
      p++;
    }

    // Replace the separator with a string terminator.
    if(*p != 0){
      *p = 0;
      p++;
    }
  }

  // A null pointer must terminate the exec argument array.
  exec_argv[exec_argc] = 0;

  /*
   * Ignore empty input lines. In that case, the argument array
   * contains only the original command.
   */
  if(exec_argc == command_argc){
    return 0;
  }

  pid = fork();

  if(pid < 0){
    fprintf(2, "xargs: fork failed\n");
    return -1;
  }

  if(pid == 0){
    exec(exec_argv[0], exec_argv);

    // exec() returns only if the command could not be started.
    fprintf(2, "xargs: exec %s failed\n", exec_argv[0]);
    exit(1);
  }

  // Execute one command at a time.
  wait(0);
  return 0;
}

int
main(int argc, char *argv[])
{
  char line[LINE_SIZE];
  char c;
  int length;
  int n;

  if(argc < 2){
    fprintf(2, "usage: xargs command [arguments ...]\n");
    exit(1);
  }

  if(argc - 1 >= MAXARG){
    fprintf(2, "xargs: too many arguments\n");
    exit(1);
  }

  length = 0;

  /*
   * Read standard input one character at a time.
   * A newline marks the end of one command invocation.
   */
  while((n = read(0, &c, 1)) > 0){
    if(c == '\n'){
      if(run_line(line, length, argc - 1, &argv[1]) < 0){
        exit(1);
      }

      length = 0;
      continue;
    }

    if(length >= LINE_SIZE - 1){
      fprintf(2, "xargs: input line too long\n");
      exit(1);
    }

    line[length++] = c;
  }

  if(n < 0){
    fprintf(2, "xargs: read failed\n");
    exit(1);
  }

  /*
   * The final line might reach EOF without ending in '\n'.
   */
  if(length > 0){
    if(run_line(line, length, argc - 1, &argv[1]) < 0){
      exit(1);
    }
  }

  exit(0);
}

