/*
 * from: https://github.com/quick2wire/quick2wire-gpio-admin
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/mman.h>

struct admin_def {
  const char *class;
  const char *object_prefix;
  const char **attrs;
};

extern struct admin_def def;

#ifdef AC_BUTTON
#include "ac_button.h"
#endif

#ifdef AC_DIMMER
#include "ac_dimmer.h"
#endif

static void usage_error(char **argv) {
  fprintf(stderr, "usage: %s {export|unexport} <gpio>\n", argv[0]);
  exit(1);
}

static void error(int status, int errnum, const char *format, ...) {
  fflush(stdout);

  va_list argp;
  va_start(argp, format);
  vfprintf(stderr, format, argp);
  va_end(argp);

  if(errnum) {
    fprintf(stderr, " : %s", strerror(errnum));
  }
  fprintf(stderr, "\n");
  if(status) {
    exit(status);
  }
}

static void allow_access_by_user(unsigned int pin, const char *attr_name) {
  char path[PATH_MAX];
  struct stat stat_data;

  int size = snprintf(path, PATH_MAX, "/sys/class/%s/%s%u/%s",
    def.class, def.object_prefix, pin, attr_name);

  if (size >= PATH_MAX) {
    error(7, 0, "path too long!");
  }

  if(stat(path, &stat_data)) {
    error(6, errno, "failed to get permissions of %s", path);
  }

  mode_t mode = 0;
  if(stat_data.st_mode & S_IRUSR) {
    mode |= (S_IRUSR | S_IRGRP | S_IROTH);
  }

  if(stat_data.st_mode & S_IWUSR) {
    mode |= (S_IWUSR | S_IWGRP | S_IWOTH);
  }

  if (chmod(path, mode) != 0) {
    error(6, errno, "failed to set permissions of %s", path);
  }
}

static unsigned int parse_gpio_pin(const char *pin_str) {
  char *endp;
  unsigned int pin;

  if (pin_str[0] == '\0') {
    error(2, 0, "empty string given for GPIO pin number");
  }

  pin = strtoul(pin_str, &endp, 0);

  if (*endp != '\0') {
    error(2, 0, "%s is not a valid GPIO pin number", pin_str);
  }

  return pin;
}

static void write_pin_to_export(const char *export, unsigned int pin) {
  char path[PATH_MAX];
  int size = snprintf(path, PATH_MAX, "/sys/class/%s/%s",
    def.class, export);

  if (size >= PATH_MAX) {
    error(7, 0, "path too long!");
  }

  FILE * out = fopen(path, "w");

  if (out == NULL) {
    error(3, errno, "could not open %s", path);
  }

  if (fprintf(out, "%u\n", pin) < 0) {
    error(4, errno, "could not write GPIO pin number to %s", path);
  }

  if (fclose(out) == EOF) {
    error(4, errno, "could not flush data to %s", path);
  }
}

int main(int argc, char **argv) {
  if (argc != 3) {
    usage_error(argv);
  }

  const char *command = argv[1];
  const char *pin_str = argv[2];

  unsigned int pin = parse_gpio_pin(pin_str);

  if (strcmp(command, "export") == 0) {
    write_pin_to_export("export", pin);
    for(const char **attr = def.attrs; *attr; ++attr) {
      allow_access_by_user(pin, *attr);
    }
  }
  else if (strcmp(command, "unexport") == 0) {
    write_pin_to_export("unexport", pin);
  }
  else {
    usage_error(argv);
  }

  return 0;
}