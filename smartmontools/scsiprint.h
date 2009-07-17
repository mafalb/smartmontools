/*
 * scsiprint.h
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2002-9 Bruce Allen <smartmontools-support@lists.sourceforge.net>
 * Copyright (C) 2000 Michael Cornwell <cornwell@acm.org>
 *
 * Additional SCSI work:
 * Copyright (C) 2003-9 Douglas Gilbert <dougg@torque.net>
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


#ifndef SCSI_PRINT_H_
#define SCSI_PRINT_H_

#define SCSIPRINT_H_CVSID "$Id: scsiprint.h,v 1.24 2009/06/21 02:39:32 dpgilbert Exp $\n"

// Options for scsiPrintMain
// TODO: Move remaining options from con->* to here.
struct scsi_print_options
{
  bool drive_info;
  bool smart_check_status;
  bool smart_vendor_attrib;
  bool smart_error_log;
  bool smart_selftest_log;
  bool smart_background_log;

  bool smart_disable, smart_enable;
  bool smart_auto_save_disable, smart_auto_save_enable;

  bool smart_default_selftest;
  bool smart_short_selftest, smart_short_cap_selftest;
  bool smart_extend_selftest, smart_extend_cap_selftest;
  bool smart_selftest_abort;

  bool sasphy, sasphy_reset;

  scsi_print_options()
    : drive_info(false),
      smart_check_status(false),
      smart_vendor_attrib(false),
      smart_error_log(false),
      smart_selftest_log(false),
      smart_background_log(false),
      smart_disable(false), smart_enable(false),
      smart_auto_save_disable(false), smart_auto_save_enable(false),
      smart_default_selftest(false),
      smart_short_selftest(false), smart_short_cap_selftest(false),
      smart_extend_selftest(false), smart_extend_cap_selftest(false),
      smart_selftest_abort(false),
      sasphy(false), sasphy_reset(false)
    { }
};

int scsiPrintMain(scsi_device * device, const scsi_print_options & options);

#endif
