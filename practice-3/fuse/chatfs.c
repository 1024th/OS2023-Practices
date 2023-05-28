#define FUSE_USE_VERSION 31
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fuse.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// A simple file system that supports cd, ls, mkdir, touch, cat, echo, rmdir,
// and rm. Use the fuse library to implement a file system that stores all data
// in memory.

// The file system is a tree of directories and files. Each directory contains a
// list of files and a list of subdirectories. Each file contains a string of
// data.

struct file {
  char *name;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  time_t atime;
  time_t mtime;
  size_t size;
  char *data;
  struct friend_t *friend;
  struct file *next;
};

struct directory {
  char *name;
  mode_t mode;
  uid_t uid;
  gid_t gid;
  time_t atime;
  time_t mtime;
  struct file *files;
  struct directory *subdirectories;
  struct directory *next;
};

#define MAX_FRIENDS 10
#define MAX_CHAT_SIZE 1024
#define MAX_USERNAME_SIZE 32
struct friend_t {
  struct sockaddr_in addr;
  char username[MAX_USERNAME_SIZE];
  struct file *f;  // the file representing the chat to this friend
};
struct friends_t {
  struct friend_t *list[MAX_FRIENDS];
  int num_friends;
} friends;

struct directory *root;
int listenfd;
pthread_t listening_thread;
struct sockaddr_in serv_addr;
char username[MAX_USERNAME_SIZE];
pthread_rwlock_t rwlock;

// debug print function that only prints when DEBUG is defined
#ifdef DEBUG
#define debug_print(...) fprintf(stdout, "[dbg]" __VA_ARGS__)
#else
#define debug_print(...)
#endif

// Helper function to find a file in a directory.
struct file *find_file(struct directory *dir, const char *name) {
  struct file *file = dir->files;
  while (file != NULL) {
    if (strcmp(file->name, name) == 0) {
      return file;
    }
    file = file->next;
  }
  return NULL;
}

// Helper function to find a subdirectory in a directory.
struct directory *find_subdirectory(struct directory *dir, const char *name) {
  struct directory *subdir = dir->subdirectories;
  while (subdir != NULL) {
    if (strcmp(subdir->name, name) == 0) {
      return subdir;
    }
    subdir = subdir->next;
  }
  return NULL;
}

// Helper function to find a directory in the file system.
struct directory *find_directory(const char *path) {
  debug_print("find_directory(%s)\n", path);
  struct directory *dir = root;
  char *path_copy = strdup(path);
  char *token = strtok(path_copy, "/");
  while (token != NULL) {
    dir = find_subdirectory(dir, token);
    if (dir == NULL) {
      return NULL;
    }
    token = strtok(NULL, "/");
  }
  free(path_copy);
  return dir;
}

// Used to return a directory or file from a function.
struct find_result {
  struct directory *dir;
  struct file *file;
};

// Helper function to find a file or a subdirectory in a directory.
// struct find_result find_file_or_subdirectory(struct directory *dir, const
// char *name) {
//   struct find_result res = {.dir = NULL, .file = NULL};
//   res.file = find_file(dir, name);
//   if (res.file != NULL) {
//     return res;
//   }
//   res.dir = find_subdirectory(dir, name);
//   return res;
// }

// Helper function to find a directory or a file in the file system.
struct find_result find(const char *path) {
  debug_print("find(%s)\n", path);
  struct find_result res = {.dir = root, .file = NULL};
  char *path_copy = strdup(path);
  char *token = strtok(path_copy, "/");
  while (token != NULL) {
    res.file = find_file(res.dir, token);
    if (res.file != NULL) {  // found a file
      res.dir = NULL;
      // check if there are more tokens
      token = strtok(NULL, "/");
      if (token != NULL) {
        res.dir = NULL;
        res.file = NULL;
      }
      break;
    }
    res.dir = find_subdirectory(res.dir, token);
    token = strtok(NULL, "/");
  }
  free(path_copy);
  return res;
}

// Helper function to split a path into a directory and a file name.
struct split_result {
  char *dir;
  char *name;
};

struct split_result split(const char *path) {
  struct split_result res = {.dir = NULL, .name = NULL};
  char *path_copy = strdup(path);
  char *last_slash = strrchr(path_copy, '/');
  if (last_slash == NULL) {
    res.name = strdup(path_copy);
  } else {
    *last_slash = '\0';
    res.dir = strdup(path_copy);
    res.name = strdup(last_slash + 1);
  }
  free(path_copy);
  if (strlen(res.dir) == 0) {  // root directory
    free(res.dir);
    res.dir = strdup("/");
  }
  return res;
}

// Helper function to add a file to a directory.
void add_file(struct directory *dir, struct file *file) {
  file->next = dir->files;
  dir->files = file;
}

// Helper function to add a subdirectory to a directory.
void add_subdirectory(struct directory *dir, struct directory *subdir) {
  subdir->next = dir->subdirectories;
  dir->subdirectories = subdir;
}

// Helper function to remove a file from a directory.
void remove_file(struct directory *dir, struct file *file) {
  if (dir->files == file) {
    dir->files = file->next;
    return;
  }
  struct file *prev = dir->files;
  while (prev->next != file) {
    prev = prev->next;
  }
  prev->next = file->next;
}

// Helper function to remove a subdirectory from a directory.
void remove_subdirectory(struct directory *dir, struct directory *subdir) {
  if (dir->subdirectories == subdir) {
    dir->subdirectories = subdir->next;
    return;
  }
  struct directory *prev = dir->subdirectories;
  while (prev->next != subdir) {
    prev = prev->next;
  }
  prev->next = subdir->next;
}

// Helper function to compare two sockaddr_in structs.
int sockaddr_cmp(struct sockaddr_in *a, struct sockaddr_in *b) {
  if (a->sin_family != b->sin_family) {
    return a->sin_family - b->sin_family;
  }
  if (a->sin_port != b->sin_port) {
    return a->sin_port - b->sin_port;
  }
  return memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr));
}

// Helper function to add a friend to the list of friends.
// Returns 0 on success, -1 on failure.
int add_friend(struct friend_t *friend) {
  if (friends.num_friends == MAX_FRIENDS) {
    printf("Too many friends\n");
    return -1;
  }
  struct file *f = find_file(root, friend->username);
  if (f == NULL) {
    // create a new file
    f = malloc(sizeof(struct file));
    f->name = strdup(friend->username);
    add_file(root, f);
  }
  f->mode = S_IFREG | 0666;
  f->uid = getuid();
  f->gid = getgid();
  f->atime = f->mtime = time(NULL);
  f->friend = friend;
  // clear the file
  f->size = 0;
  f->data = strdup("");

  friend->f = f;
  friends.list[friends.num_friends] = friend;
  friends.num_friends++;
  return 0;
}

enum msg_type {
  MSG_TYPE_CONNECT,
  MSG_TYPE_DISCONNECT,
  MSG_TYPE_CHAT,
};

int send_message(struct friend_t *friend, enum msg_type type) {
  debug_print("send_message(%d, %d)\n", friend->addr.sin_port, type);
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("create socket failed");
    return -1;
  }
  if (connect(sockfd, (struct sockaddr *)&friend->addr, sizeof(friend->addr)) <
      0) {
    perror("connect failed");
    close(sockfd);
    return -1;
  }
  int n = write(sockfd, username, MAX_USERNAME_SIZE);
  if (n < 0) {
    perror("write username failed");
    close(sockfd);
    return -1;
  }
  n = write(sockfd, &type, sizeof(type));
  if (n < 0) {
    perror("write message type failed");
    close(sockfd);
    return -1;
  }

  if (type == MSG_TYPE_CHAT) {
    n = write(sockfd, friend->f->data, friend->f->size);
    if (n < 0) {
      perror("write message failed");
      close(sockfd);
      return -1;
    }
  } else if (type == MSG_TYPE_CONNECT) {
    // send port of this node
    n = write(sockfd, &serv_addr.sin_port, sizeof(serv_addr.sin_port));
    if (n < 0) {
      perror("write port failed");
      close(sockfd);
      return -1;
    }
    n = read(sockfd, friend->username, MAX_USERNAME_SIZE);
    if (n < 0) {
      perror("read friend username failed");
      close(sockfd);
      return -1;
    }
  }
  close(sockfd);
  return 0;
}

int connect_to(const char *buf) {
  char host[20];
  int port;
  sscanf(buf, "%19s %d", host, &port);
  // check if already connected
  for (int i = 0; i < friends.num_friends; i++) {
    if (sockaddr_cmp(&friends.list[i]->addr, &serv_addr) == 0) {
      printf("Already connected to %s\n", host);
      return -1;
    }
  }
  struct friend_t *friend = malloc(sizeof(struct friend_t));
  if (inet_pton(AF_INET, host, &friend->addr.sin_addr) != 1) {
    printf("Invalid IP address\n");
    free(friend);
    return -1;
  }
  friend->addr.sin_family = AF_INET;
  friend->addr.sin_port = htons(port);
  debug_print("Connecting to %s:%d\n", host, port);
  if (send_message(friend, MSG_TYPE_CONNECT) != 0) {
    printf("Failed to connect\n");
    free(friend);
    return -1;
  }
  return add_friend(friend);
}

// Another thread that handles a client connection.
void listening() {
  char friend_name[MAX_USERNAME_SIZE];
  enum msg_type type;
  char buffer[MAX_CHAT_SIZE];
  struct file *f;
  int n;
  while (1) {
    // Accept a connection.
    struct sockaddr_in client_addr;
    socklen_t client_addrlen = sizeof(client_addr);
    int clientfd =
        accept(listenfd, (struct sockaddr *)&client_addr, &client_addrlen);
    if (clientfd < 0) {
      perror("accept failed");
      continue;
    }
    debug_print("Accepted connection from %d\n", client_addr.sin_port);
    n = read(clientfd, friend_name, MAX_USERNAME_SIZE);
    if (n != MAX_USERNAME_SIZE) {
      perror("read username failed");
      close(clientfd);
      continue;
    }
    n = read(clientfd, &type, sizeof(type));
    if (n < 0) {
      perror("read message type failed");
      close(clientfd);
      continue;
    }
    n = read(clientfd, buffer, sizeof(buffer));
    if (n < 0) {
      perror("read message failed");
      close(clientfd);
      continue;
    }
    debug_print(
        "  username: %s, type: %d, message: %s\n", friend_name, type, buffer);

    pthread_rwlock_wrlock(&rwlock);

    // find the friend
    struct friend_t *friend = NULL;
    for (int i = 0; i < friends.num_friends; i++) {
      if (strcmp(friends.list[i]->username, friend_name) == 0) {
        friend = friends.list[i];
        break;
      }
    }
    if (friend == NULL) {
      // add the friend
      printf("Add new friend %s, address %s:%d\n", friend_name,
          inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
      friend = malloc(sizeof(struct friend_t));
      friend->addr = client_addr;
      strncpy(friend->username, friend_name, MAX_USERNAME_SIZE);
      if (add_friend(friend) < 0) {
        printf("Failed to add friend %s\n", friend_name);
        free(friend);
        pthread_rwlock_unlock(&rwlock);
        close(clientfd);
        continue;
      }
    }
    f = friend->f;
    if (type == MSG_TYPE_CHAT) {
      // write to chat file
      free(f->data);
      f->data = malloc(n);
      f->size = n;
      memcpy(f->data, buffer, n);
    } else if (type == MSG_TYPE_CONNECT) {
      // send the username of this node to the friend
      n = write(clientfd, username, strlen(username) + 1);
      if (n < 0) {
        perror("write username failed");
        close(clientfd);
        pthread_rwlock_unlock(&rwlock);
        continue;
      }
    } else if (type == MSG_TYPE_DISCONNECT) {
      // TODO: disconnect
    }

    pthread_rwlock_unlock(&rwlock);
    close(clientfd);
  }
}

void init_server(int port) {
  // Create a socket.
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    exit(1);
  }

  // Set the "reuse port" socket option.
  int optval = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

  // Bind to the specified port.
  struct sockaddr_in server_addr = {.sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    exit(1);
  }

  // Listen for connections.
  if (listen(listenfd, 16) < 0) {
    perror("listen failed");
    exit(1);
  }

  printf("listening on port %d\n", ntohs(server_addr.sin_port));
  pthread_create(&listening_thread, NULL, (void *)listening, NULL);
}

struct file *create_special_file(const char *name) {
  struct file *f = malloc(sizeof(struct file));
  f->name = strdup(name);
  f->mode = S_IFREG | 0644;
  f->uid = getuid();
  f->gid = getgid();
  f->atime = f->mtime = time(NULL);
  f->data = strdup("");
  f->size = 0;
  f->friend = NULL;
  f->next = NULL;
  return f;
}

/*
 * fuse functions
 */

static void *chatfs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
  cfg->kernel_cache = 1;
  root = malloc(sizeof(struct directory));
  root->name = strdup("");
  root->mode = S_IFDIR | 0755;
  root->uid = getuid();
  root->gid = getgid();
  root->atime = root->mtime = time(NULL);
  root->files = NULL;
  root->subdirectories = NULL;
  root->next = NULL;

  // special files
  struct file *f;
  f = create_special_file("connect");
  add_file(root, f);
  // f = create_special_file("info");
  // add_file(root, f);

  // input username
  printf("Enter your username: \n");
  char format[32];
  snprintf(format, sizeof(format), "%%%zus", (size_t)MAX_USERNAME_SIZE - 1);
  scanf(format, username);
  printf("Your username is %s\n", username);
  // input port
  printf("Enter the port you want to listen on: \n");
  int port;
  scanf("%d", &port);
  init_server(port);

  pthread_rwlock_init(&rwlock, NULL);
  return NULL;
}

// static int chatfs_access(const char *path, int mask) {
//   // Check if the file exists and the user has permission to access it.
//   debug_print("access(%s, %d)\n", path, mask);
//   pthread_rwlock_rdlock(&rwlock);
//   struct find_result res = find(path);
//   int ret;
//   if (res.dir != NULL) {
//     if ((res.dir->mode & mask) == mask) {
//       ret = 0;
//     } else {
//       ret = -EACCES;
//     }
//   } else if (res.file != NULL) {
//     if ((res.file->mode & mask) == mask) {
//       ret = 0;
//     } else {
//       ret = -EACCES;
//     }
//   } else {
//     ret = -ENOENT;
//   }
//   pthread_rwlock_unlock(&rwlock);
//   return ret;
// }

static int chatfs_getattr(
    const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
  pthread_rwlock_rdlock(&rwlock);
  struct find_result res = find(path);
  int ret;
  if (res.dir != NULL) {
    stbuf->st_mode = res.dir->mode;
    stbuf->st_uid = res.dir->uid;
    stbuf->st_gid = res.dir->gid;
    stbuf->st_atime = res.dir->atime;
    stbuf->st_mtime = res.dir->mtime;
    stbuf->st_nlink = 2;
    ret = 0;
  } else if (res.file != NULL) {
    stbuf->st_mode = res.file->mode;
    stbuf->st_uid = res.file->uid;
    stbuf->st_gid = res.file->gid;
    stbuf->st_atime = res.file->atime;
    stbuf->st_mtime = res.file->mtime;
    stbuf->st_nlink = 1;
    stbuf->st_size = res.file->size;
    debug_print("getattr(%s) -> found file, size: %ld\n", path, stbuf->st_size);
    debug_print("  atime: %ld, mtime: %ld\n", stbuf->st_atime, stbuf->st_mtime);
    ret = 0;
  } else {
    ret = -ENOENT;
  }
  pthread_rwlock_unlock(&rwlock);
  return ret;
}

static int chatfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
    off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
  pthread_rwlock_rdlock(&rwlock);
  struct directory *dir = find_directory(path);
  if (dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  filler(buf, ".", NULL, 0, 0);
  filler(buf, "..", NULL, 0, 0);
  struct file *file = dir->files;
  while (file != NULL) {
    filler(buf, file->name, NULL, 0, 0);
    file = file->next;
  }
  struct directory *subdir = dir->subdirectories;
  while (subdir != NULL) {
    filler(buf, subdir->name, NULL, 0, 0);
    subdir = subdir->next;
  }
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

// Create and open a file
static int chatfs_create(
    const char *path, mode_t mode, struct fuse_file_info *fi) {
  debug_print("create(%s, %d)\n", path, mode);
  pthread_rwlock_wrlock(&rwlock);
  struct split_result res = split(path);
  debug_print("  dir: %s, name: %s\n", res.dir, res.name);
  struct directory *dir = find_directory(res.dir);
  if (dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  struct file *file = find_file(dir, res.name);
  if (file != NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -EEXIST;
  }
  debug_print("  creating file %s\n", res.name);
  file = malloc(sizeof(struct file));
  file->name = strdup(res.name);
  file->mode = S_IFREG | mode;
  struct fuse_context *cxt = fuse_get_context();
  if (cxt) {
    file->uid = cxt->uid;
    file->gid = cxt->gid;
  } else {
    file->uid = getuid();
    file->gid = getgid();
  }
  file->atime = file->mtime = time(NULL);
  debug_print("  atime: %ld, mtime: %ld\n", file->atime, file->mtime);
  file->size = 0;
  file->data = strdup("");
  file->friend = NULL;
  add_file(dir, file);
  free(res.dir);
  free(res.name);
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_mkdir(const char *path, mode_t mode) {
  debug_print("mkdir(%s, %d)\n", path, mode);
  pthread_rwlock_wrlock(&rwlock);
  struct split_result res = split(path);
  struct directory *dir = find_directory(res.dir);
  if (dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  struct directory *subdir = find_subdirectory(dir, res.name);
  if (subdir != NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -EEXIST;
  }
  debug_print("  creating directory %s\n", res.name);
  subdir = malloc(sizeof(struct directory));
  subdir->name = strdup(res.name);
  subdir->mode = S_IFDIR | mode;
  struct fuse_context *cxt = fuse_get_context();
  if (cxt) {
    subdir->uid = cxt->uid;
    subdir->gid = cxt->gid;
  } else {
    subdir->uid = getuid();
    subdir->gid = getgid();
  }
  subdir->atime = subdir->mtime = time(NULL);
  subdir->files = NULL;
  subdir->subdirectories = NULL;
  add_subdirectory(dir, subdir);
  free(res.dir);
  free(res.name);
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_rmdir(const char *path) {
  debug_print("rmdir %s\n", path);
  pthread_rwlock_wrlock(&rwlock);
  struct split_result res = split(path);
  struct directory *dir = find_directory(res.dir);
  if (dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  struct directory *subdir = find_subdirectory(dir, res.name);
  if (subdir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  remove_subdirectory(dir, subdir);
  free(subdir->name);
  free(subdir);
  free(res.dir);
  free(res.name);
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_unlink(const char *path) {
  debug_print("unlink %s\n", path);
  pthread_rwlock_wrlock(&rwlock);
  struct split_result res = split(path);
  struct directory *dir = find_directory(res.dir);
  if (dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  struct file *file = find_file(dir, res.name);
  if (file == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  remove_file(dir, file);
  free(file->name);
  free(file->data);
  free(file);
  free(res.dir);
  free(res.name);
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_open(const char *path, struct fuse_file_info *fi) {
  debug_print("open %s\n", path);
  pthread_rwlock_wrlock(&rwlock);
  struct find_result res = find(path);
  if (res.file == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  // clear the file if flags include O_TRUNC
  if (fi->flags & O_TRUNC) {
    debug_print("  truncating file\n");
    free(res.file->data);
    res.file->data = strdup("");
    res.file->size = 0;
    res.file->mtime = time(NULL);
    if (res.file->friend != NULL) {
      send_message(res.file->friend, MSG_TYPE_CHAT);
    }
  }
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_read(const char *path, char *buf, size_t size, off_t offset,
    struct fuse_file_info *fi) {
  debug_print("read %s\n", path);
  pthread_rwlock_rdlock(&rwlock);
  struct find_result res = find(path);
  if (res.file == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  if (offset >= res.file->size) {
    pthread_rwlock_unlock(&rwlock);
    return 0;
  }
  if (offset + size > res.file->size) {
    size = res.file->size - offset;
  }
  memcpy(buf, res.file->data + offset, size);
  res.file->atime = time(NULL);
  pthread_rwlock_unlock(&rwlock);
  return size;
}

static int chatfs_write(const char *path, const char *buf, size_t size,
    off_t offset, struct fuse_file_info *fi) {
  debug_print("write %s\n", path);
  debug_print("  size: %ld, offset: %ld, flags: %d\n", size, offset, fi->flags);
  pthread_rwlock_wrlock(&rwlock);
  if (strcmp(path, "/connect") == 0) {
    debug_print("  connecting to %s\n", buf);
    if (connect_to(buf) != 0) {
      pthread_rwlock_unlock(&rwlock);
      return -ENOENT;
    }
  }
  struct find_result res = find(path);
  if (res.file == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  size_t new_size = offset + size;
  if (new_size > res.file->size) {
    res.file->data = realloc(res.file->data, new_size);
    res.file->size = new_size;
  }
  memcpy(res.file->data + offset, buf, size);
  res.file->atime = res.file->mtime = time(NULL);
  if (res.file->friend != NULL) {
    send_message(res.file->friend, MSG_TYPE_CHAT);
  }
  pthread_rwlock_unlock(&rwlock);
  return size;
}

static int chatfs_truncate(
    const char *path, off_t size, struct fuse_file_info *fi) {
  debug_print("truncate %s\n", path);
  pthread_rwlock_wrlock(&rwlock);
  struct find_result res = find(path);
  if (res.file == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  if (size > res.file->size) {
    res.file->data = realloc(res.file->data, size);
  }
  res.file->size = size;
  res.file->atime = res.file->mtime = time(NULL);
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_rename(const char *from, const char *to, unsigned int flags) {
  debug_print("rename %s -> %s (not implemented)\n", from, to);
  pthread_rwlock_wrlock(&rwlock);
  struct split_result from_res = split(from);
  struct split_result to_res = split(to);
  struct directory *from_dir = find_directory(from_res.dir);
  if (from_dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  struct directory *to_dir = find_directory(to_res.dir);
  if (to_dir == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  struct file *file = find_file(from_dir, from_res.name);
  if (file == NULL) {
    pthread_rwlock_unlock(&rwlock);
    return -ENOENT;
  }
  free(file->name);
  file->name = strdup(to_res.name);
  remove_file(from_dir, file);
  add_file(to_dir, file);
  free(from_res.dir);
  free(from_res.name);
  free(to_res.dir);
  free(to_res.name);
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

static int chatfs_chmod(
    const char *path, mode_t mode, struct fuse_file_info *fi) {
  pthread_rwlock_wrlock(&rwlock);
  struct find_result res = find(path);
  int ret;
  if (res.file != NULL) {
    res.file->mode = mode;
    ret = 0;
  } else if (res.dir != NULL) {
    res.dir->mode = mode;
    ret = 0;
  } else {
    ret = -ENOENT;
  }
  pthread_rwlock_unlock(&rwlock);
  return ret;
}

static int chatfs_chown(
    const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
  pthread_rwlock_wrlock(&rwlock);
  struct find_result res = find(path);
  int ret;
  if (res.file != NULL) {
    res.file->uid = uid;
    res.file->gid = gid;
    ret = 0;
  } else if (res.dir != NULL) {
    res.dir->uid = uid;
    res.dir->gid = gid;
    ret = 0;
  } else {
    ret = -ENOENT;
  }
  pthread_rwlock_unlock(&rwlock);
  return ret;
}

static int chatfs_utimens(
    const char *path, const struct timespec ts[2], struct fuse_file_info *fi) {
  debug_print("utimens not implemented\n");
  return 0;
}

// static int chatfs_statfs(const char *path, struct statvfs *stbuf) { return 0;
// }

// static int chatfs_flush(const char *path, struct fuse_file_info *fi) { return
// 0; }

// static int chatfs_release(const char *path, struct fuse_file_info *fi) {
//   return 0;
// }

// static int chatfs_fsync(const char *path, int isdatasync, struct
// fuse_file_info *fi) { return 0; }

// static int chatfs_setxattr(const char *path, const char *name, const char
// *value, size_t size, int flags) { return 0;
// }

// static int chatfs_getxattr(const char *path, const char *name, char *value,
// size_t size) { return 0; }

// static int chatfs_listxattr(const char *path, char *list, size_t size) {
// return 0; }

// static int chatfs_removexattr(const char *path, const char *name) { return 0;
// }

static struct fuse_operations chatfs_operations = {
    .init = chatfs_init,
    .getattr = chatfs_getattr,
    .readdir = chatfs_readdir,
    .mkdir = chatfs_mkdir,
    .rmdir = chatfs_rmdir,
    .create = chatfs_create,
    .unlink = chatfs_unlink,
    .open = chatfs_open,
    .read = chatfs_read,
    .write = chatfs_write,
    .truncate = chatfs_truncate,
    .rename = chatfs_rename,
    // .access = chatfs_access,
    .chmod = chatfs_chmod,
    .chown = chatfs_chown,
    .utimens = chatfs_utimens,
    // .statfs = chatfs_statfs,
    // .flush = chatfs_flush,
    // .release = chatfs_release,
    // .fsync = chatfs_fsync,
    // .setxattr = chatfs_setxattr,
    // .getxattr = chatfs_getxattr,
    // .listxattr = chatfs_listxattr,
    // .removexattr = chatfs_removexattr,
};

int main(int argc, char *argv[]) {
  return fuse_main(argc, argv, &chatfs_operations, NULL);
}
