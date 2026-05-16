#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wombat.h"

static char *argv0;

static void
usage(FILE *out, int code)
{
	fprintf(out,
	        "usage: %s -d [-s sdb.txt] input.m output.m\n"
	        "       %s -c [-s sdb.txt] [-r ref.m] input.m output.m\n"
	        "\n"
	        "  -d    decode bytecode to text\n"
	        "  -c    encode text to bytecode\n"
	        "  -s    path to sdb.txt (default: sdb.txt)\n"
	        "  -r    reference binary for variant-preserving re-encode\n"
	        "  -h    show this help and exit\n",
	        argv0, argv0);
	exit(code);
}

int
main(int argc, char *argv[])
{
	int mode;
	const char *sdbpath;
	const char *refpath;
	const char *inpath;
	const char *outpath;
	CScriptStringDB db;
	int ret;

	argv0 = argv[0];
	argc--;
	argv++;

	mode = 0;
	sdbpath = "sdb.txt";
	refpath = NULL;
	inpath = NULL;
	outpath = NULL;

	while (argc > 0 && argv[0][0] == '-') {
		if (strncmp(argv[0], "-d", 2) == 0)
			mode = 'd';
		else if (strncmp(argv[0], "-c", 2) == 0)
			mode = 'c';
		else if (strncmp(argv[0], "-h", 2) == 0)
			usage(stdout, 0);
		else if (strncmp(argv[0], "-s", 2) == 0) {
			argc--;
			argv++;
			if (argc == 0)
				usage(stderr, 1);
			sdbpath = argv[0];
		} else if (strncmp(argv[0], "-r", 2) == 0) {
			argc--;
			argv++;
			if (argc == 0)
				usage(stderr, 1);
			refpath = argv[0];
		} else
			usage(stderr, 1);
		argc--;
		argv++;
	}

	if (argc != 2 || mode == 0)
		usage(stderr, 1);
	inpath = argv[0];
	outpath = argv[1];

	memset(&db, 0, sizeof(db));
	if (CScriptStringDB_Load(&db, sdbpath) != 0) {
		fprintf(stderr, "wombat: cannot load sdb: %s\n", sdbpath);
		return 1;
	}

	if (mode == 'd')
		ret = wombat_decode(inpath, outpath, &db);
	else {
		int old_count = db.count;
		ret = wombat_encode(inpath, outpath, &db, refpath);
		if (ret == 0 && db.count > old_count)
			CScriptStringDB_Save(&db, sdbpath);
	}

	CScriptStringDB_Free(&db);
	return ret;
}
