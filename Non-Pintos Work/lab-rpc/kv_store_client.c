/**
 * Client binary.
 */

#include "kv_store_client.h"
#include <stdio.h>

#define HOST "localhost"

CLIENT* clnt_connect(char* host) {
  CLIENT* clnt = clnt_create(host, KVSTORE, KVSTORE_V1, "udp");
  if (clnt == NULL) {
    clnt_pcreateerror(host);
    exit(1);
  }
  return clnt;
}

int example(int input) {
  CLIENT *clnt = clnt_connect(HOST);

  int ret;
  int *result;

  result = example_1(&input, clnt);
  if (result == (int *)NULL) {
    clnt_perror(clnt, "call failed");
    exit(1);
  }
  ret = *result;
  xdr_free((xdrproc_t)xdr_int, (char *)result);

  clnt_destroy(clnt);
  
  return ret;
}

char* echo(char* input) {
  CLIENT *clnt = clnt_connect(HOST);

  char* ret;
  char **result;

  result = echo_1(&input, clnt);
  if (result == (char **)NULL) {
    clnt_perror(clnt, "call failed");
    exit(1);
  }
  ret = strdup(*result);
  xdr_free((xdrproc_t)xdr_int, (char *)result);

  clnt_destroy(clnt);
  
  return ret;
}

void put(buf key, buf value) {
  CLIENT *clnt = clnt_connect(HOST);
  void *result;


  char* new = (char*) malloc(key.buf_len + value.buf_len + 2);
  strcpy(new, key.buf_val);
  strcat(new, "|");
  strcat(new, value.buf_val);

  put_request input;

  input.put_request_len = key.buf_len + value.buf_len + 1;
  input.put_request_val = new;

  result = put_1(&input, clnt);
  xdr_free((xdrproc_t)xdr_int, (char *)result);

  clnt_destroy(clnt);
  free(new);
  
  return;
}

buf* get(buf key) {

  CLIENT *clnt = clnt_connect(HOST);

  buf* ret = (buf*) malloc(sizeof(buf));


  buf *result;

  result = get_1(&key, clnt);

  ret->buf_val = (char*) malloc(sizeof(char) * (result->buf_len + 1));

  if (result->buf_len > 0){
    strcpy(ret->buf_val, result->buf_val);
  }

  ret->buf_len = result->buf_len;

  xdr_free((xdrproc_t)xdr_int, (char *)result);

  clnt_destroy(clnt);

  return ret;
}
