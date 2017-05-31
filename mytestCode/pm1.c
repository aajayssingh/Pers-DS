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

typedef struct state_data {
  char data[100];
  uint16_t count;
  struct state_data *next;
} state_data;

const char *test_data = "test";
const char *test_data1 = "tes1";

int main(int argc, const char *argv[]) {
  int fd = open("test.mm", O_RDWR|O_CREAT|O_APPEND, (mode_t)0700);
  if (fd < 0) {
    perror("Unable to open file 'test.mm'");
    exit(1);
  }
  size_t data_length = sizeof(state_data);
  if (ftruncate(fd, data_length) < 0) {
    perror("Unable to truncate file 'test.mm'");
    exit(1);
  }
  state_data *data = (state_data *)mmap(NULL, data_length*2, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0);
  if (MAP_FAILED == data) {
    perror("Unable to mmap file 'test.mm'");
    close(fd);
    exit(1);
  }
  memset(data, 0, data_length*2);

  state_data *data1 = (state_data *)mmap(NULL, data_length, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, fd, 0);
  if (MAP_FAILED == data) {
    perror("Unable to mmap file 'test.mm'");
    close(fd);
    exit(1);
  }
  
  memset(data1, 0, data_length);
  for (data->count = 0; data->count < 5; ++data->count) {
    data->data[data->count] = test_data[data->count];
    data1->data[data1->count] = test_data1[data1->count];

  }


  data->next = data1;
 // kill(getpid(), 9);
}
