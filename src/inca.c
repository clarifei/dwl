/* Inca! entry point. */
#include "inca.h"

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	const char *config_path = NULL;
	int c, debug = 0;

	while ((c = getopt(argc, argv, "s:c:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'c')
			config_path = optarg;
		else if (c == 'd')
			debug = 1;
		else if (c == 'v') {
			printf("Inca! %s\n", VERSION);
			return EXIT_SUCCESS;
		}
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	config_init(config_path);
	if (debug)
		config.log_level = WLR_DEBUG;
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-c config path] [-s startup command]", argv[0]);
}
