//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

#define NUM_USER 10

struct
{
  struct spinlock lock;
  struct inode *ip;
  char user[NUM_USER][2][16];
  int online[NUM_USER];
} utable;

int
accountcheck(char* id, char* pw)
{
  acquire(&utable.lock);
  for(int i = 0; i < NUM_USER; i++){
    if(strncmp(utable.user[i][0], id, 16) == 0 && strncmp(utable.user[i][1], pw, 16) == 0){
      utable.online[i] = 1;
      release(&utable.lock);
      return 0;
    }
  }
  release(&utable.lock);
  return -1;
}

int
usysinit(void)
{
  initlock(&utable.lock, "utable");
  acquire(&utable.lock);
  for(int i = 0; i < NUM_USER; i++){
    for(int j = 0; j < 16; j++){
      utable.user[i][0][j] = '\0';
      utable.user[i][1][j] = '\0';
      utable.online[i] = 0;
    }
  }
  release(&utable.lock);

  begin_op();
  char *pathname = "./useraccount";
  char name[DIRSIZ];
  struct inode *ip, *dp;

  if((dp = nameiparent(pathname, name)) == 0)
    return -1;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) == 0){
    acquire(&utable.lock);
    strncpy(utable.user[0][0], "root", 16);
    strncpy(utable.user[0][1], "0000", 16);
    utable.online[0] = 1;
    release(&utable.lock);

    if((ip = ialloc(dp->dev, T_FILE)) == 0){
      panic("create: ialloc");
    }
    ilock(ip);
    ip->major = 0;
    ip->minor = 0;
    ip->nlink = 1;
    strncpy(ip->user, "root", 16);
    iupdate(ip);

    if((dirlink(dp, name, ip->inum)) < 0)
      panic("create: dirlink");

    if(writei(ip, utable.user[0][0], 0, sizeof(utable.user)) < 0){
      cprintf("writei error\n");
      iunlock(ip);
      end_op();
      return -1;
    }
    iupdate(dp);
    iupdate(ip);
    iunlockput(dp);
    iunlock(ip);
  }
  else{
    iunlockput(dp);
    ilock(ip);
    if((ip = namei(pathname)) == 0){
      iunlock(ip);
      end_op();
      return -1;
    }
    if(readi(ip, utable.user[0][0], 0, sizeof(utable.user)) < 0){
      iunlock(ip);
      end_op();
      return -1;
    }
    iunlockput(ip);
    acquire(&utable.lock);
    strncpy(utable.user[0][0], "root", 16);
    strncpy(utable.user[0][1], "0000", 16);
    release(&utable.lock);
  }

  acquire(&utable.lock);
  utable.ip = ip;
  release(&utable.lock);

  end_op();
  return 0;
}

int
addUser(char* username, char* password)
{
  if(strncmp(myproc()->id, "root", 16))
    return -1;

  acquire(&utable.lock);
  for(int i = 0; i < NUM_USER; i++){
    if(!strncmp(utable.user[i][0], username, 16)){
      release(&utable.lock);
      return -1;
    }
  }

  int i;
  for(i = 0; i < NUM_USER; i++){
    if(utable.online[i] == 0)
      break;
  }
  if(i == NUM_USER){
    release(&utable.lock);
    return -1;
  }
  strncpy(utable.user[i][0], username, 16);
  strncpy(utable.user[i][1], password, 16);
  utable.online[i] = 1;
  release(&utable.lock);

  begin_op();
  ilock(utable.ip);
  writei(utable.ip, utable.user[0][0], 0, sizeof(utable.user));
  iunlock(utable.ip);

  struct inode *ip, *dp;
  if((dp = namei("/")) == 0){
    end_op();
    return -1;
  }

  if((ip = dirlookup(dp, username, 0)) == 0){
    if((ip = ialloc(dp->dev, T_DIR)) == 0)
      panic("create: ialloc");
    ilock(dp);
    ilock(ip);
    ip->major = 0;
    ip->minor = 0;
    ip->nlink = 1;
    strncpy(ip->user, username, 16);
    iupdate(ip);

    dp->nlink++;
    iupdate(dp);
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
    if(dirlink(dp, username, ip->inum) < 0)
      panic("create: dirlink");

    iupdate(ip);
    iupdate(dp);
    iunlockput(ip);
    iunlockput(dp);
  }
  end_op();
  return 0;
}

int
deleteUser(char *username)
{
  struct proc *curproc = myproc();
  if(strncmp(curproc->id, "root", 16))
    return -1;
  if(!strncmp(username, "root", 16))
    return -1;

  acquire(&utable.lock);
  for(int i = 0; i < NUM_USER; i++){
    if(!strncmp(utable.user[i][0], username, 16)){
      for(int j = 0; j < 16; j++){
        utable.user[i][0][j] = '\0';
        utable.user[i][1][j] = '\0';
      }
      utable.online[i] = 0;
      release(&utable.lock);

      begin_op();
      ilock(utable.ip);
      if(writei(utable.ip, utable.user[0][0], 0, sizeof(utable.user)) < 0){
        end_op();
        return -1;
      }
      iunlock(utable.ip);
      end_op();
      return 0;
    }
  }
  release(&utable.lock);
  return -1;
}

int
chmod(char* pathname, int mode)
{
  struct inode *ip;

  begin_op();
  if((ip = namei(pathname)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type != T_DEV){
    if((strncmp(myproc()->id, "root", 16) != 0) && (strncmp(myproc()->id, ip->user, 16) != 0)){
      iunlock(ip);
      end_op();
      return -1;
    }
  }
  if(ip->type == T_DEV){
    iunlock(ip);
    end_op();
    return -1;
  }

  ip->mode = mode;
  iupdate(ip);
  iunlock(ip);
  end_op();

  return 0;
}

int 
sys_usysinit(void)
{
  return usysinit();
}

int 
sys_addUser(void)
{
  char *id, *pw;
  if (argstr(0, &id) < 0 || argstr(1, &pw) < 0)
    return -1;
  return addUser(id, pw);
}

int 
sys_deleteUser(void)
{
  char *id;
  if (argstr(0, &id) < 0)
    return -1;

  return deleteUser(id);
}

int
sys_accountcheck(void)
{
  char *id, *pw;
  if(argstr(0, &id) < 0 || argstr(1, &pw) < 0)
    return -1;
  return accountcheck(id, pw);
}

int
sys_chmod(void)
{
  char *pathname;
  int mode;

  if(argstr(0, &pathname) < 0 || argint(1, &mode) < 0)
    return -1;
  
  return chmod(pathname, mode);
}

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *curproc = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd] == 0){
      curproc->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

int
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

int
sys_read(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return fileread(f, p, n);
}

int
sys_write(void)
{
  struct file *f;
  int n;
  char *p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argptr(1, &p, n) < 0)
    return -1;
  return filewrite(f, p, n);
}

int
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

int
sys_fstat(void)
{
  struct file *f;
  struct stat *st;

  if(argfd(0, 0, &f) < 0 || argptr(1, (void*)&st, sizeof(*st)) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
int
sys_link(void)
{
  char name[DIRSIZ], *new, *old;
  struct inode *dp, *ip;

  if(argstr(0, &old) < 0 || argstr(1, &new) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

//PAGEBREAK!
int
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], *path;
  uint off;

  if(argstr(0, &path) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);
  if(dp->type != T_DEV){
    if(!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, dp->user, 16)){
      if(!(dp->mode & MODE_WUSR))
        goto bad;
    }
    else{
      if(!(dp->mode & MODE_WOTH))
        goto bad;
    }
  }
  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;
  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(ip->type != T_DEV){
      if(!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, ip->user, 16)){
        if(!(ip->mode & MODE_WUSR)){
          iunlockput(ip);
          return 0;
        }
      }
      else{
        if(!(ip->mode & MODE_WOTH)){
          iunlockput(ip);
          return 0;
        }
      }
    }
    if(type == T_FILE && ip->type == T_FILE)
      return ip;
    iunlockput(ip);
    return 0;
  }
  if(dp->type != T_DEV){
    if(!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, dp->user, 16)){
      if(!(dp->mode & MODE_WUSR)){
        iunlockput(dp);
        return 0; 
      }
    }
    else{
      if(!(dp->mode & MODE_WOTH)){
        iunlockput(dp);
        return 0;
      }
    }
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  strncpy(ip->user, myproc()->id, sizeof(myproc()->id));
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

int
sys_open(void)
{
  char *path;
  int fd, omode;
  struct file *f;
  struct inode *ip;

  if(argstr(0, &path) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type != T_DEV){
      if(omode == O_RDONLY){
        if (!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, ip->user, 16)){
          if(!(ip->mode & MODE_RUSR)){
            iunlockput(ip);
            end_op();
            return -1;
          }
        }
        else{
          if(!(ip->mode & MODE_ROTH)){
            iunlockput(ip);
            end_op();
            return -1;
          }
        }
      }
      else if(omode == O_WRONLY){
        if(!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, ip->user, 16)){
          if(!(ip->mode & MODE_WUSR)){
            iunlockput(ip);
            end_op();
            return -1;
          }
        }
        else{
          if(!(ip->mode & MODE_WOTH)){
            iunlockput(ip);
            end_op();
            return -1;
          }
        }
      }
      else if(omode == O_RDWR){
        if(!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, ip->user, 16)){
          if(!(ip->mode & MODE_WUSR && ip->mode & MODE_RUSR)){
            iunlockput(ip);
            end_op();
            return -1;
          }
        }
        else{
          if(!(ip->mode & MODE_WOTH && ip->mode & MODE_ROTH)){
            iunlockput(ip);
            end_op();
            return -1;
          }
        }
      }
    }

    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  end_op();

  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  return fd;
}

int
sys_mkdir(void)
{
  char *path;
  struct inode *ip;

  begin_op();
  if(argstr(0, &path) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_mknod(void)
{
  struct inode *ip;
  char *path;
  int major, minor;

  begin_op();
  if((argstr(0, &path)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEV, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

int
sys_chdir(void)
{
  char *path;
  struct inode *ip;
  struct proc *curproc = myproc();
  
  begin_op();
  if(argstr(0, &path) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DEV){
    if(!strncmp(myproc()->id, "root", 16) || !strncmp(myproc()->id, ip->user, 16)){
      if(!(ip->mode & MODE_XUSR)){
        iunlockput(ip);
        end_op();
        return -1;
      }
    }
    else{
      if(!(ip->mode & MODE_XOTH)){
        iunlockput(ip);
        end_op();
        return -1;
      }
    }
  }
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(curproc->cwd);
  end_op();
  curproc->cwd = ip;
  return 0;
}

int
sys_exec(void)
{
  char *path, *argv[MAXARG];
  int i;
  uint uargv, uarg;

  if(argstr(0, &path) < 0 || argint(1, (int*)&uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv))
      return -1;
    if(fetchint(uargv+4*i, (int*)&uarg) < 0)
      return -1;
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    if(fetchstr(uarg, &argv[i]) < 0)
      return -1;
  }
  return exec(path, argv);
}

int
sys_pipe(void)
{
  int *fd;
  struct file *rf, *wf;
  int fd0, fd1;

  if(argptr(0, (void*)&fd, 2*sizeof(fd[0])) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      myproc()->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  fd[0] = fd0;
  fd[1] = fd1;
  return 0;
}
