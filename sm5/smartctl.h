/*
 * smartctl.h
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

#ifndef SMARTCTL_H_
#define SMARTCTL_H_

#define SMARTCTL_H_CVSID "$Id: smartctl.h,v 1.25 2008/03/04 22:09:47 ballen4705 Exp $\n"

/* Boolean Values */
#define TRUE 0x01
#define FALSE 0x00

// Return codes (bitmask)

// command line did not parse, or internal error occured in smartctl
#define FAILCMD   (0x01<<0)

// device open failed
#define FAILDEV   (0x01<<1)

// device is in low power mode and -n option requests to exit
#define FAILPOWER (0x01<<1)

// read device identity (ATA only) failed
#define FAILID    (0x01<<1)

// smart command failed, or ATA identify device structure missing information
#define FAILSMART (0x01<<2)

// SMART STATUS returned FAILURE
#define FAILSTATUS (0x01<<3)

// Attributes found <= threshold with prefail=1
#define FAILATTR (0x01<<4)

// SMART STATUS returned GOOD but age attributes failed or prefail
// attributes have failed in the past
#define FAILAGE (0x01<<5)

// Device had Errors in the error log
#define FAILERR (0x01<<6)

// Device had Errors in the self-test log
#define FAILLOG (0x01<<7)

// Classes of SMART commands.  Here 'mandatory' means "Required by the
// ATA/ATAPI-5 Specification if the device implements the S.M.A.R.T.
// command set."  The 'mandatory' S.M.A.R.T.  commands are: (1)
// Enable/Disable Attribute Autosave, (2) Enable/Disable S.M.A.R.T.,
// and (3) S.M.A.R.T. Return Status.  All others are optional.
#define OPTIONAL_CMD 1
#define MANDATORY_CMD 2

void print_smartctl_examples();

#endif
