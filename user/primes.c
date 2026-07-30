#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

/*
 * Read integers from read_fd, print the first integer as a prime,
 * and pass integers that are not divisible by it to the next process.
 */
static void
sieve(int read_fd)
{
  int prime;
  int number;
  int next_pipe[2];
  int pid;
  int n;

  /*
   * The first number that reaches this process is the next prime.
   * A return value of 0 means that every writer has closed the pipe.
   */
  n = read(read_fd, &prime, sizeof(prime));

  if(n == 0){
    close(read_fd);
    exit(0);
  }

  if(n != sizeof(prime)){
    fprintf(2, "primes: read failed\n");
    close(read_fd);
    exit(1);
  }

  printf("prime %d\n", prime);

  // Create a pipe for the next filtering stage.
  if(pipe(next_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    close(read_fd);
    exit(1);
  }

  pid = fork();

  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(read_fd);
    close(next_pipe[0]);
    close(next_pipe[1]);
    exit(1);
  }

  if(pid == 0){
    /*
     * The child becomes the next filtering process.
     * It only reads from next_pipe.
     */
    close(next_pipe[1]);
    close(read_fd);
    sieve(next_pipe[0]);
  }

  /*
   * The parent keeps the current filtering stage.
   * It only writes to next_pipe.
   */
  close(next_pipe[0]);

  while((n = read(read_fd, &number, sizeof(number)))
        == sizeof(number)){
    if(number % prime != 0){
      if(write(next_pipe[1], &number, sizeof(number))
         != sizeof(number)){
        fprintf(2, "primes: write failed\n");
        close(read_fd);
        close(next_pipe[1]);
        exit(1);
      }
    }
  }

  if(n < 0){
    fprintf(2, "primes: read failed\n");
  }

  /*
   * Closing the write end is essential. It allows the next process
   * to observe EOF after all remaining numbers have been processed.
   */
  close(read_fd);
  close(next_pipe[1]);

  wait(0);
  exit(n < 0 ? 1 : 0);
}

int
main(int argc, char *argv[])
{
  int first_pipe[2];
  int pid;
  int number;

  (void)argc;
  (void)argv;

  if(pipe(first_pipe) < 0){
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  pid = fork();

  if(pid < 0){
    fprintf(2, "primes: fork failed\n");
    close(first_pipe[0]);
    close(first_pipe[1]);
    exit(1);
  }

  if(pid == 0){
    /*
     * The first child starts the filtering pipeline.
     * It only reads from first_pipe.
     */
    close(first_pipe[1]);
    sieve(first_pipe[0]);
  }

  /*
   * The original parent generates the integers from 2 through 35.
   * It only writes to first_pipe.
   */
  close(first_pipe[0]);

  for(number = 2; number <= 35; number++){
    if(write(first_pipe[1], &number, sizeof(number))
       != sizeof(number)){
      fprintf(2, "primes: write failed\n");
      close(first_pipe[1]);
      exit(1);
    }
  }

  /*
   * Closing the write end tells the first filtering process
   * that no more integers will be sent.
   */
  close(first_pipe[1]);

  wait(0);
  exit(0);
}
