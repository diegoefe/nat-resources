#include <stdio.h>

#include "pjwrap.h"


/*
 * And here's the main()
 */
int main(int argc, char *argv[])
{
	app_t cam;
	memset(&cam, 0, sizeof(cam));

    struct pj_getopt_option long_options[] = {
	{ "help",		0, 0, 'h'},
	{ "stun-srv",		1, 0, 's'},
	{ "log-file",		1, 0, 'L'},
    };
    int c, opt_id;
    pj_status_t status;

    cam.opt.comp_cnt = 1;
    cam.opt.max_host = -1;

    while((c=pj_getopt_long(argc,argv, "s:h:L", long_options, &opt_id))!=-1) {
	switch (c) {
	case 'h':
	    app_usage();
	    return 0;
	case 's':
	    cam.opt.stun_srv = pj_str(pj_optarg);
	    break;
	case 'L':
	    cam.opt.log_file = pj_optarg;
	    break;
	default:
	    printf("Argument \"%s\" is not valid. Use -h to see help",
		   argv[pj_optind]);
	    return 1;
	}
    }

    status = app_init(&cam);
    if (status != PJ_SUCCESS)
	return 1;

    // app_console();
	 app_start(&cam);
	getchar();
	app_stop(&cam);

    err_exit(&cam, "Quitting..", PJ_SUCCESS);
    return 0;
}

