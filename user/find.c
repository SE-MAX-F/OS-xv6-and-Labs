#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "user/user.h"

/*
 * Return the last path component.
 * For example, "./a/b" returns "b".
 */
static char *
last_component(char *path)
{
  char *name;

  name = path + strlen(path);

  while(name > path && name[-1] != '/'){
    name--;
  }

  return name;
}

/*
 * Recursively search for files named target,
 * starting from path.
 */
static void
find(char *path, char *target)
{
  char buffer[512];
  char *p;
  int fd;
  struct dirent entry;
  struct stat st;

  // Open the current file or directory.
  fd = open(path, 0);

  if(fd < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  // Obtain the type and other information of the current path.
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    /*
     * Compare only the final component of the path
     * with the requested file name.
     */
    if(strcmp(last_component(path), target) == 0){
      printf("%s\n", path);
    }
    break;

  case T_DIR:
    /*
     * Reserve enough space for:
     * existing path + "/" + directory name + '\0'.
     */
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buffer)){
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }

    strcpy(buffer, path);
    p = buffer + strlen(buffer);

    // Add a slash unless the path already ends with one.
    if(p == buffer || p[-1] != '/'){
      *p++ = '/';
    }

    /*
     * Read every directory entry and recursively
     * process valid entries.
     */
    while(read(fd, &entry, sizeof(entry)) == sizeof(entry)){
      if(entry.inum == 0){
        continue;
      }

      /*
       * Directory names have a fixed length of DIRSIZ,
       * so add a terminating zero manually.
       */
      memmove(p, entry.name, DIRSIZ);
      p[DIRSIZ] = 0;

      /*
       * Never recurse into "." or "..", otherwise the
       * traversal would repeatedly visit the same directories.
       */
      if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0){
        continue;
      }

      find(buffer, target);
    }

    break;
  }

  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "usage: find path name\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}
