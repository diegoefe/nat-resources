#ifndef MY_UTIL_H_INCLUIDO
#define MY_UTIL_H_INCLUIDO

#include <stdint.h>
#include <re.h>

int dns_init(struct dnsc **dnsc);
const char *protocol_name(int proto, bool secure);

#endif