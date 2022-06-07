#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/buffer_cache.h"
#include "threads/thread.h"
#include "filesys/dentry_cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");
  bc_init();
  dc_init();
  inode_init ();
  free_map_init ();
  if (format) 
    do_format ();
  free_map_open ();
  thread_current()->dir = dir_open_root();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  bc_term();
  dc_destroy (&dentry_cache);
  dir_close(thread_current()->dir);
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  //printf("start filesys_create\n");
  block_sector_t inode_sector = 0;
  struct inode *inode = NULL;
  char cp_name[512], file_name[512];
  bool hit = false;
  struct dir *dir = NULL;
  int i;

  /* dentry_cache는 절대 경로일 때만 사용된다. */
  if (name[0] == '/' && strlen (name) != 1)
  {
    /* dentry_cache에 path를 member로 가진 dce가 있는지 확인. */
    struct dc_entry *dce = find_dce (name);

    /* 이미 존재한다면, create 불가능. */
    if (dce != NULL)
    {
      //printf("already existed file\n");
      return false;
    }

    /* 존재하지 않는다면, parent directory까지의 path를 member로 가진 dce가 있는지 확인. */
    char *parent_path = (char *) malloc (strlen (name) + 1);
    int check_point;

    for (check_point = strlen (name) - 1; check_point >= 0; check_point--)
    {
      if (name[check_point] == '/')
        break;
    }

    if (check_point == 0)
    {
      //printf("parent directory is root\n");
      free (parent_path);
      dir = dir_open_root ();

      for (i = check_point + 1; i < strlen (name) + 1; i++)
        file_name[i - check_point - 1] = name[i];

      hit = true;
    }
    else
    {
      strlcpy (parent_path, name, check_point + 1);

      //printf("parent path %s\n", parent_path);

      for (i = check_point + 1; i < strlen (name) + 1; i++)
        file_name[i - check_point - 1] = name[i];

      //printf("file_name %s\n", file_name);
      
      dce = find_dce (parent_path);
      free (parent_path);

      /* 존재한다면, 해당 dce의 inumber를 dir의 inode_sector로 설정. */
      if (dce != NULL)
      {
        //printf("cache hit!\n");
        dir = dir_open (inode_open (dce->inumber));
        hit = true;
      }
    }
  }
  
  if (hit == false)
  {
    strlcpy(cp_name, name, strlen(name) + 1) ;
    dir = parse_path(cp_name, file_name);
    if(dir == NULL)
    return false;
  }
  //printf("abc:%s\n", cp_name);
  //printf("dir:%s, %d\n", file_name, inode_is_dir(dir_get_inode(dir)));
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, 0)
                  && dir_add (dir, file_name, inode_sector));               
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  /* dentry cache에 create한 file에 대한 정보를 insert. */
  if (name[0] == '/')
  {
    //printf("cache insert\n");
    struct dc_entry *new_dce = (struct dc_entry *) malloc (sizeof (struct dc_entry));

    new_dce->absolute_path = (char *) malloc (strlen (name) + 1);
    strlcpy (new_dce->absolute_path, name, strlen(name) + 1);
    new_dce->inumber = inode_sector;

    insert_dce (&dentry_cache, new_dce);
  }

  dir_close (dir); 
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  int i;
  char cp_name[512], file_name[512], n[NAME_MAX + 1];
  struct inode *inode = NULL;
  bool hit = false;
  struct dir *dir = NULL;

  /* dentry_cache는 절대 경로일 때만 사용된다. */
  if (name[0] == '/' && strlen (name) != 1)
  {
    /* dentry_cache에 path를 member로 가진 dce가 있는지 확인. */
    struct dc_entry *dce = find_dce (name);

    /* 존재한다면, 바로 open. */
    if (dce != NULL)
    {
      //printf("cache hit!\n");
      return file_open (inode_open (dce->inumber));
    }

    /* 존재하지 않는다면, parent directory까지의 path를 member로 가진 dce가 있는지 확인. */
    char *parent_path = (char *) malloc (strlen (name) + 1);
    int check_point;

    for (check_point = strlen (name) - 1; check_point >= 0; check_point--)
    {
      if (name[check_point] == '/')
        break;
    }

    if (check_point == 0)
    {
      //printf("parent directory is root\n");
      free (parent_path);
      dir = dir_open_root ();
      for (i = check_point + 1; i < strlen (name) + 1; i++)
        file_name[i - check_point - 1] = name[i];

      hit = true;
    }
    else
    {
      strlcpy (parent_path, name, check_point + 1);

      //printf("parent path %s\n", parent_path);

      for (i = check_point + 1; i < strlen (name) + 1; i++)
        file_name[i - check_point - 1] = name[i];

      //printf("file_name %s\n", file_name);
      
      dce = find_dce (parent_path);
      free (parent_path);

      /* 존재한다면, 해당 dce의 inumber를 dir의 inode_sector로 설정. */
      if (dce != NULL)
      {
        //printf("cache hit!\n");
        dir = dir_open (inode_open (dce->inumber));
        hit = true;
      }
    }
  }

  if (hit == false)
  {
    strlcpy(cp_name, name, strlen(name) + 1);
    dir = parse_path(cp_name, file_name);

        //printf("file:%s\n", file_name);
    if(dir == NULL)
    return NULL;
  }

  if(!strcmp(file_name,".")){
      return file_open(dir_get_inode(dir)); 
  }
    //printf("sec:%d\n",inode_get_inumber(dir_get_inode(dir)));
  
  if(dir == NULL)
  return NULL;
  if(*file_name == '\0')
  inode =dir_get_inode(dir); 
  else{
    //printf("sec:%d\n",inode_get_inumber(dir_get_inode(dir)));
    dir_lookup (dir, file_name, &inode);
    
    //printf("inode%d\n",inode_get_inumber(inode));
  }

  /* dentry cache에 open한 file에 대한 정보를 insert. */
  if (name[0] == '/' && inode != NULL)
  {
    //printf("cache insert\n");
    struct dc_entry *new_dce = (struct dc_entry *) malloc (sizeof (struct dc_entry));

    new_dce->absolute_path = (char *) malloc (strlen (name) + 1);
    strlcpy (new_dce->absolute_path, name, strlen(name) + 1);
    new_dce->inumber = inode_get_inumber (inode);

    insert_dce (&dentry_cache, new_dce);
  }

  dir_close (dir);
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  int i;
  struct dir *dir = NULL;
  char cp_name[128], file_name[128], n[NAME_MAX + 1];
  bool success = false;
  bool hit = false;

  /* dentry_cache는 절대 경로일 때만 사용된다. */
  if (name[0] == '/' && strlen (name) != 1)
  {
    /* dentry_cache에 path를 member로 가진 dce가 있는지 확인. */
    struct dc_entry *dce = find_dce (name);
    char *parent_path = (char *) malloc (strlen (name) + 1);
    int check_point;

    for (check_point = strlen (name) - 1; check_point >= 0; check_point--)
    {
      if (name[check_point] == '/')
        break;
    }
    
    for (i = check_point + 1; i < strlen (name) + 1; i++)
      file_name[i - check_point - 1] = name[i];

    /* 존재한다면, dir을 parent dir로 변경. */
    if (dce != NULL)
    {
      if (check_point == 0)
      {
        free (parent_path);
        dir = dir_open_root ();
        hit = true;
      }
      else
      {
        strlcpy (parent_path, name, check_point + 1);
        dce = find_dce (parent_path);
        free (parent_path);
        dir = dir_open (inode_open (dce->inumber));
        hit = true;
      }
    }
    else
      return false;
  }

  if (hit == false)
  {
    strlcpy(cp_name, name, strlen(name) + 1);
    dir = parse_path(cp_name, file_name);
    if(dir == NULL || file_name == NULL)
    return false;
  }

  struct dir *sub;
  struct inode *inode = NULL;
  
  dir_lookup(thread_current()->dir, "..", &inode);
  sub = dir_open(inode);
  
  if(dir_lookup(sub, file_name, &inode) && inode_get_inumber(inode) == inode_get_inumber(dir_get_inode(thread_current()->dir)))
  {  // 현재 디렉토리 삭제

     if(!dir_readdir(thread_current()->dir, n))
     {     //dir 비었음
      dir_close(thread_current()->dir);
      thread_current()->dir = NULL;
      success = dir_remove(sub, file_name);
      if (name[0] == '/')
      {
        //printf("cache remove\n");
        struct dc_entry *removed_dce = find_dce (name);
        delete_dce (&dentry_cache, removed_dce);
      }
      dir_close(sub);
      return success;
    }    
  }
  else
    dir_close(sub);

  
  if(!dir_lookup(dir, file_name, &inode))
  {  //dir에 파일 존재
    dir_close(dir);
    return false;
  }

  sub = dir_open(inode);
  if(inode_is_dir(inode)){   //file_name을 가진 entry가 디렉터리
    if(!dir_readdir(sub, n)){  //이 디렉터리가 비었으면 삭제
        dir_close(sub);
     success = dir_remove(dir, file_name);
     if (name[0] == '/')
      {
        //printf("cache remove\n");
        struct dc_entry *removed_dce = find_dce (name);
        delete_dce (&dentry_cache, removed_dce);
      }
    }
    else
    dir_close(sub); 
  }
  else{
      dir_close(sub);
  success = dir_remove (dir, file_name);
  if (name[0] == '/')
      {
        //printf("cache remove\n");
        struct dc_entry *removed_dce = find_dce (name);
        delete_dce (&dentry_cache, removed_dce);
      }
  }

  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!filesys_create_dir(NULL))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

struct dir* parse_path(char *path_name, char *file_name){
  if(thread_current()->dir == NULL)
  return NULL;
  struct dir *dir = dir_reopen(thread_current()->dir);
  struct inode *inode = NULL;
  if(path_name == NULL || file_name == NULL)
  return NULL;
  if(strlen(path_name) == 0)
  return NULL;
  char *token, *nextToken, *savePtr;
  token = strtok_r(path_name, "/", &savePtr);

  nextToken = strtok_r(NULL, "/", &savePtr);
  //printf("token:%s\n", token);
  //printf("nextToken:%s\n", nextToken);
  while(token != NULL && nextToken != NULL){
  dir_lookup(dir, token, &inode);
  if(inode == NULL)
  return NULL;
  if(!inode_is_dir(inode)){
  inode_close(inode);
  return NULL;
  }
  dir_close(dir);
  dir = dir_open(inode);
  token = nextToken;
  nextToken = strtok_r(NULL, "/", &savePtr);
  }
  if(token == NULL)
  strlcpy(file_name, "", 1);  
  else
  strlcpy(file_name, token, strlen(token) + 1);

  return dir;
}

bool filesys_create_dir(const char *name){
  int i;
  bool success = false;
  block_sector_t inode_sector, inode_sector1, inode_sector2, inode_sector3;
  char cp_name[512], file_name[512], dname[15];
  struct dir *dir = NULL, *sub, *sub1, *sub2, *sub3;
  struct inode *inode = NULL, *inode1;
  bool hit = false;

  if(name == NULL){
    success = (dir_create (ROOT_DIR_SECTOR, 16)
    && (inode = inode_open(ROOT_DIR_SECTOR)) != NULL
    && (dir = dir_open(inode)) != NULL
    && dir_add (dir, ".", ROOT_DIR_SECTOR)
    && dir_add (dir, "..", ROOT_DIR_SECTOR));
    dir_close(dir);

    return success;
  }

  /* dentry_cache는 절대 경로일 때만 사용된다. */
  if (name[0] == '/' && strlen (name) != 1)
  {
    /* dentry_cache에 path를 member로 가진 dce가 있는지 확인. */
    struct dc_entry *dce = find_dce (name);

    /* 이미 존재한다면, create 불가능. */
    if (dce != NULL)
    {
      //printf("already existed file, %s\n", dce->absolute_path);
      return false;
    }

    /* 존재하지 않는다면, parent directory까지의 path를 member로 가진 dce가 있는지 확인. */
    char *parent_path = (char *) malloc (strlen (name) + 1);
    int check_point;

    for (check_point = strlen (name) - 1; check_point >= 0; check_point--)
    {
      if (name[check_point] == '/')
        break;
    }

    if (check_point == 0)
    {
      //printf("parent directory is root\n");
      free (parent_path);
      dir = dir_open_root ();

      for (i = check_point + 1; i < strlen (name) + 1; i++)
        file_name[i - check_point - 1] = name[i];

      hit = true;
    }
    else
    {
      strlcpy (parent_path, name, check_point + 1);

      //printf("parent path %s\n", parent_path);

      for (i = check_point + 1; i < strlen (name) + 1; i++)
        file_name[i - check_point - 1] = name[i];

      //printf("dir_name %s\n", dir_name);
      
      dce = find_dce (parent_path);
      free (parent_path);

      /* 존재한다면, 해당 dce의 inumber를 dir의 inode_sector로 설정. */
      if (dce != NULL)
      {
        //printf("cache hit!\n");
        dir = dir_open (inode_open (dce->inumber));
        hit = true;
      }
    }
  }

  if (hit == false)
  {
    strlcpy(cp_name, name, strlen(name) + 1);
    dir = parse_path(cp_name, file_name);
    if(dir == NULL)
    return NULL;
    dir_lookup(dir, file_name, &inode);
    if(inode != NULL){  // 이미 file_name을 가진 entry가 존재
      inode_close(inode);
    dir_close(dir); 
    return false;
    }
  }
       success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_add (dir, file_name, inode_sector)
                  && dir_create (inode_sector, 16)
                  && (inode = inode_open(inode_sector)) != NULL
                  && (sub = dir_open(inode)) != NULL
                  && dir_add (sub, ".", inode_sector)
                  && dir_add (sub, "..", inode_get_inumber(dir_get_inode(dir))));       

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

    /* dentry cache에 create한 file에 대한 정보를 insert. */
  if (name[0] == '/')
  {
    //printf("insert %s\n", path);
    struct dc_entry *new_dce = (struct dc_entry *) malloc (sizeof (struct dc_entry));
    
    new_dce->absolute_path = (char *) malloc (strlen (name) + 1);
    strlcpy (new_dce->absolute_path, name, strlen(name) + 1);
    new_dce->inumber = inode_sector;

    insert_dce (&dentry_cache, new_dce);
  }

  dir_close (dir); 
  dir_close (sub);
  return success;
}