/* Taken from http://laiskiainen.org/rpm/examples/minirpm.c */

/*
 * "Trivial rpm" for an example of using rpmlib for installing, upgrading
 * and erasing packages by pmatilai@laiskiainen.org
 *
 * Usage: rpm [-i pkg] [-U pkg] -e [pkg] ...
 * Compile with "gcc -o minirpm minirpm.c -lrpm"
 */

#include <fcntl.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
#include <rpm/rpmcli.h>
#include <rpm/rpmdb.h>

int add_for_install(rpmts ts, char *file, int upgrade)
{
	FD_t fd;
	Header hdr;
	int rc = 0;

	/* Read package header */
	fd = Fopen(file, "r.ufdio");
	if (fd == NULL) {
		printf("Unable to open file %s\n", file);
		return 1;
	}
	rc = rpmReadPackageFile(ts, fd, file, &hdr);
	if (rc != RPMRC_OK) {
		printf("Unable to read package %s\n", file);
		return rc;
	}
	/* Add it to the transaction set */
	rc = rpmtsAddInstallElement(ts, hdr, file, upgrade, 0);
	if (rc) {
		printf("Error adding %s to transaction\n", file);
		goto out;
	}
out:
	headerFree(hdr);
	Fclose(fd);
	return rc;
}

int add_for_erase(rpmts ts, char *name)
{
	Header hdr;
	rpmdbMatchIterator mi;
	int rc = 0;

	/* Locate the package and add for erasure */
	mi = rpmtsInitIterator(ts, (rpmTag)RPMDBI_LABEL, name, 0);
	while ((hdr = rpmdbNextIterator(mi)) != NULL) {
		int recOffset = rpmdbGetIteratorOffset(mi);
		if (recOffset) {
			rc = rpmtsAddEraseElement(ts, hdr, recOffset);
			if (rc) 
				printf("Error adding %s to transaction", name);

		}
	}
	mi = rpmdbFreeIterator(mi);
	return rc;	
}

int main(int argc, char **argv)
{
	rpmts ts;
	rpmps probs;
	int probFilter = 0;
	int notifyFlags = 0;
	int tsFlags = 0;
	int rc = 0;
	
	/* Read configuration, initialize transaction */
	rpmReadConfigFiles(NULL, NULL);
	ts = rpmtsCreate();

	/* Set verification flags if needed, for example --nomd5 */
	/* rpmtsSetVSFlags(ts, rpmtsVSFlags(ts) | RPMVSF_NOMD5); */

	/* Open rpmdb */
	//rpmtsSetRootDir(ts, NULL);
	rc = rpmtsOpenDB(ts, O_RDWR);
	if (rc) {
		printf("Error opening rpmdb\n");
		goto exit;
	}

	/* Add packages for install/upgrade/erase */
	while (optind < argc) {
		int upgrade = 0;
		switch (getopt(argc, argv, "i:U:e:")) {
			case 'U':
				upgrade = 1;
			case 'i':
				add_for_install(ts, optarg, upgrade);
				break;
			case 'e':
				add_for_erase(ts, optarg);
				break;
			default:
				printf("usage ...\n");
				goto exit;
		}
	}

	/* Set problem filters if needed, for example --oldpackage */
	/* rpmbFilter |= RPMPROB_FILTER_OLDPACKAGE /*

	/* Set transaction flags if needed, for example --excludedocs */
	/* tsFlags |= RPMTRANS_FLAG_NODOCS */
	
	/* Check transaction sanity */
	rc = rpmtsCheck(ts);
	probs = rpmtsProblems(ts);
	if (rc || rpmpsNumProblems(probs)) {
		rpmpsPrint(NULL, probs);
		rpmpsFree(probs);
		goto exit;
	}
	
        /* Create ordering for the transaction */
	rc = rpmtsOrder(ts);
	if (rc > 0) {
		printf("Ordering failed\n");
		goto exit;
	}
	rpmtsClean(ts);

	/* Set callback routine & flags, for example -vh */
	notifyFlags |= INSTALL_LABEL | INSTALL_HASH;
	rpmtsSetNotifyCallback(ts, rpmShowProgress, (void *)notifyFlags);

	/* Set transaction flags and run the actual transaction */
	rpmtsSetFlags(ts, (rpmtransFlags)(rpmtsFlags(ts) | tsFlags));
	rc = rpmtsRun(ts, NULL, (rpmprobFilterFlags)probFilter);
	/* Check for results .. */
   	if (rc || rpmpsNumProblems(probs) > 0)
		rpmpsPrint(stderr, probs);
	rpmpsFree(probs);

exit:
	/* ..and clean up */
	rpmtsFree(ts);
	exit(rc);
}	
		
		
