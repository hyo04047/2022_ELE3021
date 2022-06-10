#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

char*
fmtname(char *path)
{
  static char buf[DIRSIZ+1];
  char *p;

  // Find first character after last slash.
  for(p=path+strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;

  // Return blank-padded name.
  if(strlen(p) >= DIRSIZ)
    return p;
  memmove(buf, p, strlen(p));
  memset(buf+strlen(p), ' ', DIRSIZ-strlen(p));
  return buf;
}

void
ls(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  char mode[8];

  if((fd = open(path, 0)) < 0){
    printf(2, "ls: cannot open %s\n", path);
    return;
  }

  if(fstat(fd, &st) < 0){
    printf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  switch(st.type){
  case T_FILE:
    for (int i = 0; i < 7; i++)
      mode[i] = '-';
    mode[7] = '\0';
    if (st.mode & MODE_RUSR)
      mode[1] = 'r';
    if (st.mode & MODE_WUSR)
      mode[2] = 'w';
    if (st.mode & MODE_XUSR)
      mode[3] = 'x';
    if (st.mode & MODE_ROTH)
      mode[4] = 'r';
    if (st.mode & MODE_WOTH)
      mode[5] = 'w';
    if (st.mode & MODE_XOTH)
      mode[6] = 'x';
    mode[0] = '-';
    printf(1, "%s %s %s %d %d %d\n", fmtname(path), st.id, mode, st.type, st.ino, st.size);
    break;

  case T_DIR:
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf){
      printf(1, "ls: path too long\n");
      break;
    }
    strcpy(buf, path);
    p = buf+strlen(buf);
    *p++ = '/';
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      if(stat(buf, &st) < 0){
        printf(1, "ls: cannot stat %s\n", buf);
        continue;
      }
      for (int i = 0; i < 7; i++)
        mode[i] = '-';
      mode[7] = '\0';
      if (st.mode & MODE_RUSR)
        mode[1] = 'r';
      if (st.mode & MODE_WUSR)
        mode[2] = 'w';
      if (st.mode & MODE_XUSR)
        mode[3] = 'x';
      if (st.mode & MODE_ROTH)
        mode[4] = 'r';
      if (st.mode & MODE_WOTH)
        mode[5] = 'w';
      if (st.mode & MODE_XOTH)
        mode[6] = 'x';
      if(st.type == T_FILE)
        mode[0] = '-';
      else if(st.type == T_DIR)
        mode[0] = 'd';
      printf(1, "%s %s %s %d %d %d\n", fmtname(buf), st.id, mode, st.type, st.ino, st.size);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  int i;

  if(argc < 2){
    ls(".");
    exit();
  }
  for(i=1; i<argc; i++)
    ls(argv[i]);
  exit();
}
