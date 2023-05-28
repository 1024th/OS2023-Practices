#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

int main() {
  pid_t pid;
  // OPEN FILES
  int fd;
  fd = open("test.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    fprintf(stderr, "fail on open test.txt\n");
    return -1;
  }
  // write 'hello fcntl!' to file
  char str[20] = "hello fcntl!";
  write(fd, str, strlen(str));

  // DUPLICATE FD
  int fd2;
  fd2 = dup(fd);
  if (fd2 == -1) {
    fprintf(stderr, "fail on dup fd\n");
    return -1;
  }

  pid = fork();

  if (pid < 0) {
    // FAILS
    printf("error in fork");
    return 1;
  }

  struct flock fl;

  if (pid > 0) {
    // PARENT PROCESS
    // set the lock
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = getpid();
    int ret = fcntl(fd, F_SETLKW, &fl);
    if (ret == -1) {
      fprintf(stderr, "fail on lock\n");
      return -1;
    }

    // append 'b'
    write(fd, "b", 1);

    // unlock
    fl.l_type = F_UNLCK;
    ret = fcntl(fd, F_SETLK, &fl);
    if (ret == -1) {
      fprintf(stderr, "fail on unlock\n");
      return -1;
    }

    sleep(3);

    // read the file
    lseek(fd, 0, SEEK_SET);
    read(fd, str, sizeof(str) - 1);
    printf("%s", str);  // the feedback should be 'hello fcntl!ba'

    exit(0);

  } else {
    // CHILD PROCESS
    sleep(2);
    // get the lock
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = getpid();
    int ret = fcntl(fd2, F_SETLKW, &fl);
    if (ret == -1) {
      fprintf(stderr, "fail on lock\n");
      return -1;
    }

    // append 'a'
    write(fd2, "a", 1);

    // unlock
    fl.l_type = F_UNLCK;
    ret = fcntl(fd2, F_SETLK, &fl);
    if (ret == -1) {
      fprintf(stderr, "fail on unlock\n");
      return -1;
    }
    close(fd2);

    exit(0);
  }
  close(fd);
  return 0;
}