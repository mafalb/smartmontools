/*
 * smartd.h
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2002-8 Bruce Allen <smartmontools-support@lists.sourceforge.net>
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

#ifndef SMARTD_H_
#define SMARTD_H_

// Needed since some structure definitions below require POSIX
// extended regular expressions.
#include <sys/types.h>
#include <regex.h>


#ifndef SMARTD_H_CVSID
#define SMARTD_H_CVSID "$Id: smartd.h,v 1.86 2008/03/04 22:09:47 ballen4705 Exp $\n"
#endif

// Configuration file
#define CONFIGFILENAME "smartd.conf"

// Scan directive for configuration file
#define SCANDIRECTIVE "DEVICESCAN"

// maximum line length in configuration file
#define MAXLINELEN 256

// maximum length of a continued line in configuration file
#define MAXCONTLINE 1023

// default for how often SMART status is checked, in seconds
#define CHECKTIME 1800

/* Boolean Values */
#define TRUE 0x01
#define FALSE 0x00

// Number of monitoring flags per Attribute and offsets.  See
// monitorattflags below.
#define NMONITOR 4
#define MONITOR_FAILUSE   0
#define MONITOR_IGNORE    1
#define MONITOR_RAWPRINT  2
#define MONITOR_RAW       3


// Number of allowed mail message types
#define SMARTD_NMAIL 13

typedef struct mailinfo_s {
  int logged;// number of times an email has been sent
  time_t firstsent;// time first email was sent, as defined by time(2)
  time_t lastsent; // time last email was sent, as defined by time(2)
} mailinfo;

// If user has requested email warning messages, then this structure
// stores the information about them, and track type/date of email
// messages.
typedef struct maildata_s {
  mailinfo maillog[SMARTD_NMAIL];         // log info on when mail sent
  char *emailcmdline;                     // script to execute
  char *address;                          // email address, or null
  unsigned char emailfreq;                // Emails once (1) daily (2) diminishing (3)
  unsigned char emailtest;                // Send test email?
} maildata;

// If user has requested automatic testing, then this structure stores
// their regular expression pattern, the compiled form of that regex,
// and information about the disk capabilities and when the last text
// took place

typedef struct testinfo_s {
  char *regex;                    // text form of regex
  regex_t cregex;                 // compiled form of regex
  unsigned short hour;            // 1+hour of year when last scheduled self-test done
  char testtype;                  // type of test done at hour indicated just above
  signed char not_cap_offline;    // 0==unknown OR capable of offline, 1==not capable 
  signed char not_cap_conveyance;
  signed char not_cap_short;
  signed char not_cap_long;
} testinfo;


// cfgfile is the main data structure of smartd. It is used in two
// ways.  First, to store a list of devices/options given in the
// configuration smartd.conf or constructed with DEVICESCAN.  And
// second, to point to or provide all persistent storage needed to
// track a device, if registered either as SCSI or ATA.
// 
// After parsing the config file, each valid entry has a cfgfile data
// structure allocated in memory for it.  In parsing the configuration
// file, some storage space may be needed, of indeterminate length,
// for example for the device name.  When this happens, memory should
// be allocated and then pointed to from within the corresponding
// cfgfile structure.

// After parsing the configuration file, each device is then checked
// to see if it can be monitored (this process is called "registering
// the device".  This is done in [scsi|ata]devicescan, which is called
// exactly once, after the configuration file has been parsed and
// cfgfile data structures have been created for each of its entries.
//
// If a device can not be monitored, the memory for its cfgfile data
// structure should be freed by calling rmconfigentry(cfgfile *). In
// this case, we say that this device "was not registered".  All
// memory associated with that particular cfgfile structure is thus
// freed.
// 
// The remaining devices are polled in a timing look, where
// [ata|scsi]CheckDevice looks at each entry in turn.
// 
// If you want to add small amounts of "private" data on a per-device
// basis, just make a new field in cfgfile.  This is guaranteed zero
// on startup (when ata|scsi]scsidevicescan(cfgfile *cfg) is first
// called with a pointer to cfgfile.
// 
// If you need *substantial* persistent data space for a device
// (dozens or hundreds of bytes) please add a pointer field to
// cfgfile.  As before, this is guaranteed NULL when
// ata|scsi]scsidevicescan(cfgfile *cfg) is called. Allocate space for
// it in scsidevicescan or atadevicescan, if needed, and deallocate
// the space in rmconfigentry(cfgfile *cfg). Be sure to make the
// pointer NULL unless it points to an area of the heap that can be
// deallocated with free().  In other words, a non-NULL pointer in
// cfgfile means "this points to data space that should be freed if I
// stop monitoring this device." If you don't need the space anymore,
// please call free() and then SET THE POINTER IN cfgfile TO NULL.
// 
// Note that we allocate one cfgfile structure per device.  This is
// why substantial persisent data storage should only be pointed to
// from within cfgfile, not kept within cfgfile itself - it saves
// memory for those devices that don't need that type of persistent
// data.
// 
// In general, the capabilities of devices should be checked at
// registration time within atadevicescan() and scsidevicescan(), and
// then noted within *cfg.  So if device lacks some capability, this
// should be visible within *cfg after returning from
// [ata|scsi]devicescan.
// 
// Devices are then checked, once per polling interval, within
// ataCheckDevice() and scsiCheckDevice().  These should only check
// the capabilities that devices already are known to have (as noted
// within *cfg).

typedef struct configfile_s {
  // FIRST SET OF ENTRIES CORRESPOND TO WHAT THE USER PUT IN THE
  // CONFIG FILE.  SOME ENTRIES MAY BE MODIFIED WHEN A DEVICE IS
  // REGISTERED AND WE LEARN ITS CAPABILITIES.
  int lineno;                             // Line number of entry in file
  char *name;                             // Device name (+ optional [3ware_disk_XX])
  unsigned char controller_explicit;      // Controller (device) type has been specified explicitly
  unsigned char controller_type;          // Controller type, ATA/SCSI/SAT/3Ware/(more to come)
  unsigned char controller_port;          // 1 + (disk number in controller). 0 means controller only handles one disk.
  unsigned char hpt_data[3];              // combined controller/channle/pmport for highpoint rocketraid controller
  unsigned char satpassthrulen;           // length of SAT ata pass through scsi commands (12, 16 or 0 (platform choice))
  char smartcheck;                        // Check SMART status
  char usagefailed;                       // Check for failed Usage Attributes
  char prefail;                           // Track changes in Prefail Attributes
  char usage;                             // Track changes in Usage Attributes
  char selftest;                          // Monitor number of selftest errors
  char errorlog;                          // Monitor number of ATA errors
  char permissive;                        // Ignore failed SMART commands
  char autosave;                          // 1=disable, 2=enable Autosave Attributes
  char autoofflinetest;                   // 1=disable, 2=enable Auto Offline Test
  unsigned char fixfirmwarebug;           // Fix firmware bug
  char ignorepresets;                     // Ignore database of -v options
  char showpresets;                       // Show database entry for this device
  char removable;                         // Device may disappear (not be present)
  char powermode;                         // skip check, if disk in idle or standby mode
  char powerquiet;                        // skip powermode 'skipping checks' message
  unsigned char tempdiff;                 // Track Temperature changes >= this limit
  unsigned char tempinfo, tempcrit;       // Track Temperatures >= these limits as LOG_INFO, LOG_CRIT+mail
  unsigned char tempmin, tempmax;         // Min/Max Temperatures
  unsigned char selflogcount;             // total number of self-test errors
  unsigned short selfloghour;             // lifetime hours of last self-test error
  testinfo *testdata;                     // Pointer to data on scheduled testing
  unsigned short pending;                 // lower 8 bits: ID of current pending sector count
                                          // upper 8 bits: ID of offline pending sector count
  
  // THE NEXT SET OF ENTRIES ALSO TRACK DEVICE STATE AND ARE DYNAMIC
  maildata *mailwarn;                     // non-NULL: info about sending mail or executing script
  unsigned char temperature;              // last recorded Temperature (in Celsius)
  unsigned char tempmininc;               // #checks where Min Temperature is increased after powerup
  int powerskipcnt;                       // Number of checks skipped due to idle or standby mode

  // SCSI ONLY
  unsigned char SmartPageSupported;       // has log sense IE page (0x2f)
  unsigned char TempPageSupported;        // has log sense temperature page (0xd)
  unsigned char SuppressReport;           // minimize nuisance reports
  unsigned char modese_len;               // mode sense/select cmd len: 0 (don't
                                          // know yet) 6 or 10
  unsigned char notused2[3];              // for packing alignment

  // ATA ONLY FROM HERE ON TO THE END
  int ataerrorcount;                      // Total number of ATA errors
  
  // following NMONITOR items each point to 32 bytes, in the form of
  // 32x8=256 single bit flags 
  // valid attribute numbers are from 1 <= x <= 255
  // monitorattflags+0  set: ignore failure for a usage attribute
  // monitorattflats+32 set: don't track attribute
  // monitorattflags+64 set: print raw value when tracking
  // monitorattflags+96 set: track changes in raw value
  unsigned char *monitorattflags;

  // NULL UNLESS (1) STORAGE IS ALLOCATED WHEN CONFIG FILE SCANNED
  // (SET BY USER) or (2) IT IS SET WHEN DRIVE IS AUTOMATICALLY
  // RECOGNIZED IN DATABASE (WHEN DRIVE IS REGISTERED)
  unsigned char *attributedefs;            // -v options, see end of extern.h for def

  // ATA ONLY - SAVE SMART DATA. NULL POINTERS UNLESS NEEDED.  IF
  // NEEDED, ALLOCATED WHEN DEVICE REGISTERED.
  struct ata_smart_values *smartval;       // Pointer to SMART data
  struct ata_smart_thresholds_pvt *smartthres; // Pointer to SMART thresholds

  // Added to distinguish ATA and SCSI entries on list
  int WhichCheckDevice;

} cfgfile;


typedef struct changedattribute_s {
  unsigned char newval;
  unsigned char oldval;
  unsigned char id;
  unsigned char prefail;
  unsigned char sameraw;
} changedattribute_t;

// Declare our own printing functions. Doing this provides error
// messages if the argument number/types don't match the format.
#ifndef __GNUC__
#define __attribute__(x)      /* nothing */
#endif
void PrintOut(int priority, const char *fmt, ...) __attribute__ ((format(printf, 2, 3)));

void PrintAndMail(cfgfile *cfg, int which, int priority, char *fmt, ...) __attribute__ ((format(printf, 4, 5)));   

/* Debugging notes: to check for memory allocation/deallocation problems, use:

export LD_PRELOAD=libnjamd.so;
export NJAMD_PROT=strict;           
export NJAMD_CHK_FREE=error;
export NJAMD_DUMP_LEAKS_ON_EXIT=num;
export NJAMD_DUMP_LEAKS_ON_EXIT=3;
export NJAMD_TRACE_LIBS=1

*/

// Number of seconds to allow for registering a SCSI device. If this
// time expires without sucess or failure, then treat it as failure.
// Set to 0 to eliminate this timeout feature from the code
// (equivalent to an infinite timeout interval).
#define SCSITIMEOUT 0

// This is for solaris, where signal() resets the handler to SIG_DFL
// after the first signal is caught.
#ifdef HAVE_SIGSET
#define SIGNALFN sigset
#else
#define SIGNALFN signal
#endif


#define SELFTEST_ERRORCOUNT(x) (x & 0xff)
#define SELFTEST_ERRORHOURS(x) ((x >> 8) & 0xffff)

// cfg->pending is a 16 bit unsigned quantity.  If the least
// significant 8 bits are zero, this means monitor Attribute
// CUR_UNC_DEFAULT's raw value.  If they are CUR_UNC_DEFAULT, this
// means DON'T MONITOR.  If the most significant 8 bits are zero, this
// means monitor Attribute OFF_UNC_DEFAULT's raw value.  If they are
// OFF_UNC_DEFAULT, this means DON'T MONITOR.
#define OFF_UNC_DEFAULT 198
#define CUR_UNC_DEFAULT 197

#define CURR_PEND(x) (x & 0xff)
#define OFF_PEND(x) ((x >> 8) & 0xff)

// if cfg->pending has this value, dont' monitor
#define DONT_MONITOR_UNC (256*OFF_UNC_DEFAULT+CUR_UNC_DEFAULT)

// Some return values from SCSIFilterKnown(), used to detect known
// device types hidden behind SCSI devices.

#define SCSIFK_FAILED   -1
#define SCSIFK_NORMAL    0
#define SCSIFK_3WARE    11
#define SCSIFK_SAT      12
#define SCSIFK_MARVELL  13

// Make additions BEFORE this line.  The line is the end of
// double-inclusion protection and should remain the final line.
#endif  // #ifndef SMARTD_H_
