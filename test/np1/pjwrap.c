#include "pjwrap.h"

#define THIS_FILE   "pjwrap.c"

/* Utility to display error messages */
void app_perror(const char *title, pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(1,(THIS_FILE, "%s: %s", title, errmsg));
}

/* Utility: display error message and exit application (usually
 * because of fatal error.
 */
void err_exit(app_t* _app, const char *title, pj_status_t status) {
   if (status != PJ_SUCCESS) {
      app_perror(title, status);
   }
   PJ_LOG(3,(THIS_FILE, "Shutting down.."));

   if (_app->icest)
      pj_ice_strans_destroy(_app->icest);
    
    pj_thread_sleep(500);

    _app->thread_quit_flag = PJ_TRUE;
    if (_app->thread) {
       pj_thread_join(_app->thread);
       pj_thread_destroy(_app->thread);
    }

    if (_app->ice_cfg.stun_cfg.ioqueue)
	pj_ioqueue_destroy(_app->ice_cfg.stun_cfg.ioqueue);

    if (_app->ice_cfg.stun_cfg.timer_heap)
	pj_timer_heap_destroy(_app->ice_cfg.stun_cfg.timer_heap);

    pj_caching_pool_destroy(&_app->cp);

    pj_shutdown();

    if (_app->log_fhnd) {
	fclose(_app->log_fhnd);
	_app->log_fhnd = NULL;
    }

    exit(status != PJ_SUCCESS);
}

#define CHECK(expr)	status=expr; \
			if (status!=PJ_SUCCESS) { \
			    err_exit(_app, #expr, status); \
			}

/*
 * This function checks for events from both timer and ioqueue (for
 * network events). It is invoked by the worker thread.
 */
pj_status_t handle_events(app_t* _app, unsigned max_msec, unsigned *p_count)
{
    enum { MAX_NET_EVENTS = 1 };
    pj_time_val max_timeout = {0, 0};
    pj_time_val timeout = { 0, 0};
    unsigned count = 0, net_event_count = 0;
    int c;

    max_timeout.msec = max_msec;

    /* Poll the timer to run it and also to retrieve the earliest entry. */
    timeout.sec = timeout.msec = 0;
    c = pj_timer_heap_poll( _app->ice_cfg.stun_cfg.timer_heap, &timeout );
    if (c > 0)
	count += c;

    /* timer_heap_poll should never ever returns negative value, or otherwise
     * ioqueue_poll() will block forever!
     */
    pj_assert(timeout.sec >= 0 && timeout.msec >= 0);
    if (timeout.msec >= 1000) timeout.msec = 999;

    /* compare the value with the timeout to wait from timer, and use the 
     * minimum value. 
    */
    if (PJ_TIME_VAL_GT(timeout, max_timeout))
	timeout = max_timeout;

    /* Poll ioqueue. 
     * Repeat polling the ioqueue while we have immediate events, because
     * timer heap may process more than one events, so if we only process
     * one network events at a time (such as when IOCP backend is used),
     * the ioqueue may have trouble keeping up with the request rate.
     *
     * For example, for each send() request, one network event will be
     *   reported by ioqueue for the send() completion. If we don't poll
     *   the ioqueue often enough, the send() completion will not be
     *   reported in timely manner.
     */
    do {
	c = pj_ioqueue_poll( _app->ice_cfg.stun_cfg.ioqueue, &timeout);
	if (c < 0) {
	    pj_status_t err = pj_get_netos_error();
	    pj_thread_sleep(PJ_TIME_VAL_MSEC(timeout));
	    if (p_count)
		*p_count = count;
	    return err;
	} else if (c == 0) {
	    break;
	} else {
	    net_event_count += c;
	    timeout.sec = timeout.msec = 0;
	}
    } while (c > 0 && net_event_count < MAX_NET_EVENTS);

    count += net_event_count;
    if (p_count)
	*p_count = count;

    return PJ_SUCCESS;

}

/*
 * This is the worker thread that polls event in the background.
 */
int app_worker_thread(void *arg)
{
	 app_t* app = arg; 

    while (!app->thread_quit_flag) {
		handle_events(app, 500, NULL);
    }

    return 0;
}

/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about incoming data. By "data" it means application
 * data such as RTP/RTCP, and not packets that belong to ICE signaling (such
 * as STUN connectivity checks or TURN signaling).
 */
void cb_on_rx_data(pj_ice_strans *ice_st,
			  unsigned comp_id, 
			  void *pkt, pj_size_t size,
			  const pj_sockaddr_t *src_addr,
			  unsigned src_addr_len)
{
    char ipstr[PJ_INET6_ADDRSTRLEN+10];

    PJ_UNUSED_ARG(ice_st);
    PJ_UNUSED_ARG(src_addr_len);
    PJ_UNUSED_ARG(pkt);

    // Don't do this! It will ruin the packet buffer in case TCP is used!
    //((char*)pkt)[size] = '\0';

    PJ_LOG(3,(THIS_FILE, "Component %d: received %d bytes data from %s: \"%.*s\"",
	      comp_id, size,
	      pj_sockaddr_print(src_addr, ipstr, sizeof(ipstr), 3),
	      (unsigned)size,
	      (char*)pkt));
}

#if 0
/*
 * This is the callback that is registered to the ICE stream transport to
 * receive notification about ICE state progression.
 */
void cb_on_ice_complete(pj_ice_strans *ice_st, 
			       pj_ice_strans_op op,
			       pj_status_t status)
{
    const char *opname = (op==PJ_ICE_STRANS_OP_INIT? "initialization" :
	    (op==PJ_ICE_STRANS_OP_NEGOTIATION ? "negotiation" : "unknown_op"));

	if (status == PJ_SUCCESS) {
		PJ_LOG(3,(THIS_FILE, "ICE %s successful", opname));
	} else {
		char errmsg[PJ_ERR_MSG_SIZE];
		pj_strerror(status, errmsg, sizeof(errmsg));
		PJ_LOG(1,(THIS_FILE, "ICE %s failed: %s", opname, errmsg));
		pj_ice_strans_destroy(ice_st);
		// OJO! correr antes de resetear
		// _app->icest = NULL;
	}
}
#endif

// GLOBAL GARBAGE (must go away!)
static FILE* log_fh = NULL;

static void static_log_func(int level, const char *data, int len) {
    pj_log_write(level, data, len);
    if (log_fh) {
		 if (fwrite(data, len, 1, log_fh) != 1) return;
    }
}

/* log callback to write to file */
void log_func(app_t* _app, int level, const char *data, int len)
{
	log_fh = _app->log_fhnd;
}

/*
 * This is the main application initialization function. It is called
 * once (and only once) during application initialization sequence by 
 * main().
 */
pj_status_t app_init(app_t* _app)
{
    pj_status_t status;

    if (_app->opt.log_file) {
		_app->log_fhnd = fopen(_app->opt.log_file, "a");
		pj_log_set_log_func(&static_log_func);
    }

    /* Initialize the libraries before anything else */
    CHECK( pj_init() );
    CHECK( pjlib_util_init() );
    CHECK( pjnath_init() );

    /* Must create pool factory, where memory allocations come from */
    pj_caching_pool_init(&_app->cp, NULL, 0);

    /* Init our ICE settings with null values */
    pj_ice_strans_cfg_default(&_app->ice_cfg);

    _app->ice_cfg.stun_cfg.pf = &_app->cp.factory;

    /* Create application memory pool */
    _app->pool = pj_pool_create(&_app->cp.factory, _app->name.ptr, 512, 512, NULL);

    /* Create timer heap for timer stuff */
    CHECK( pj_timer_heap_create(_app->pool, 100, 
				&_app->ice_cfg.stun_cfg.timer_heap) );

    /* and create ioqueue for network I/O stuff */
    CHECK( pj_ioqueue_create(_app->pool, 16, 
			     &_app->ice_cfg.stun_cfg.ioqueue) );

    /* something must poll the timer heap and ioqueue, 
     * unless we're on Symbian where the timer heap and ioqueue run
     * on themselves.
     */
    CHECK( pj_thread_create(_app->pool, _app->name.ptr, &app_worker_thread, _app, 0, 0, &_app->thread) ); 
    _app->ice_cfg.af = pj_AF_INET();

    /* Create DNS resolver if nameserver is set */
    if (_app->opt.ns.slen) {
	CHECK( pj_dns_resolver_create(&_app->cp.factory, 
				      "resolver", 
				      0, 
				      _app->ice_cfg.stun_cfg.timer_heap,
				      _app->ice_cfg.stun_cfg.ioqueue, 
				      &_app->ice_cfg.resolver) );

	CHECK( pj_dns_resolver_set_ns(_app->ice_cfg.resolver, 1, 
				      &_app->opt.ns, NULL) );
    }

    /* -= Start initializing ICE stream transport config =- */

    /* Maximum number of host candidates */
    if (_app->opt.max_host != -1)
	_app->ice_cfg.stun.max_host_cands = _app->opt.max_host;

    /* Nomination strategy */
    if (_app->opt.regular)
	_app->ice_cfg.opt.aggressive = PJ_FALSE;
    else
	_app->ice_cfg.opt.aggressive = PJ_TRUE;

    /* Configure STUN/srflx candidate resolution */
    if (_app->opt.stun_srv.slen) {
	char *pos;

	/* Command line option may contain port number */
	if ((pos=pj_strchr(&_app->opt.stun_srv, ':')) != NULL) {
	    _app->ice_cfg.stun.server.ptr = _app->opt.stun_srv.ptr;
	    _app->ice_cfg.stun.server.slen = (pos - _app->opt.stun_srv.ptr);

	    _app->ice_cfg.stun.port = (pj_uint16_t)atoi(pos+1);
	} else {
	    _app->ice_cfg.stun.server = _app->opt.stun_srv;
	    _app->ice_cfg.stun.port = PJ_STUN_PORT;
	}

	/* For this demo app, configure longer STUN keep-alive time
	 * so that it does't clutter the screen output.
	 */
	_app->ice_cfg.stun.cfg.ka_interval = KA_INTERVAL;
    }

    /* -= That's it for now, initialization is complete =- */
    return PJ_SUCCESS;
}


/*
 * Create ICE stream transport instance, invoked from the menu.
 */
void app_create_instance(app_t* _app, f_on_ice_complete _on_ice_complete)
{
    pj_ice_strans_cb icecb;
    pj_status_t status;

    if (_app->icest != NULL) {
	puts("ICE instance already created, destroy it first");
	return;
    }

    /* init the callback */
    pj_bzero(&icecb, sizeof(icecb));
    icecb.on_rx_data = cb_on_rx_data;
    // icecb.on_ice_complete = cb_on_ice_complete;
    icecb.on_ice_complete = _on_ice_complete;

    /* create the instance */
    status = pj_ice_strans_create(_app->name.ptr,		    /* object name  */
				&_app->ice_cfg,	    /* settings	    */
				_app->opt.comp_cnt,	    /* comp_cnt	    */
				NULL,			    /* user data    */
				&icecb,			    /* callback	    */
				&_app->icest)		    /* instance ptr */
				;
    if (status != PJ_SUCCESS)
	app_perror("error creating ice", status);
    else
	PJ_LOG(3,(THIS_FILE, "ICE instance successfully created"));
}

/* Utility to nullify parsed remote info */
void reset_rem_info(app_t* _app)
{
    pj_bzero(&_app->rem, sizeof(_app->rem));
}


/*
 * Destroy ICE stream transport instance, invoked from the menu.
 */
void app_destroy_instance(app_t* _app)
{
    if (_app->icest == NULL) {
	PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
	return;
    }

    pj_ice_strans_destroy(_app->icest);
    _app->icest = NULL;

    reset_rem_info(_app);

    PJ_LOG(3,(THIS_FILE, "ICE instance destroyed"));
}


/*
 * Create ICE session, invoked from the menu.
 */
void app_init_session(app_t* _app, unsigned rolechar)
{
    pj_ice_sess_role role = (pj_tolower((pj_uint8_t)rolechar)=='o' ? 
				PJ_ICE_SESS_ROLE_CONTROLLING : 
				PJ_ICE_SESS_ROLE_CONTROLLED);
    pj_status_t status;

	PJ_LOG(3,(THIS_FILE, "SESSION 1 ------------------------------------------------"));
    if (_app->icest == NULL) {
	PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
	return;
    }

    if (pj_ice_strans_has_sess(_app->icest)) {
	PJ_LOG(1,(THIS_FILE, "Error: Session already created"));
	return;
    }
	PJ_LOG(3,(THIS_FILE, "SESSION 2 ------------------------------------------------"));

    status = pj_ice_strans_init_ice(_app->icest, role, NULL, NULL);
    if (status != PJ_SUCCESS)
	app_perror("error creating session", status);
    else
	PJ_LOG(3,(THIS_FILE, "ICE session created"));

	PJ_LOG(3,(THIS_FILE, "SESSION 3 ------------------------------------------------"));
    reset_rem_info(_app);
	PJ_LOG(3,(THIS_FILE, "SESSION 4 ------------------------------------------------"));
}


/*
 * Stop/destroy ICE session, invoked from the menu.
 */
void app_stop_session(app_t* _app)
{
    pj_status_t status;

    if (_app->icest == NULL) {
	PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
	return;
    }

    if (!pj_ice_strans_has_sess(_app->icest)) {
	PJ_LOG(1,(THIS_FILE, "Error: No ICE session, initialize first"));
	return;
    }

    status = pj_ice_strans_stop_ice(_app->icest);
    if (status != PJ_SUCCESS)
	app_perror("error stopping session", status);
    else
	PJ_LOG(3,(THIS_FILE, "ICE session stopped"));

    reset_rem_info(_app);
}

#define PRINT(...)	    \
	printed = pj_ansi_snprintf(p, maxlen - (p-buffer),  \
				   __VA_ARGS__); \
	if (printed <= 0 || printed >= (int)(maxlen - (p-buffer))) \
	    return -PJ_ETOOSMALL; \
	p += printed


/* Utility to create a=candidate SDP attribute */
int print_cand(char buffer[], unsigned maxlen,
		      const pj_ice_sess_cand *cand)
{
    char ipaddr[PJ_INET6_ADDRSTRLEN];
    char *p = buffer;
    int printed;

    PRINT("a=candidate:%.*s %u UDP %u %s %u typ ",
	  (int)cand->foundation.slen,
	  cand->foundation.ptr,
	  (unsigned)cand->comp_id,
	  cand->prio,
	  pj_sockaddr_print(&cand->addr, ipaddr, 
			    sizeof(ipaddr), 0),
	  (unsigned)pj_sockaddr_get_port(&cand->addr));

    PRINT("%s\n",
	  pj_ice_get_cand_type_name(cand->type));

    if (p == buffer+maxlen)
	return -PJ_ETOOSMALL;

    *p = '\0';

    return (int)(p-buffer);
}

/* 
 * Encode ICE information in SDP.
 */
int encode_session(app_t* _app, char buffer[], unsigned maxlen)
{
char *p = buffer;
unsigned comp;
int printed;
pj_str_t local_ufrag, local_pwd;
pj_status_t status;

/* Write "dummy" SDP v=, o=, s=, and t= lines */
PRINT("v=0\no=- 3414953978 3414953978 IN IP4 localhost\ns=ice\nt=0 0\n");

/* Get ufrag and pwd from current session */
pj_ice_strans_get_ufrag_pwd(_app->icest, &local_ufrag, &local_pwd,
	 NULL, NULL);

/* Write the a=ice-ufrag and a=ice-pwd attributes */
PRINT("a=ice-ufrag:%.*s\na=ice-pwd:%.*s\n",
(int)local_ufrag.slen,
local_ufrag.ptr,
(int)local_pwd.slen,
local_pwd.ptr);

/* Write each component */
for (comp=0; comp<_app->opt.comp_cnt; ++comp) {
unsigned j, cand_cnt;
pj_ice_sess_cand cand[PJ_ICE_ST_MAX_CAND];
char ipaddr[PJ_INET6_ADDRSTRLEN];

/* Get default candidate for the component */
status = pj_ice_strans_get_def_cand(_app->icest, comp+1, &cand[0]);
if (status != PJ_SUCCESS)
return -status;

/* Write the default address */
if (comp==0) {
/* For component 1, default address is in m= and c= lines */
PRINT("m=audio %d RTP/AVP 0\n"
"c=IN IP4 %s\n",
(int)pj_sockaddr_get_port(&cand[0].addr),
pj_sockaddr_print(&cand[0].addr, ipaddr,
		  sizeof(ipaddr), 0));
} else if (comp==1) {
/* For component 2, default address is in a=rtcp line */
PRINT("a=rtcp:%d IN IP4 %s\n",
(int)pj_sockaddr_get_port(&cand[0].addr),
pj_sockaddr_print(&cand[0].addr, ipaddr,
		  sizeof(ipaddr), 0));
} else {
/* For other components, we'll just invent this.. */
PRINT("a=Xice-defcand:%d IN IP4 %s\n",
(int)pj_sockaddr_get_port(&cand[0].addr),
pj_sockaddr_print(&cand[0].addr, ipaddr,
		  sizeof(ipaddr), 0));
}

/* Enumerate all candidates for this component */
cand_cnt = PJ_ARRAY_SIZE(cand);
status = pj_ice_strans_enum_cands(_app->icest, comp+1,
			&cand_cnt, cand);
if (status != PJ_SUCCESS)
return -status;

/* And encode the candidates as SDP */
for (j=0; j<cand_cnt; ++j) {
printed = print_cand(p, maxlen - (unsigned)(p-buffer), &cand[j]);
if (printed < 0)
return -PJ_ETOOSMALL;
p += printed;
}
}

if (p == buffer+maxlen)
return -PJ_ETOOSMALL;

*p = '\0';
return (int)(p - buffer);
}


/*
* Show information contained in the ICE stream transport. This is
* invoked from the menu.
*/
void app_show_ice(app_t* _app)
{
static char buffer[1000];
int len;

if (_app->icest == NULL) {
PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
return;
}

puts("General info");
puts("---------------");
printf("Component count    : %d\n", _app->opt.comp_cnt);
printf("Status             : ");
if (pj_ice_strans_sess_is_complete(_app->icest))
puts("negotiation complete");
else if (pj_ice_strans_sess_is_running(_app->icest))
puts("negotiation is in progress");
else if (pj_ice_strans_has_sess(_app->icest))
puts("session ready");
else
puts("session not created");

if (!pj_ice_strans_has_sess(_app->icest)) {
puts("Create the session first to see more info");
return;
}

printf("Negotiated comp_cnt: %d\n", 
pj_ice_strans_get_running_comp_cnt(_app->icest));
printf("Role               : %s\n",
pj_ice_strans_get_role(_app->icest)==PJ_ICE_SESS_ROLE_CONTROLLED ?
"controlled" : "controlling");

len = encode_session(_app, buffer, sizeof(buffer));
if (len < 0)
err_exit(_app, "not enough buffer to show ICE status", -len);

puts("");
printf("Local SDP (paste this to remote host):\n"
"--------------------------------------\n"
"%s\n", buffer);


puts("");
puts("Remote info:\n"
"----------------------");
if (_app->rem.cand_cnt==0) {
puts("No remote info yet");
} else {
unsigned i;

printf("Remote ufrag       : %s\n", _app->rem.ufrag);
printf("Remote password    : %s\n", _app->rem.pwd);
printf("Remote cand. cnt.  : %d\n", _app->rem.cand_cnt);

for (i=0; i<_app->rem.cand_cnt; ++i) {
len = print_cand(buffer, sizeof(buffer), &_app->rem.cand[i]);
if (len < 0)
err_exit(_app, "not enough buffer to show ICE status", -len);

printf("  %s", buffer);
}
}
}


/*
* Input and parse SDP from the remote (containing remote's ICE information) 
* and save it to global variables.
*/
void app_input_remote(app_t* _app)
{
char linebuf[80];
unsigned media_cnt = 0;
unsigned comp0_port = 0;
char     comp0_addr[80];
pj_bool_t done = PJ_FALSE;

puts("Paste SDP from remote host, end with empty line");

reset_rem_info(_app);

comp0_addr[0] = '\0';

while (!done) {
pj_size_t len;
char *line;

printf(">");
if (stdout) fflush(stdout);

if (fgets(linebuf, sizeof(linebuf), stdin)==NULL)
break;

len = strlen(linebuf);
while (len && (linebuf[len-1] == '\r' || linebuf[len-1] == '\n'))
linebuf[--len] = '\0';

line = linebuf;
while (len && pj_isspace(*line))
++line, --len;

if (len==0)
break;

/* Ignore subsequent media descriptors */
if (media_cnt > 1)
continue;

switch (line[0]) {
case 'm':
{
int cnt;
char media[32], portstr[32];

++media_cnt;
if (media_cnt > 1) {
  puts("Media line ignored");
  break;
}

cnt = sscanf(line+2, "%s %s RTP/", media, portstr);
if (cnt != 2) {
  PJ_LOG(1,(THIS_FILE, "Error parsing media line"));
  goto on_error;
}

comp0_port = atoi(portstr);

}
break;
case 'c':
{
int cnt;
char c[32], net[32], ip[80];

cnt = sscanf(line+2, "%s %s %s", c, net, ip);
if (cnt != 3) {
  PJ_LOG(1,(THIS_FILE, "Error parsing connection line"));
  goto on_error;
}

strcpy(comp0_addr, ip);
}
break;
case 'a':
{
char *attr = strtok(line+2, ": \t\r\n");
if (strcmp(attr, "ice-ufrag")==0) {
  strcpy(_app->rem.ufrag, attr+strlen(attr)+1);
} else if (strcmp(attr, "ice-pwd")==0) {
  strcpy(_app->rem.pwd, attr+strlen(attr)+1);
} else if (strcmp(attr, "rtcp")==0) {
  char *val = attr+strlen(attr)+1;
  int af, cnt;
  int port;
  char net[32], ip[64];
  pj_str_t tmp_addr;
  pj_status_t status;

  cnt = sscanf(val, "%d IN %s %s", &port, net, ip);
  if (cnt != 3) {
 PJ_LOG(1,(THIS_FILE, "Error parsing rtcp attribute"));
 goto on_error;
  }

  if (strchr(ip, ':'))
 af = pj_AF_INET6();
  else
 af = pj_AF_INET();

  pj_sockaddr_init(af, &_app->rem.def_addr[1], NULL, 0);
  tmp_addr = pj_str(ip);
  status = pj_sockaddr_set_str_addr(af, &_app->rem.def_addr[1],
					 &tmp_addr);
  if (status != PJ_SUCCESS) {
 PJ_LOG(1,(THIS_FILE, "Invalid IP address"));
 goto on_error;
  }
  pj_sockaddr_set_port(&_app->rem.def_addr[1], (pj_uint16_t)port);

} else if (strcmp(attr, "candidate")==0) {
  char *sdpcand = attr+strlen(attr)+1;
  int af, cnt;
  char foundation[32], transport[12], ipaddr[80], type[32];
  pj_str_t tmpaddr;
  int comp_id, prio, port;
  pj_ice_sess_cand *cand;
  pj_status_t status;

  cnt = sscanf(sdpcand, "%s %d %s %d %s %d typ %s",
	  foundation,
	  &comp_id,
	  transport,
	  &prio,
	  ipaddr,
	  &port,
	  type);
  if (cnt != 7) {
 PJ_LOG(1, (THIS_FILE, "error: Invalid ICE candidate line"));
 goto on_error;
  }

  cand = &_app->rem.cand[_app->rem.cand_cnt];
  pj_bzero(cand, sizeof(*cand));
  
  if (strcmp(type, "host")==0)
 cand->type = PJ_ICE_CAND_TYPE_HOST;
  else if (strcmp(type, "srflx")==0)
 cand->type = PJ_ICE_CAND_TYPE_SRFLX;
  else if (strcmp(type, "relay")==0)
 cand->type = PJ_ICE_CAND_TYPE_RELAYED;
  else {
 PJ_LOG(1, (THIS_FILE, "Error: invalid candidate type '%s'", 
		 type));
 goto on_error;
  }

  cand->comp_id = (pj_uint8_t)comp_id;
  pj_strdup2(_app->pool, &cand->foundation, foundation);
  cand->prio = prio;
  
  if (strchr(ipaddr, ':'))
 af = pj_AF_INET6();
  else
 af = pj_AF_INET();

  tmpaddr = pj_str(ipaddr);
  pj_sockaddr_init(af, &cand->addr, NULL, 0);
  status = pj_sockaddr_set_str_addr(af, &cand->addr, &tmpaddr);
  if (status != PJ_SUCCESS) {
 PJ_LOG(1,(THIS_FILE, "Error: invalid IP address '%s'",
		ipaddr));
 goto on_error;
  }

  pj_sockaddr_set_port(&cand->addr, (pj_uint16_t)port);

  ++_app->rem.cand_cnt;

  if (cand->comp_id > _app->rem.comp_cnt)
 _app->rem.comp_cnt = cand->comp_id;
}
}
break;
}
}

if (_app->rem.cand_cnt==0 ||
_app->rem.ufrag[0]==0 ||
_app->rem.pwd[0]==0 ||
_app->rem.comp_cnt == 0)
{
PJ_LOG(1, (THIS_FILE, "Error: not enough info"));
goto on_error;
}

if (comp0_port==0 || comp0_addr[0]=='\0') {
PJ_LOG(1, (THIS_FILE, "Error: default address for component 0 not found"));
goto on_error;
} else {
int af;
pj_str_t tmp_addr;
pj_status_t status;

if (strchr(comp0_addr, ':'))
af = pj_AF_INET6();
else
af = pj_AF_INET();

pj_sockaddr_init(af, &_app->rem.def_addr[0], NULL, 0);
tmp_addr = pj_str(comp0_addr);
status = pj_sockaddr_set_str_addr(af, &_app->rem.def_addr[0],
			&tmp_addr);
if (status != PJ_SUCCESS) {
PJ_LOG(1,(THIS_FILE, "Invalid IP address in c= line"));
goto on_error;
}
pj_sockaddr_set_port(&_app->rem.def_addr[0], (pj_uint16_t)comp0_port);
}

PJ_LOG(3, (THIS_FILE, "Done, %d remote candidate(s) added", 
  _app->rem.cand_cnt));
return;

on_error:
reset_rem_info(_app);
}


/*
* Start ICE negotiation! This function is invoked from the menu.
*/
void app_start_nego(app_t* _app)
{
pj_str_t rufrag, rpwd;
pj_status_t status;

if (_app->icest == NULL) {
PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
return;
}

if (!pj_ice_strans_has_sess(_app->icest)) {
PJ_LOG(1,(THIS_FILE, "Error: No ICE session, initialize first"));
return;
}

if (_app->rem.cand_cnt == 0) {
PJ_LOG(1,(THIS_FILE, "Error: No remote info, input remote info first"));
return;
}

PJ_LOG(3,(THIS_FILE, "Starting ICE negotiation.."));

status = pj_ice_strans_start_ice(_app->icest, 
			pj_cstr(&rufrag, _app->rem.ufrag),
			pj_cstr(&rpwd, _app->rem.pwd),
			_app->rem.cand_cnt,
			_app->rem.cand);
if (status != PJ_SUCCESS)
app_perror("Error starting ICE", status);
else
PJ_LOG(3,(THIS_FILE, "ICE negotiation started"));
}


/*
* Send application data to remote agent.
*/
void app_send_data(app_t* _app, unsigned comp_id, const char *data)
{
	pj_status_t status;
	
	if (_app->icest == NULL) {
		PJ_LOG(1,(THIS_FILE, "Error: No ICE instance, create it first"));
		return;
	}
	
	if (!pj_ice_strans_has_sess(_app->icest)) {
		PJ_LOG(1,(THIS_FILE, "Error: No ICE session, initialize first"));
		return;
	}
	
	/*
	if (!pj_ice_strans_sess_is_complete(_app->icest)) {
	PJ_LOG(1,(THIS_FILE, "Error: ICE negotiation has not been started or is in progress"));
	return;
	}
	*/
	
	if (comp_id<1||comp_id>pj_ice_strans_get_running_comp_cnt(_app->icest)) {
		PJ_LOG(1,(THIS_FILE, "Error: invalid component ID"));
		return;
	}
	
	status = pj_ice_strans_sendto(_app->icest, comp_id, data, strlen(data),
			&_app->rem.def_addr[comp_id-1],
			pj_sockaddr_get_len(&_app->rem.def_addr[comp_id-1]));
	if (status != PJ_SUCCESS)
		app_perror("Error sending data", status);
	else
		PJ_LOG(3,(THIS_FILE, "Data sent"));
}


/*
* Display help for the menu.
*/
void app_help_menu(app_t* _app)
{
	puts("");
	puts("-= Help on using ICE and this _app->program =-");
	puts("");
	puts("This application demonstrates how to use ICE in pjnath without having\n"
	"to use the SIP protocol. To use this application, you will need to run\n"
	"two instances of this application, to simulate two ICE agents.\n");
	
	puts("Basic ICE flow:\n"
	" create instance [menu \"c\"]\n"
	" repeat these steps as wanted:\n"
	"   - init session as offerer or answerer [menu \"i\"]\n"
	"   - display our SDP [menu \"s\"]\n"
	"   - \"send\" our SDP from the \"show\" output above to remote, by\n"
	"     copy-pasting the SDP to the other _app->application\n"
	"   - parse remote SDP, by pasting SDP generated by the other app\n"
	"     instance [menu \"r\"]\n"
	"   - begin ICE negotiation in our end [menu \"b\"], and \n"
	"   - immediately begin ICE negotiation in the other app instance\n"
	"   - ICE negotiation will run, and result will be printed to screen\n"
	"   - send application data to remote [menu \"x\"]\n"
	"   - end/stop ICE session [menu \"e\"]\n"
	" destroy instance [menu \"d\"]\n"
"");

puts("");
puts("This concludes the help screen.");
puts("");
}


/*
* Display console menu
*/
void app_print_menu(void)
{
	puts("");
	puts("+----------------------------------------------------------------------+");
	puts("|                    M E N U                                           |");
	puts("+---+------------------------------------------------------------------+");
	puts("| c | create           Create the instance                             |");
	puts("| d | destroy          Destroy the instance                            |");
	puts("| i | init o|a         Initialize ICE session as offerer or answerer   |");
	puts("| e | stop             End/stop ICE session                            |");
	puts("| s | show             Display local ICE info                          |");
	puts("| r | remote           Input remote ICE info                           |");
	puts("| b | start            Begin ICE negotiation                           |");
	puts("| x | send <compid> .. Send data to remote                             |");
	puts("+---+------------------------------------------------------------------+");
	puts("| h |  help            * Help! *                                       |");
	puts("| q |  quit            Quit                                            |");
	puts("+----------------------------------------------------------------------+");
}


/*
* Display program usage.
*/
void app_usage(app_t* _app)
{
	printf("Usage: %s [options]\n", _app->name.ptr);
	printf("%s, using pjsip(%s)\n", _app->name.ptr, pj_get_version());
	puts("");
	puts("General options:");
	puts(" --log-file, -L FILE       Save output to log FILE");
	puts(" --help, -h                Display this screen.");
	puts("");
	puts("STUN related options:");
	puts(" --stun-srv, -s HOSTDOM    Enable srflx candidate by resolving to STUN server.");
	puts("                           HOSTDOM may be a \"host_or_ip[:port]\" or a domain");
	puts("                           name if DNS SRV resolution is used.");
	puts("");
}

void app_start(app_t* _app, f_on_ice_complete _on_ice_complete) {
	pj_log_set_level(_app->log_level);
	app_create_instance(_app, _on_ice_complete);
	app_init_session(_app, 'o');
	getchar();
	app_show_ice(_app);
	//app_input_remote();
	// app_start_nego();
}

void app_stop(app_t* _app) {
	app_stop_session(_app);
	app_destroy_instance(_app);
}

