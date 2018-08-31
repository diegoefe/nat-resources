#ifndef NP1_H_INCLUIDO
#define NP1_H_INCLUIDO

#include <string.h>

#define NUM_CANDIDATES 4

static char* default_remote_sdp(const char* _app_name) {
   return (char*)(strcmp("cam", _app_name)==0 ? "phone.sdp" : "cam.sdp");
}


#endif

