/*
 * smartctl.cpp
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2002-10 Bruce Allen <smartmontools-support@lists.sourceforge.net>
 * Copyright (C) 2008-10 Christian Franke <smartmontools-support@lists.sourceforge.net>
 * Copyright (C) 2000 Michael Cornwell <cornwell@acm.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * This code was originally developed as a Senior Thesis by Michael Cornwell
 * at the Concurrent Systems Laboratory (now part of the Storage Systems
 * Research Center), Jack Baskin School of Engineering, University of
 * California, Santa Cruz. http://ssrc.soe.ucsc.edu/
 *
 */

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <stdarg.h>
#include <stdexcept>
#include <getopt.h>

#include "config.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(__FreeBSD__)
#include <sys/param.h>
#endif

#if defined(__QNXNTO__) 
#include <new> // TODO: Why is this include necessary on QNX ?
#endif

#include "int64.h"
#include "atacmds.h"
#include "dev_interface.h"
#include "ataprint.h"
#include "extern.h"
#include "knowndrives.h"
#include "scsicmds.h"
#include "scsiprint.h"
#include "smartctl.h"
#include "utility.h"

const char * smartctl_cpp_cvsid = "$Id$"
                                  CONFIG_H_CVSID EXTERN_H_CVSID SMARTCTL_H_CVSID;

// This is a block containing all the "control variables".  We declare
// this globally in this file, and externally in other files.
smartmonctrl *con=NULL;

static void printslogan()
{
  pout("%s\n", format_version_info("smartctl").c_str());
}

void UsageSummary(){
  pout("\nUse smartctl -h to get a usage summary\n\n");
  return;
}

static std::string getvalidarglist(char opt);

/*  void prints help information for command syntax */
void Usage (void){
  printf("Usage: smartctl [options] device\n\n");
  printf(
"============================================ SHOW INFORMATION OPTIONS =====\n\n"
"  -h, --help, --usage\n"
"         Display this help and exit\n\n"
"  -V, --version, --copyright, --license\n"
"         Print license, copyright, and version information and exit\n\n"
"  -i, --info                                                       \n"
"         Show identity information for device\n\n"
"  -a, --all                                                        \n"
"         Show all SMART information for device\n\n"
"  -x, --xall\n"
"         Show all information for device\n\n"
  );
  printf(
"================================== SMARTCTL RUN-TIME BEHAVIOR OPTIONS =====\n\n"
"  -q TYPE, --quietmode=TYPE                                           (ATA)\n"
"         Set smartctl quiet mode to one of: errorsonly, silent, noserial\n\n"
"  -d TYPE, --device=TYPE\n"
"         Specify device type to one of: %s\n\n"
"  -T TYPE, --tolerance=TYPE                                           (ATA)\n"
"         Tolerance: normal, conservative, permissive, verypermissive\n\n"
"  -b TYPE, --badsum=TYPE                                              (ATA)\n"
"         Set action on bad checksum to one of: warn, exit, ignore\n\n"
"  -r TYPE, --report=TYPE\n"
"         Report transactions (see man page)\n\n"
"  -n MODE, --nocheck=MODE                                             (ATA)\n"
"         No check if: never, sleep, standby, idle (see man page)\n\n",
  getvalidarglist('d').c_str()); // TODO: Use this function also for other options ?
  printf(
"============================== DEVICE FEATURE ENABLE/DISABLE COMMANDS =====\n\n"
"  -s VALUE, --smart=VALUE\n"
"        Enable/disable SMART on device (on/off)\n\n"
"  -o VALUE, --offlineauto=VALUE                                       (ATA)\n"
"        Enable/disable automatic offline testing on device (on/off)\n\n"
"  -S VALUE, --saveauto=VALUE                                          (ATA)\n"
"        Enable/disable Attribute autosave on device (on/off)\n\n"
  );
  printf(
"======================================= READ AND DISPLAY DATA OPTIONS =====\n\n"
"  -H, --health\n"
"        Show device SMART health status\n\n"
"  -c, --capabilities                                                  (ATA)\n"
"        Show device SMART capabilities\n\n"
"  -A, --attributes                                                         \n"
"        Show device SMART vendor-specific Attributes and values\n\n"
"  -l TYPE, --log=TYPE\n"
"        Show device log. TYPE: error, selftest, selective, directory[,g|s],\n"
"                               background, sasphy[,reset], sataphy[,reset],\n"
"                               scttemp[sts,hist],\n"
"                               gplog,N[,RANGE], smartlog,N[,RANGE],\n"
"                               xerror[,N][,error], xselftest[,N][,selftest]\n\n"
"  -v N,OPTION , --vendorattribute=N,OPTION                            (ATA)\n"
"        Set display OPTION for vendor Attribute N (see man page)\n\n"
"  -F TYPE, --firmwarebug=TYPE                                         (ATA)\n"
"        Use firmware bug workaround: none, samsung, samsung2,\n"
"                                     samsung3, swapid\n\n"
"  -P TYPE, --presets=TYPE                                             (ATA)\n"
"        Drive-specific presets: use, ignore, show, showall\n\n"
"  -B [+]FILE, --drivedb=[+]FILE                                       (ATA)\n"
"        Read and replace [add] drive database from FILE\n"
#ifdef SMARTMONTOOLS_DRIVEDBDIR
"        [default is "SMARTMONTOOLS_DRIVEDBDIR"/drivedb.h]\n"
#endif
"\n"
  );
  printf(
"============================================ DEVICE SELF-TEST OPTIONS =====\n\n"
"  -t TEST, --test=TEST\n"
"        Run test. TEST: offline short long conveyance select,M-N\n"
"                        pending,N afterselect,[on|off] scttempint,N[,p]\n\n"
"  -C, --captive\n"
"        Do test in captive mode (along with -t)\n\n"
"  -X, --abort\n"
"        Abort any non-captive test on device\n\n"
);
  std::string examples = smi()->get_app_examples("smartctl");
  if (!examples.empty())
    printf("%s\n", examples.c_str());
}

/* Returns a string containing a formatted list of the valid arguments
   to the option opt or empty on failure. Note 'v' case different */
static std::string getvalidarglist(char opt)
{
  switch (opt) {
  case 'q':
    return "errorsonly, silent, noserial";
  case 'd':
    return smi()->get_valid_dev_types_str() + ", test";
  case 'T':
    return "normal, conservative, permissive, verypermissive";
  case 'b':
    return "warn, exit, ignore";
  case 'r':
    return "ioctl[,N], ataioctl[,N], scsiioctl[,N]";
  case 's':
  case 'o':
  case 'S':
    return "on, off";
  case 'l':
    return "error, selftest, selective, directory[,g|s], background, scttemp[sts|hist], "
           "sasphy[,reset], sataphy[,reset], gplog,N[,RANGE], smartlog,N[,RANGE], "
	   "xerror[,N][,error], xselftest[,N][,selftest]";
  case 'P':
    return "use, ignore, show, showall";
  case 't':
    return "offline, short, long, conveyance, select,M-N, pending,N, afterselect,[on|off], scttempint,N[,p]";
  case 'F':
    return "none, samsung, samsung2, samsung3, swapid";
  case 'n':
    return "never, sleep, standby, idle";
  case 'v':
  default:
    return "";
  }
}

/* Prints the message "=======> VALID ARGUMENTS ARE: <LIST> \n", where
   <LIST> is the list of valid arguments for option opt. */
void printvalidarglistmessage(char opt) {
 
  if (opt=='v'){
    pout("=======> VALID ARGUMENTS ARE:\n\thelp\n%s\n<=======\n",
         create_vendor_attribute_arg_list().c_str());
  }
  else {
  // getvalidarglist() might produce a multiline or single line string.  We
  // need to figure out which to get the formatting right.
    std::string s = getvalidarglist(opt);
    char separator = strchr(s.c_str(), '\n') ? '\n' : ' ';
    pout("=======> VALID ARGUMENTS ARE:%c%s%c<=======\n", separator, s.c_str(), separator);
  }

  return;
}

// Checksum error mode
enum checksum_err_mode_t {
  CHECKSUM_ERR_WARN, CHECKSUM_ERR_EXIT, CHECKSUM_ERR_IGNORE
};

static checksum_err_mode_t checksum_err_mode = CHECKSUM_ERR_WARN;

/*      Takes command options and sets features to be run */    
const char * parse_options(int argc, char** argv,
                           ata_print_options & ataopts,
                           scsi_print_options & scsiopts)
{
  // Please update getvalidarglist() if you edit shortopts
  const char *shortopts = "h?Vq:d:T:b:r:s:o:S:HcAl:iaxv:P:t:CXF:n:B:";
  // Please update getvalidarglist() if you edit longopts
  struct option longopts[] = {
    { "help",            no_argument,       0, 'h' },
    { "usage",           no_argument,       0, 'h' },
    { "version",         no_argument,       0, 'V' },
    { "copyright",       no_argument,       0, 'V' },
    { "license",         no_argument,       0, 'V' },
    { "quietmode",       required_argument, 0, 'q' },
    { "device",          required_argument, 0, 'd' },
    { "tolerance",       required_argument, 0, 'T' },
    { "badsum",          required_argument, 0, 'b' },
    { "report",          required_argument, 0, 'r' },
    { "smart",           required_argument, 0, 's' },
    { "offlineauto",     required_argument, 0, 'o' },
    { "saveauto",        required_argument, 0, 'S' },
    { "health",          no_argument,       0, 'H' },
    { "capabilities",    no_argument,       0, 'c' },
    { "attributes",      no_argument,       0, 'A' },
    { "log",             required_argument, 0, 'l' },
    { "info",            no_argument,       0, 'i' },
    { "all",             no_argument,       0, 'a' },
    { "xall",            no_argument,       0, 'x' },
    { "vendorattribute", required_argument, 0, 'v' },
    { "presets",         required_argument, 0, 'P' },
    { "test",            required_argument, 0, 't' },
    { "captive",         no_argument,       0, 'C' },
    { "abort",           no_argument,       0, 'X' },
    { "firmwarebug",     required_argument, 0, 'F' },
    { "nocheck",         required_argument, 0, 'n' },
    { "drivedb",         required_argument, 0, 'B' },
    { 0,                 0,                 0, 0   }
  };

  char extraerror[256];
  memset(extraerror, 0, sizeof(extraerror));
  memset(con,0,sizeof(*con));
  opterr=optopt=0;

  const char * type = 0; // set to -d optarg
  bool no_defaultdb = false; // set true on '-B FILE'
  bool badarg = false, captive = false;
  int testcnt = 0; // number of self-tests requested

  int optchar;
  char *arg;

  // This miserable construction is needed to get emacs to do proper indenting. Sorry!
  while (-1 != (optchar = 
                getopt_long(argc, argv, shortopts, longopts, NULL)
                )){
    switch (optchar){
    case 'V':
      con->dont_print = false;
      pout("%s", format_version_info("smartctl", true /*full*/).c_str());
      EXIT(0);
      break;
    case 'q':
      if (!strcmp(optarg,"errorsonly")) {
        con->printing_switchable = true;
        con->dont_print = false;
      } else if (!strcmp(optarg,"silent")) {
        con->printing_switchable = false;
        con->dont_print = true;
      } else if (!strcmp(optarg,"noserial")) {
        con->dont_print_serial = true;
      } else {
        badarg = true;
      }
      break;
    case 'd':
      type = optarg;
      break;
    case 'T':
      if (!strcmp(optarg,"normal")) {
        con->conservative = false;
        con->permissive   = 0;
      } else if (!strcmp(optarg,"conservative")) {
        con->conservative = true;
      } else if (!strcmp(optarg,"permissive")) {
        if (con->permissive<0xff)
          con->permissive++;
      } else if (!strcmp(optarg,"verypermissive")) {
        con->permissive = 0xff;
      } else {
        badarg = true;
      }
      break;
    case 'b':
      if (!strcmp(optarg,"warn")) {
        checksum_err_mode = CHECKSUM_ERR_WARN;
      } else if (!strcmp(optarg,"exit")) {
        checksum_err_mode = CHECKSUM_ERR_EXIT;
      } else if (!strcmp(optarg,"ignore")) {
        checksum_err_mode = CHECKSUM_ERR_IGNORE;
      } else {
        badarg = true;
      }
      break;
    case 'r':
      {
        int i;
        char *s;

        // split_report_arg() may modify its first argument string, so use a
        // copy of optarg in case we want optarg for an error message.
        if (!(s = strdup(optarg))) {
          throw std::bad_alloc();
        }
        if (split_report_arg(s, &i)) {
          badarg = true;
        } else if (!strcmp(s,"ioctl")) {
          con->reportataioctl  = con->reportscsiioctl = i;
        } else if (!strcmp(s,"ataioctl")) {
          con->reportataioctl = i;
        } else if (!strcmp(s,"scsiioctl")) {
          con->reportscsiioctl = i;
        } else {
          badarg = true;
        }
        free(s);
      }
      break;
    case 's':
      if (!strcmp(optarg,"on")) {
        ataopts.smart_enable  = scsiopts.smart_enable  = true;
        ataopts.smart_disable = scsiopts.smart_disable = false;
      } else if (!strcmp(optarg,"off")) {
        ataopts.smart_disable = scsiopts.smart_disable = true;
        ataopts.smart_enable  = scsiopts.smart_enable  = false;
      } else {
        badarg = true;
      }
      break;
    case 'o':
      if (!strcmp(optarg,"on")) {
        ataopts.smart_auto_offl_enable  = true;
        ataopts.smart_auto_offl_disable = false;
      } else if (!strcmp(optarg,"off")) {
        ataopts.smart_auto_offl_disable = true;
        ataopts.smart_auto_offl_enable  = false;
      } else {
        badarg = true;
      }
      break;
    case 'S':
      if (!strcmp(optarg,"on")) {
        ataopts.smart_auto_save_enable  = scsiopts.smart_auto_save_enable  = true;
        ataopts.smart_auto_save_disable = scsiopts.smart_auto_save_disable = false;
      } else if (!strcmp(optarg,"off")) {
        ataopts.smart_auto_save_disable = scsiopts.smart_auto_save_disable = true;
        ataopts.smart_auto_save_enable  = scsiopts.smart_auto_save_enable  = false;
      } else {
        badarg = true;
      }
      break;
    case 'H':
      ataopts.smart_check_status = scsiopts.smart_check_status = true;
      break;
    case 'F':
      if (!strcmp(optarg,"none")) {
        ataopts.fix_firmwarebug = FIX_NONE;
      } else if (!strcmp(optarg,"samsung")) {
        ataopts.fix_firmwarebug = FIX_SAMSUNG;
      } else if (!strcmp(optarg,"samsung2")) {
        ataopts.fix_firmwarebug = FIX_SAMSUNG2;
      } else if (!strcmp(optarg,"samsung3")) {
        ataopts.fix_firmwarebug = FIX_SAMSUNG3;
      } else if (!strcmp(optarg,"swapid")) {
        ataopts.fix_swapped_id = true;
      } else {
        badarg = true;
      }
      break;
    case 'c':
      ataopts.smart_general_values = true;
      break;
    case 'A':
      ataopts.smart_vendor_attrib = scsiopts.smart_vendor_attrib = true;
      break;
    case 'l':
      if (!strcmp(optarg,"error")) {
        ataopts.smart_error_log = scsiopts.smart_error_log = true;
      } else if (!strcmp(optarg,"selftest")) {
        ataopts.smart_selftest_log = scsiopts.smart_selftest_log = true;
      } else if (!strcmp(optarg, "selective")) {
        ataopts.smart_selective_selftest_log = true;
      } else if (!strcmp(optarg,"directory")) {
        ataopts.smart_logdir = ataopts.gp_logdir = true; // SMART+GPL
      } else if (!strcmp(optarg,"directory,s")) {
        ataopts.smart_logdir = true; // SMART
      } else if (!strcmp(optarg,"directory,g")) {
        ataopts.gp_logdir = true; // GPL
      } else if (!strcmp(optarg,"sasphy")) {
        scsiopts.sasphy = true;
      } else if (!strcmp(optarg,"sasphy,reset")) {
        scsiopts.sasphy = scsiopts.sasphy_reset = true;
      } else if (!strcmp(optarg,"sataphy")) {
        ataopts.sataphy = true;
      } else if (!strcmp(optarg,"sataphy,reset")) {
        ataopts.sataphy = ataopts.sataphy_reset = true;
      } else if (!strcmp(optarg,"background")) {
        scsiopts.smart_background_log = true;
      } else if (!strcmp(optarg,"scttemp")) {
        ataopts.sct_temp_sts = ataopts.sct_temp_hist = true;
      } else if (!strcmp(optarg,"scttempsts")) {
        ataopts.sct_temp_sts = true;
      } else if (!strcmp(optarg,"scttemphist")) {
        ataopts.sct_temp_hist = true;

      } else if (!strncmp(optarg, "xerror", sizeof("xerror")-1)) {
        int n1 = -1, n2 = -1, len = strlen(optarg);
        unsigned val = 8;
        sscanf(optarg, "xerror%n,error%n", &n1, &n2);
        if (!(n1 == len || n2 == len)) {
          n1 = n2 = -1;
          sscanf(optarg, "xerror,%u%n,error%n", &val, &n1, &n2);
        }
        if ((n1 == len || n2 == len) && val > 0) {
          ataopts.smart_ext_error_log = val;
          ataopts.retry_error_log = (n2 == len);
        }
        else
          badarg = true;

      } else if (!strncmp(optarg, "xselftest", sizeof("xselftest")-1)) {
        int n1 = -1, n2 = -1, len = strlen(optarg);
        unsigned val = 25;
        sscanf(optarg, "xselftest%n,selftest%n", &n1, &n2);
        if (!(n1 == len || n2 == len)) {
          n1 = n2 = -1;
          sscanf(optarg, "xselftest,%u%n,selftest%n", &val, &n1, &n2);
        }
        if ((n1 == len || n2 == len) && val > 0) {
          ataopts.smart_ext_selftest_log = val;
          ataopts.retry_selftest_log = (n2 == len);
        }
        else
          badarg = true;

      } else if (   !strncmp(optarg, "gplog,"   , sizeof("gplog,"   )-1)
                 || !strncmp(optarg, "smartlog,", sizeof("smartlog,")-1)) {
        unsigned logaddr = ~0U; unsigned page = 0, nsectors = 1; char sign = 0;
        int n1 = -1, n2 = -1, n3 = -1, len = strlen(optarg);
        sscanf(optarg, "%*[a-z],0x%x%n,%u%n%c%u%n",
               &logaddr, &n1, &page, &n2, &sign, &nsectors, &n3);
        if (len > n2 && n3 == -1 && !strcmp(optarg+n2, "-max")) {
          nsectors = ~0U; sign = '+'; n3 = len;
        }
        bool gpl = (optarg[0] == 'g');
        const char * erropt = (gpl ? "gplog" : "smartlog");
        if (!(   n1 == len || n2 == len
              || (n3 == len && (sign == '+' || sign == '-')))) {
          sprintf(extraerror, "Option -l %s,ADDR[,FIRST[-LAST|+SIZE]] syntax error\n", erropt);
          badarg = true;
        }
        else if (!(    logaddr <= 0xff && page <= (gpl ? 0xffffU : 0x00ffU)
                   && 0 < nsectors
                   && (nsectors <= (gpl ? 0xffffU : 0xffU) || nsectors == ~0U)
                   && (sign != '-' || page <= nsectors)                       )) {
          sprintf(extraerror, "Option -l %s,ADDR[,FIRST[-LAST|+SIZE]] parameter out of range\n", erropt);
          badarg = true;
        }
        else {
          ata_log_request req;
          req.gpl = gpl; req.logaddr = logaddr; req.page = page;
          req.nsectors = (sign == '-' ? nsectors-page+1 : nsectors);
          ataopts.log_requests.push_back(req);
        }
      } else {
        badarg = true;
      }
      break;
    case 'i':
      ataopts.drive_info = scsiopts.drive_info = true;
      break;
    case 'a':
      ataopts.drive_info           = scsiopts.drive_info          = true;
      ataopts.smart_check_status   = scsiopts.smart_check_status  = true;
      ataopts.smart_general_values = true;
      ataopts.smart_vendor_attrib  = scsiopts.smart_vendor_attrib = true;
      ataopts.smart_error_log      = scsiopts.smart_error_log     = true;
      ataopts.smart_selftest_log   = scsiopts.smart_selftest_log  = true;
      ataopts.smart_selective_selftest_log = true;
      /* scsiopts.smart_background_log = true; */
      break;
    case 'x':
      ataopts.drive_info           = scsiopts.drive_info          = true;
      ataopts.smart_check_status   = scsiopts.smart_check_status  = true;
      ataopts.smart_general_values = true;
      ataopts.smart_vendor_attrib  = scsiopts.smart_vendor_attrib = true;
      ataopts.smart_ext_error_log  = 8;
      ataopts.retry_error_log      = true;
      ataopts.smart_ext_selftest_log = 25;
      ataopts.retry_selftest_log   = true;
      scsiopts.smart_error_log     = scsiopts.smart_selftest_log    = true;
      ataopts.smart_selective_selftest_log = true;
      ataopts.smart_logdir = ataopts.gp_logdir = true;
      ataopts.sct_temp_sts = ataopts.sct_temp_hist = true;
      ataopts.sataphy = true;
      scsiopts.smart_background_log = true;
      scsiopts.sasphy = true;
      break;
    case 'v':
      // parse vendor-specific definitions of attributes
      if (!strcmp(optarg,"help")) {
        con->dont_print = false;
        printslogan();
        pout("The valid arguments to -v are:\n\thelp\n%s\n",
             create_vendor_attribute_arg_list().c_str());
        EXIT(0);
      }
      if (!parse_attribute_def(optarg, ataopts.attribute_defs, PRIOR_USER))
        badarg = true;
      break;    
    case 'P':
      if (!strcmp(optarg, "use")) {
        ataopts.ignore_presets = false;
      } else if (!strcmp(optarg, "ignore")) {
        ataopts.ignore_presets = true;
      } else if (!strcmp(optarg, "show")) {
        ataopts.show_presets = true;
      } else if (!strcmp(optarg, "showall")) {
        if (!no_defaultdb && !read_default_drive_databases())
          EXIT(FAILCMD);
        if (optind < argc) { // -P showall MODEL [FIRMWARE]
          int cnt = showmatchingpresets(argv[optind], (optind+1<argc ? argv[optind+1] : NULL));
          EXIT(cnt); // report #matches
        }
        if (showallpresets())
          EXIT(FAILCMD); // report regexp syntax error
        EXIT(0);
      } else {
        badarg = true;
      }
      break;
    case 't':
      if (!strcmp(optarg,"offline")) {
        testcnt++;
        ataopts.smart_selftest_type = OFFLINE_FULL_SCAN;
        scsiopts.smart_default_selftest = true;
      } else if (!strcmp(optarg,"short")) {
        testcnt++;
        ataopts.smart_selftest_type = SHORT_SELF_TEST;
        scsiopts.smart_short_selftest = true;
      } else if (!strcmp(optarg,"long")) {
        testcnt++;
        ataopts.smart_selftest_type = EXTEND_SELF_TEST;
        scsiopts.smart_extend_selftest = true;
      } else if (!strcmp(optarg,"conveyance")) {
        testcnt++;
        ataopts.smart_selftest_type = CONVEYANCE_SELF_TEST;
      } else if (!strcmp(optarg,"afterselect,on")) {
        // scan remainder of disk after doing selected segment
        ataopts.smart_selective_args.scan_after_select = 2;
      } else if (!strcmp(optarg,"afterselect,off")) {
        // don't scan remainder of disk after doing selected segments
        ataopts.smart_selective_args.scan_after_select = 1;
      } else if (!strncmp(optarg,"pending,",strlen("pending,"))) {
	// parse number of minutes that test should be pending
	int i;
	char *tailptr=NULL;
	errno=0;
	i=(int)strtol(optarg+strlen("pending,"), &tailptr, 10);
	if (errno || *tailptr != '\0') {
	  sprintf(extraerror, "Option -t pending,N requires N to be a non-negative integer\n");
          badarg = true;
	} else if (i<0 || i>65535) {
	  sprintf(extraerror, "Option -t pending,N (N=%d) must have 0 <= N <= 65535\n", i);
          badarg = true;
	} else {
          ataopts.smart_selective_args.pending_time = i+1;
	}
      } else if (!strncmp(optarg,"select",strlen("select"))) {
        testcnt++;
        // parse range of LBAs to test
        uint64_t start, stop; int mode;
        if (split_selective_arg(optarg, &start, &stop, &mode)) {
	  sprintf(extraerror, "Option -t select,M-N must have non-negative integer M and N\n");
          badarg = true;
        } else {
          if (ataopts.smart_selective_args.num_spans >= 5 || start > stop) {
            if (start > stop) {
              sprintf(extraerror, "ERROR: Start LBA (%"PRIu64") > ending LBA (%"PRId64") in argument \"%s\"\n",
                start, stop, optarg);
            } else {
              sprintf(extraerror,"ERROR: No more than five selective self-test spans may be"
                " defined\n");
            }
            badarg = true;
          }
          ataopts.smart_selective_args.span[ataopts.smart_selective_args.num_spans].start = start;
          ataopts.smart_selective_args.span[ataopts.smart_selective_args.num_spans].end   = stop;
          ataopts.smart_selective_args.span[ataopts.smart_selective_args.num_spans].mode  = mode;
          ataopts.smart_selective_args.num_spans++;
          ataopts.smart_selftest_type = SELECTIVE_SELF_TEST;
        }
      } else if (!strncmp(optarg, "scttempint,", sizeof("scstempint,")-1)) {
        unsigned interval = 0; int n1 = -1, n2 = -1, len = strlen(optarg);
        if (!(   sscanf(optarg,"scttempint,%u%n,p%n", &interval, &n1, &n2) == 1
              && 0 < interval && interval <= 0xffff && (n1 == len || n2 == len))) {
            strcpy(extraerror, "Option -t scttempint,N[,p] must have positive integer N\n");
            badarg = true;
        }
        ataopts.sct_temp_int = interval;
        ataopts.sct_temp_int_pers = (n2 == len);
      } else {
        badarg = true;
      }
      break;
    case 'C':
      captive = true;
      break;
    case 'X':
      testcnt++;
      scsiopts.smart_selftest_abort = true;
      ataopts.smart_selftest_type = ABORT_SELF_TEST;
      break;
    case 'n':
      // skip disk check if in low-power mode
      if (!strcmp(optarg, "never"))
        ataopts.powermode = 1; // do not skip, but print mode
      else if (!strcmp(optarg, "sleep"))
        ataopts.powermode = 2;
      else if (!strcmp(optarg, "standby"))
        ataopts.powermode = 3;
      else if (!strcmp(optarg, "idle"))
        ataopts.powermode = 4;
      else
        badarg = true;
      break;
    case 'B':
      {
        const char * path = optarg;
        if (*path == '+' && path[1])
          path++;
        else
          no_defaultdb = true;
        if (!read_drive_database(path))
          EXIT(FAILCMD);
      }
      break;
    case 'h':
      con->dont_print = false;
      printslogan();
      Usage();
      EXIT(0);  
      break;
    case '?':
    default:
      con->dont_print = false;
      printslogan();
      // Point arg to the argument in which this option was found.
      arg = argv[optind-1];
      // Check whether the option is a long option that doesn't map to -h.
      if (arg[1] == '-' && optchar != 'h') {
        // Iff optopt holds a valid option then argument must be missing.
        if (optopt && (strchr(shortopts, optopt) != NULL)) {
          pout("=======> ARGUMENT REQUIRED FOR OPTION: %s\n", arg+2);
          printvalidarglistmessage(optopt);
        } else
          pout("=======> UNRECOGNIZED OPTION: %s\n",arg+2);
	if (extraerror[0])
	  pout("=======> %s", extraerror);
        UsageSummary();
        EXIT(FAILCMD);
      }
      if (optopt) {
        // Iff optopt holds a valid option then argument must be
        // missing.  Note (BA) this logic seems to fail using Solaris
        // getopt!
        if (strchr(shortopts, optopt) != NULL) {
          pout("=======> ARGUMENT REQUIRED FOR OPTION: %c\n", optopt);
          printvalidarglistmessage(optopt);
        } else
          pout("=======> UNRECOGNIZED OPTION: %c\n",optopt);
	if (extraerror[0])
	  pout("=======> %s", extraerror);
        UsageSummary();
        EXIT(FAILCMD);
      }
      Usage();
      EXIT(0);  
    } // closes switch statement to process command-line options
    
    // Check to see if option had an unrecognized or incorrect argument.
    if (badarg) {
      printslogan();
      // It would be nice to print the actual option name given by the user
      // here, but we just print the short form.  Please fix this if you know
      // a clean way to do it.
      pout("=======> INVALID ARGUMENT TO -%c: %s\n", optchar, optarg);
      printvalidarglistmessage(optchar);
      if (extraerror[0])
	pout("=======> %s", extraerror);
      UsageSummary();
      EXIT(FAILCMD);
    }
  }
  // At this point we have processed all command-line options.  If the
  // print output is switchable, then start with the print output
  // turned off
  if (con->printing_switchable)
    con->dont_print = true;

  // error message if user has asked for more than one test
  if (testcnt > 1) {
    con->dont_print = false;
    printslogan();
    pout("\nERROR: smartctl can only run a single test type (or abort) at a time.\n");
    UsageSummary();
    EXIT(FAILCMD);
  }

  // error message if user has set selective self-test options without
  // asking for a selective self-test
  if (   (ataopts.smart_selective_args.pending_time || ataopts.smart_selective_args.scan_after_select)
      && !ataopts.smart_selective_args.num_spans) {
    con->dont_print = false;
    printslogan();
    if (ataopts.smart_selective_args.pending_time)
      pout("\nERROR: smartctl -t pending,N must be used with -t select,N-M.\n");
    else
      pout("\nERROR: smartctl -t afterselect,(on|off) must be used with -t select,N-M.\n");
    UsageSummary();
    EXIT(FAILCMD);
  }

  // If captive option was used, change test type if appropriate.
  if (captive)
    switch (ataopts.smart_selftest_type) {
      case SHORT_SELF_TEST:
        ataopts.smart_selftest_type = SHORT_CAPTIVE_SELF_TEST;
        scsiopts.smart_short_selftest     = false;
        scsiopts.smart_short_cap_selftest = true;
        break;
      case EXTEND_SELF_TEST:
        ataopts.smart_selftest_type = EXTEND_CAPTIVE_SELF_TEST;
        scsiopts.smart_extend_selftest     = false;
        scsiopts.smart_extend_cap_selftest = true;
        break;
      case CONVEYANCE_SELF_TEST:
        ataopts.smart_selftest_type = CONVEYANCE_CAPTIVE_SELF_TEST;
        break;
      case SELECTIVE_SELF_TEST:
        ataopts.smart_selftest_type = SELECTIVE_CAPTIVE_SELF_TEST;
        break;
    }

  // From here on, normal operations...
  printslogan();
  
  // Warn if the user has provided no device name
  if (argc-optind<1){
    pout("ERROR: smartctl requires a device name as the final command-line argument.\n\n");
    UsageSummary();
    EXIT(FAILCMD);
  }
  
  // Warn if the user has provided more than one device name
  if (argc-optind>1){
    int i;
    pout("ERROR: smartctl takes ONE device name as the final command-line argument.\n");
    pout("You have provided %d device names:\n",argc-optind);
    for (i=0; i<argc-optind; i++)
      pout("%s\n",argv[optind+i]);
    UsageSummary();
    EXIT(FAILCMD);
  }

  // Read or init drive database
  if (!no_defaultdb && !read_default_drive_databases())
    EXIT(FAILCMD);

  return type;
}

// Printing function (controlled by global con->dont_print) 
// [From GLIBC Manual: Since the prototype doesn't specify types for
// optional arguments, in a call to a variadic function the default
// argument promotions are performed on the optional argument
// values. This means the objects of type char or short int (whether
// signed or not) are promoted to either int or unsigned int, as
// appropriate.]
void pout(const char *fmt, ...){
  va_list ap;
  
  // initialize variable argument list 
  va_start(ap,fmt);
  if (con->dont_print){
    va_end(ap);
    return;
  }

  // print out
  vprintf(fmt,ap);
  va_end(ap);
  fflush(stdout);
  return;
}

// This function is used by utility.cpp to report LOG_CRIT errors.
// The smartctl version prints to stdout instead of syslog().
void PrintOut(int priority, const char *fmt, ...) {
  va_list ap;

  // avoid warning message about unused variable from gcc -W: just
  // change value of local copy.
  priority=0;

  va_start(ap,fmt);
  vprintf(fmt,ap);
  va_end(ap);
  return;
}

// Used to warn users about invalid checksums. Called from atacmds.cpp.
// Action to be taken may be altered by the user.
void checksumwarning(const char * string)
{
  // user has asked us to ignore checksum errors
  if (checksum_err_mode == CHECKSUM_ERR_IGNORE)
    return;

  pout("Warning! %s error: invalid SMART checksum.\n", string);

  // user has asked us to fail on checksum errors
  if (checksum_err_mode == CHECKSUM_ERR_EXIT)
    EXIT(FAILSMART);
}

// Return info string about device protocol
static const char * get_protocol_info(const smart_device * dev)
{
  switch ((int)dev->is_ata() | ((int)dev->is_scsi() << 1)) {
    case 0x1: return "ATA";
    case 0x2: return "SCSI";
    case 0x3: return "ATA+SCSI";
    default:  return "Unknown";
  }
}

// Main program without exception handling
int main_worker(int argc, char **argv)
{
  // Initialize interface
  smart_interface::init();
  if (!smi())
    return 1;

  // define control block for external functions
  smartmonctrl control;
  con=&control;

  // Parse input arguments
  ata_print_options ataopts;
  scsi_print_options scsiopts;
  const char * type = parse_options(argc, argv, ataopts, scsiopts);

  // '-d test' -> Report result of autodetection
  bool print_type_only = (type && !strcmp(type, "test"));
  if (print_type_only)
    type = 0;

  const char * name = argv[argc-1];

  smart_device_auto_ptr dev;
  if (!strcmp(name,"-")) {
    // Parse "smartctl -r ataioctl,2 ..." output from stdin
    if (type || print_type_only) {
      pout("Smartctl: -d option is not allowed in conjunction with device name \"-\".\n");
      UsageSummary();
      return FAILCMD;
    }
    dev = get_parsed_ata_device(smi(), name);
  }
  else
    // get device of appropriate type
    dev = smi()->get_smart_device(name, type);

  if (!dev) {
    pout("%s: %s\n", name, smi()->get_errmsg());
    if (type)
      printvalidarglistmessage('d');
    else
      pout("Smartctl: please specify device type with the -d option.\n");
    UsageSummary();
    return FAILCMD;
  }

  if (print_type_only)
    // Report result of first autodetection
    pout("%s: Device of type '%s' [%s] detected\n",
         dev->get_info_name(), dev->get_dev_type(), get_protocol_info(dev.get()));

  // Open device
  {
    // Save old info
    smart_device::device_info oldinfo = dev->get_info();

    // Open with autodetect support, may return 'better' device
    dev.replace( dev->autodetect_open() );

    // Report if type has changed
    if ((type || print_type_only) && oldinfo.dev_type != dev->get_dev_type())
      pout("%s: Device open changed type from '%s' to '%s'\n",
        dev->get_info_name(), oldinfo.dev_type.c_str(), dev->get_dev_type());
  }
  if (!dev->is_open()) {
    pout("Smartctl open device: %s failed: %s\n", dev->get_info_name(), dev->get_errmsg());
    return FAILDEV;
  }

  // now call appropriate ATA or SCSI routine
  int retval = 0;
  if (print_type_only)
    pout("%s: Device of type '%s' [%s] opened\n",
         dev->get_info_name(), dev->get_dev_type(), get_protocol_info(dev.get()));
  else if (dev->is_ata())
    retval = ataPrintMain(dev->to_ata(), ataopts);
  else if (dev->is_scsi())
    retval = scsiPrintMain(dev->to_scsi(), scsiopts);
  else
    // we should never fall into this branch!
    pout("%s: Neither ATA nor SCSI device\n", dev->get_info_name());

  dev->close();
  return retval;
}


// Main program
int main(int argc, char **argv)
{
  int status;
  try {
    // Do the real work ...
    status = main_worker(argc, argv);
  }
  catch (int ex) {
    // EXIT(status) arrives here
    status = ex;
  }
  catch (const std::bad_alloc & /*ex*/) {
    // Memory allocation failed (also thrown by std::operator new)
    printf("Smartctl: Out of memory\n");
    status = FAILCMD;
  }
  catch (const std::exception & ex) {
    // Other fatal errors
    printf("Smartctl: Exception: %s\n", ex.what());
    status = FAILCMD;
  }
  return status;
}

