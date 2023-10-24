/*
 in the output of "declare -f", we get fn defs like this:

 another ()
 {
   st -e bash > /dev/null 2>&1 & cd_func $(pwd) &
 }

 this script is meant to parse out these functions into their own files, so
 that they can be put in the system PATH rather than coupled to a specific
 shell.

 it will overwrite the directory automatically each time it generates files,
 they're meant to be copied out of the output path.
*/

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CMP(a, b) (strncmp(a, b, strlen(b)) == 0)

#define PERROR_REGARDING(fn_lit, perror_file_path)                             \
  {                                                                            \
    perror(fn_lit);                                                            \
    fprintf(stderr, "(error regarding: %s)\n", perror_file_path);              \
  }

void rm_rf(char const *dirpath) {
  DIR *d = opendir(dirpath);
  if (d) {
    struct dirent *de;
    while ((de = readdir(d))) {
      if (CMP(de->d_name, ".") || CMP(de->d_name, "..")) {
        continue;
      }

      char path_buf[256];
      snprintf(path_buf, 256, "%s/%s", dirpath, de->d_name);

      if (de->d_type == DT_DIR) {
        rm_rf(path_buf);
      } else {
        remove(path_buf);
      }
    }
  } else {
    fprintf(stderr, "Could not open '%s' for recursive removal.\n", dirpath);
    return;
  }
  if (rmdir(dirpath)) {
    PERROR_REGARDING("rmdir", dirpath);
  }
  closedir(d);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <path/to/functions.file>\n", argv[0]);
    return 1;
  }

  char const *filepath = argv[1];

  char output_path[64];
  { // make a unique directory to put the output files into.
    // get the ptr to the last /
    char const *basename = strrchr(filepath, '/');
    if (basename) {
      basename++; // bump past the slash if it exists.
    } else {
      basename = filepath;
    }

    // make sure there's an output directory to shove function files into.
    snprintf(output_path, 64, "fn_output_%s", basename);
    // directory needs execute permissions to cd into it? really weird.
#define MAKE_OUTPUT_DIR() (mkdir(output_path, 0755))
    if (MAKE_OUTPUT_DIR() == -1) {
      rm_rf(output_path);
      if (MAKE_OUTPUT_DIR() == -1) {
        perror("mkdir");
        fprintf(stderr, "Could not regenerate the output directory '%s'.\n",
                output_path);
        return 1;
      }
    }
#undef MAKE_OUTPUT_DIR
  }

  int input_fd = open(filepath, O_RDONLY, 0644);

  struct stat my_stat;
  if (fstat(input_fd, &my_stat) == -1) {
    PERROR_REGARDING("fstat", filepath);
    return 1;
  }

  int len = my_stat.st_size;

  printf("Parsing file of length '%d' and writing into output path '%s'.\n",
         len, output_path);

  char buf[len + 1];
  read(input_fd, buf, len);
  buf[len] = '\0';

  // line by line parsing routine. go to next newline and null-terminate it,
  // keep going until we can't anymore.
  char *line_base = buf;
  char *newline;

  int curr_fn_file = -1;
  while ((newline = strchr(line_base, '\n'))) {
    newline[0] = '\0';
    // now we can treat line_base like its own standalone line.
    char *left_paren = strchr(line_base, '(');

    //// if the byte layout of your shell is different, view it using this
    //// printout and adjust the memcmp's accordingly. my /bin/bash has a
    //// trailing ' ' then '\0'. also, for some reason only the } line doesn't
    //// have a trailing space?
    //     if (left_paren) {
    //       for (int i = 0; i < 4; i++) {
    //         printf("0x%02X (%c) ", left_paren[i], left_paren[i]);
    //       }
    //       printf("\n");
    //     }

    //     for (int i = 0; i < 4; i++) {
    //       printf("0x%02X (%c) ", line_base[i], line_base[i]);
    //     }
    //     printf("\n");

    if (left_paren && (memcmp(left_paren, "() \0", 4) ==
                       0)) { // we're parsing a function header.
      char *space = strchr(line_base, ' ');
      if (space) {
        // then parse out the file name and open a new function file.
        space[0] = '\0';
        char const *fn_name = line_base;
        char fn_name_path_buf[128];
        snprintf(fn_name_path_buf, 128, "%s/%s", output_path, fn_name);
        curr_fn_file = creat(fn_name_path_buf, 0755);
      }
    } else if ((memcmp(line_base, "{ \0", 2) == 0) ||
               (memcmp(line_base, "}\0", 2) == 0)) {
      // pass, these lines are output filler.
    } else { // actual function data, dump it into the active file descriptor.
      if (curr_fn_file != -1) {
        write(curr_fn_file, line_base, strlen(line_base));
        write(curr_fn_file, "\n", 1);
      }
    }

    line_base = newline + 1;
  }

  printf("Finished. Your files are in directory '%s'.\n", output_path);

  return 0;
}
