/*
 * scsiprint.c
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2002 Bruce Allen <smartmontools-support@lists.sourceforge.net>
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


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include "smartctl.h"
#include "scsicmds.h"
#include "scsiprint.h"
#include "extern.h"

#define GBUF_SIZE 65535

const char* CVSid4="$Id: scsiprint.cpp,v 1.15 2003/01/05 15:24:44 ballen4705 Exp $"
CVSID3 CVSID4 CVSID5 CVSID6;

// control block which points to external global control variables
extern atamainctrl *con;

UINT8 gBuf[GBUF_SIZE];

UINT8 gSmartPage = 0;
UINT8 gTempPage = 0;
UINT8 gSelfTestPage = 0;
UINT8 gStartStopPage = 0;
UINT8 gTapeAlertsPage = 0;





void scsiGetSupportPages ( int device)
{
   int i;

   if (logsense ( device , SUPPORT_LOG_PAGES, (UINT8 *) &gBuf) != 0)
   {
       perror ( "Log Sense failed"); 
       exit (1);
   } 

   for ( i = 4; i < gBuf[3] + LOGPAGEHDRSIZE ; i++)
   {
      switch ( gBuf[i])
      {
         case TEMPERATURE_PAGE:
            gTempPage = 1;
            break;
         case STARTSTOP_CYCLE_COUNTER_PAGE:
            gStartStopPage = 1;
            break;
         case SELFTEST_RESULTS_PAGE:
            gSelfTestPage = 1;
            break;
         case SMART_PAGE:
            gSmartPage = 1;
            break;
         case TAPE_ALERTS_PAGE:
            gTapeAlertsPage = 1;
            break;
         default:
            break;

       }
   }
}

void scsiGetSmartData (int device)
{

   UINT8 returnvalue;
   UINT8 currenttemp;
   UINT8 triptemp;

   if ( scsiCheckSmart(device, gSmartPage, 
                       &returnvalue, &currenttemp, &triptemp ) != 0)
   {
      perror ( "scsiGetSmartData Failed");
      exit (1);
   }
	
   if ( returnvalue )
      printf("S.M.A.R.T. Sense: (%02x) %s\n", (UINT8) returnvalue, 
                scsiSmartGetSenseCode(returnvalue));
   else
      printf("S.M.A.R.T. Sense: Ok!\n");

   if ( (currenttemp || triptemp) && !gTempPage)
   {
      printf("Current Drive Temperature:     %d C\n", currenttemp);
      printf("Drive Trip Temperature:        %d C\n", triptemp);
   }
}


void scsiGetTapeAlertsData (int device)
{
    unsigned short pagelength;
    unsigned short parametercode;
    int i;
    int failure = 0;

    if ( logsense( device, TAPE_ALERTS_PAGE, (UINT8 *) &gBuf) != 0)
    {
   	     perror ( "scsiGetSmartData Failed");
	     exit (1);
    }

    if ( gBuf[0] != 0x2e )
    {
         printf("TapeAlerts Log Sense Failed\n");
         exit(-1);
    }

    pagelength = (unsigned short) gBuf[2] << 8 | gBuf[3];

    for ( i = 4; i < pagelength;i+=5 )
    {
        parametercode = (unsigned short) gBuf[i] << 8 | gBuf[i+1];

        if (gBuf[i+4])
        {
           printf("Tape Alerts Error!!!\n%s\n",
              scsiTapeAlertsTapeDevice(parametercode));
           failure = 1; 
        }          
    }

    if(!failure)
      printf("No Tape Alerts Failure\n");

}

void scsiGetStartStopData ( int device)
{
    UINT32 currentStartStop;
    UINT32 recommendedStartStop; 

    if ( logsense( device, STARTSTOP_CYCLE_COUNTER_PAGE, (UINT8 *) &gBuf) != 0)
    {
   	     perror ( "scsiGetStartStopData Failed");
	     exit (1);
    }


    if ( gBuf[0] != STARTSTOP_CYCLE_COUNTER_PAGE )
    {
         printf("StartStop Log Sense Failed\n");
         exit(-1);
    }


    recommendedStartStop= (UINT32) gBuf[28]<< 24 | gBuf[29] << 16 |
                                       gBuf[30] << 8 | gBuf[31];
    currentStartStop= (UINT32) gBuf[36]<< 24 | gBuf[37] << 16 |
                                       gBuf[38] << 8 | gBuf[39];

    printf("Current start stop count:      %u times\n", currentStartStop);
    printf("Recommended start stop count:  %u times\n", recommendedStartStop);
} 

const char * self_test_code[] = {
        "Default         ", 
	"Background short", 
	"Background long ", 
	"Reserved(3)     ",
	"Abort background", 
	"Foreground short", 
	"Foreground long ",
	"Reserved(7)     "
};

const char * self_test_result[] = {
        "Completed                ",
	"Interrupted ('-X' switch)",
	"Interrupted (bus reset ?)",
	"Unknown error, incomplete",
	"Completed, segment failed",
	"Failed in first segment  ",
	"Failed in second segment ",
	"Failed in segment -->    ",
	"Reserved(8)              ", 
	"Reserved(9)              ", 
	"Reserved(10)             ", 
	"Reserved(11)             ", 
	"Reserved(12)             ", 
	"Reserved(13)             ", 
	"Reserved(14)             ",
	"Self test in progress ..."
};

// See Working Draft SCSI Primary Commands - 3 (SPC-3) pages 231-233
// T10/1416-D Rev 10
void  scsiPrintSelfTest(int device)
{
	int num, k, n, res, noheader=1;
	UINT8 * ucp;
	unsigned long long ull=0;

	if (logsense(device, SELFTEST_RESULTS_PAGE, gBuf) != 0)
	{
		perror ( "scsiPrintSelfTest Failed");
		exit (1);
	}
	if (gBuf[0] != SELFTEST_RESULTS_PAGE)
	{
		printf("Self-test Log Sense Failed\n");
		exit(1);
	}

	// compute page length
	num = (gBuf[2] << 8) + gBuf[3];

	// how come not num != 0x190 log sense wrong length?
	if (num < 0x190) {
		printf("Self-test Log Sense too short\n");
		exit(1);
	}

	// start address of first self-test result 
	// not needed initialized later in for () statement
	// ucp = &gBuf[0] + 4;

	// loop through the twenty possible entries
	for (k = 0, ucp = gBuf + 4; k < 20; ++k, ucp += 20 ) {
	  int i;

	  // timestamp in power-on hours (or zero if test in progress)
	  n = (ucp[6] << 8) | ucp[7];

	  // if no test was done, we are finished.  Should we check
	  // that all 20 bytes are zero?
	  if ((0 == n) && (0 == ucp[4]))
	    break;

	  // only print header if needed
	  if (noheader){
	    printf(",\nSMART Self-test log\n");
	    printf("Num  Test              Status                 segment  "
		   "LifeTime  LBA_first_err [SK ASC ASQ]\n");
	    printf("     Description                              number   "
		   "(hours)\n");
	    noheader=0;
	  }

	  // are the 20-byte blocks stored in increasing order of
	  // parameter code?  I think so.... this code assumes it.

	  // print parameter code (test number) & self-test code text
	  printf("#%2d  %s", (ucp[0] << 8) | ucp[1], 
		 self_test_code[(ucp[4] >> 5) & 0x7]);

	  // self-test result
	  res = ucp[4] & 0xf;
	  printf("  %s", self_test_result[res]);

	  // self-test number identifies test that failed and consists
	  // of either the number of the segment that failed during
	  // the test, or the number of the test that failed and the
	  // number of the segment in which the test was run, using a
	  // vendor-specific method of putting both numbers into a
	  // single byte.
	  if (ucp[5])
	    printf(" %3d",  (int)ucp[5]);
	  else
	    printf(" -  ");

	  // print time that the self-test was completed
	  if (n==0 && res==0xf)
	    // self-test in progress
	    printf(" NOW  ");
	  else   
	    printf(" %5d",  n);
	  
	  // construct 8-byte integer address of first failure
	  for (i=0; i<8; i++){
	    ull <<= 8;
	    ull |= ucp[i+8];
	  }

	  // print Address of First Failure, if sensible
	  if ((0xffffffffffffffffULL != ull) && (res > 0) && ( res < 0xf))
	    printf(" 0x%10llx", ull);
	  else
	    printf(" -           ");

	  // if sense key nonzero, then print it, along with
	  // additional sense code and additional sense code qualifier
	  if (ucp[16] & 0xf)
	    printf(" [0x%x 0x%x 0x%x]\n", ucp[16] & 0xf, ucp[17], ucp[18]);
	  else
	    printf(" [-   -    -]\n");
	}

	// if header never printed, then there was no output
	if (noheader)
	  printf("No self-tests have been logged\n\n");
	else
	  printf("\n");

	return;
}
 
void scsiGetDriveInfo ( int device)
{
   char manufacturer[9];
   char product[17];
   char revision[5];

   UINT8 smartsupport;
	
   if (stdinquiry ( device, (UINT8 *) &gBuf) != 0)
   {
      perror ( "Standard Inquiry failed");
   }

   memset ( &manufacturer, 0, 8);
   manufacturer[8] = '\0';
   strncpy ((char *) &manufacturer, (char *) &gBuf[8], 8);
 
   memset ( &product, 0, 16);
   strncpy ((char *) &product, (char *) &gBuf[16], 16);
   product[16] = '\0';
	
   memset ( &revision, 0, 4);
   strncpy ((char *) &revision, (char *) &gBuf[32], 4);
   revision[4] = '\0';
   printf("Device: %s %s Version: %s\n", manufacturer, product, revision);
	
   if ( scsiSmartSupport( device, (UINT8 *) &smartsupport) != 0)
   {
      printf("Device does not support %s\n",(gBuf[0] & 0x1f)?
                         "TapeAlerts": "S.M.A.R.T.");
      exit (1);
   }
	
   printf("Device supports %s and is %s\n%s\n", 
            (gBuf[0] & 0x1f)? "TapeAlerts" : "S.M.A.R.T.",
            (smartsupport & DEXCPT_ENABLE)? "Disable" : "Enabled",
            (smartsupport & EWASC_ENABLE)? "Temperature Warning Enabled":
		"Temperature Warning Disabled or Not Supported");

}


void scsiSmartEnable( int device)
{
	
	/* Enable Exception Control */

	if ( scsiSmartDEXCPTDisable(device) != 0)
	{
		exit (1);
	}
	printf("S.M.A.R.T. enabled\n");
		

	if (scsiSmartEWASCEnable(device) != 0)
	{
		printf("Temperature Warning not Supported\n");
		
	}
	else
	{
		printf("Temperature Warning Enabled\n");
		
	}

	return;

}
	

void scsiSmartDisable (int device)
{

	if ( scsiSmartDEXCPTEnable(device) != 0)
	{
		exit (1);
	}
	printf("S.M.A.R.T. Disabled\n");
		

}

void scsiPrintTemp (int device)
{
  UINT8 temp;
  UINT8 trip;

  if ( scsiGetTemp(device, &temp, &trip) != 0)
  {
   exit (1);
  }
  
  printf("Current Drive Temperature:     %d C\n", temp);
  printf("Drive Trip Temperature:        %d C\n", trip);


}

void scsiPrintStopStart ( int device )
{
/**
  unsigned int css;

  if ( scsiGetStartStop(device, unsigned int *css) != 0)
  {
   exit (1);
  }
  
  printf ("Start Stop Count: %d\n", css);
**/
}

void scsiPrintMain (char *device, int fd)
{
  int checkedsupportlogpages = 0;

  // See if unit accepts SCSI commmands from us
  if (testunitnotready(fd)){
    printf("Smartctl: device %s failed Test Unit Ready\n", device);
    exit(1);
  }

    if (con->driveinfo)
	scsiGetDriveInfo(fd); 

    if (con->smartenable) 
	scsiSmartEnable(fd);

    if (con->smartdisable)
	scsiSmartDisable(fd);

    if (con->checksmart)
    {
	scsiGetSupportPages (fd);
	checkedsupportlogpages = 1;
        if(gTapeAlertsPage)
          scsiGetTapeAlertsData (fd);
       else
        {
           scsiGetSmartData(fd);
           if(gTempPage)
            scsiPrintTemp(fd);         
           if(gStartStopPage)
            scsiGetStartStopData (fd);
        }
    }	

    if (con->smartselftestlog) {
	if (! checkedsupportlogpages)
	    scsiGetSupportPages(fd);
	if (gSelfTestPage)
            scsiPrintSelfTest(fd);
    }
	
    if ( con->smartexeoffimmediate )
    {
	if ( scsiSmartOfflineTest (fd) != 0) 
	{
          printf( "Smartctl: Smart Offline Failed\n");
          exit(-1);
	}
	printf ("Drive Command Successful offline test has begun\n");

        printf ("Use smartctl -X to abort test\n");	
			
    }


    if ( con->smartshortcapselftest )
    {
	if ( scsiSmartShortCapSelfTest (fd) != 0) 
	{
            printf( "Smartctl: Smart Short Self Test Failed\n");
            exit(-1);
	}
	printf ("Drive Command Successful Short Self test has begun\n");
        printf ("Use smartctl -X to abort test\n");	
   }

   if ( con->smartshortselftest )
   { 
		
      if ( scsiSmartShortSelfTest (fd) != 0) 
      {
	printf( "Smartctl: Smart Short Self Test Failed\n");
	exit(-1);
      }
	printf ("Drive Command Successful Short Self test has begun\n");
        printf ("Use smartctl -X to abort test\n");
   }
	
   if ( con->smartextendselftest )
   {
      if ( scsiSmartExtendSelfTest (fd) != 0) 
      {
	printf( "S.M.A.R.T. Extended Self Test Failed\n");
	exit(-1);
      }

   printf ("Drive Command Successful Extended Self test has begun\n");
   printf ("Use smartctl -X to abort test\n");	
   }
	
	if ( con->smartextendcapselftest )
	{
		
		if ( scsiSmartExtendCapSelfTest (fd) != 0) 
		{
			printf( "S.M.A.R.T. Extended Self Test Failed\n");
			exit(-1);
		}
		
		printf ("Drive Command Successful Extended Self test has begun\n");
        printf ("Use smartctl -X to abort test\n");	
	}

	if ( con->smartselftestabort )
	{
		
		if ( scsiSmartSelfTestAbort (fd) != 0) 
		{
			printf( "S.M.A.R.T. Self Test Abort Failed\n");
			exit(-1);
		}
		
		printf ("Drive Command Successful self test aborted\n");
     }		
}
