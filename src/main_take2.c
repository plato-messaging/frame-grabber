
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, const char *argv[])
{

  int8_t *data = malloc(10 * sizeof(int8_t));
  int8_t *append = malloc(5 * sizeof(int8_t));
  int size_append = 5 * sizeof(int8_t);
  int size_data = 10 * sizeof(int8_t);
  int i;
  printf("Data : ");
  for (i = 0; i < 10; i++)
  {
    data[i] = i;
    printf("%d ", data[i]);
  }
  printf("\nAppending : ");
  for (i = 0; i < 5; i++)
  {
    append[i] = 10 - i;
    printf("%d ", append[i]);
  }
  data = realloc(data, 15 * sizeof(int8_t));
  memcpy(data + size_data, append, size_append);
  printf("\nAppended : ");
  for (i = 0; i < 15; i++)
  {
    printf("%d ", data[i]);
  }

  free(data);
  free(append);
  return 0;
}