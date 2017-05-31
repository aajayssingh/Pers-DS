#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

typedef struct {
  char data[100];
  uint16_t count;
} state_data;

const char *test_data = "test";

int main(int argc, const char *argv[]) {
  int fd = open("test.mm", O_RDONLY);
  if (fd < 0) {
    perror("Unable to open file 'test.mm'");
    exit(1);
  }
  size_t data_length = sizeof(state_data);
  state_data *data = (state_data *)mmap(NULL, data_length, PROT_READ, MAP_SHARED|MAP_POPULATE, fd, 0);
  if (MAP_FAILED == data) {
    perror("Unable to mmap file 'test.mm'");
    close(fd);
    exit(1);
  }
  assert(5 == data->count);
  unsigned index;
  for (index = 0; index < 4; ++index) {
    assert(test_data[index] == data->data[index]);
  }
  printf("Validated\n");
}
