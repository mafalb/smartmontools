/*
 * extern.h
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2002 Bruce Allen <smartmontools-support@lists.sourceforge.net>
 * Copyright (C) 1999-2000 Michael Cornwell <cornwell@acm.org>
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

#ifndef _EXTERN_H_
#define _EXTERN_H_


#ifndef CVSID3
#define CVSID3 "$Id: extern.h,v 1.10 2002/11/07 11:00:56 ballen4705 Exp $\n"
#endif

// Block used for global control/communications.  If you need more
// global variables, this should be the only place that you need to
// add them.
typedef struct ataprintmain_s {
  unsigned char driveinfo;
  unsigned char checksmart;
  unsigned char smartvendorattrib;
  unsigned char generalsmartvalues;
  unsigned char smartselftestlog;
  unsigned char smarterrorlog;
  unsigned char smartdisable;
  unsigned char smartenable; 
  unsigned char smartstatus;
  unsigned char smartexeoffimmediate;
  unsigned char smartshortselftest;
  unsigned char smartextendselftest;
  unsigned char smartshortcapselftest;
  unsigned char smartextendcapselftest;
  unsigned char smartselftestabort;
  unsigned char smartautoofflineenable;
  unsigned char smartautoofflinedisable;
  unsigned char smartautosaveenable;
  unsigned char smartautosavedisable;
  unsigned char smart009minutes;
  int           testcase;
  unsigned char quietmode;
  unsigned char veryquietmode;
  unsigned char permissive;
  unsigned char conservative;
  unsigned char checksumfail;
} atamainctrl;

#endif
