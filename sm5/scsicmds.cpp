/*
 * scsicmds.c
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2002-3 Bruce Allen <smartmontools-support@lists.sourceforge.net>
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
 * Changelog:
 *      - scsi code clean up. Doug Gilbert<dougg@torque.net> 2003/3/24
 */

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "scsicmds.h"
#include "utility.h"
#include "extern.h"

const char *scsicmds_c_cvsid="$Id: scsicmds.cpp,v 1.25 2003/03/30 11:22:56 dpgilbert Exp $" SCSICMDS_H_CVSID EXTERN_H_CVSID;

// for passing global control variables
extern smartmonctrl *con;

#if 0
/* useful for watching commands and responses */
static void dStrHex(const char* str, int len, int no_ascii)
{
    const char* p = str;
    unsigned char c;
    char buff[82];
    int a = 0;
    const int bpstart = 5;
    const int cpstart = 60;
    int cpos = cpstart;
    int bpos = bpstart;
    int i, k;
    
    if (len <= 0) return;
    memset(buff,' ',80);
    buff[80]='\0';
    k = sprintf(buff + 1, "%.2x", a);
    buff[k + 1] = ' ';
    if (bpos >= ((bpstart + (9 * 3))))
        bpos++;

    for(i = 0; i < len; i++)
    {
        c = *p++;
        bpos += 3;
        if (bpos == (bpstart + (9 * 3)))
            bpos++;
        sprintf(&buff[bpos], "%.2x", (int)(unsigned char)c);
        buff[bpos + 2] = ' ';
        if (no_ascii)
            buff[cpos++] = ' ';
        else {
            if ((c < ' ') || (c >= 0x7f))
                c='.';
            buff[cpos++] = c;
        }
        if (cpos > (cpstart+15))
        {
            pout("%s\n", buff);
            bpos = bpstart;
            cpos = cpstart;
            a += 16;
            memset(buff,' ',80);
            k = sprintf(buff + 1, "%.2x", a);
            buff[k + 1] = ' ';
        }
    }
    if (cpos > cpstart)
    {
        pout("%s\n", buff);
    }
}
#endif

#if 1   
/* Linux specific code, FreeBSD could conditionally compile in CAM stuff 
 * instead of this. */

/* #include <scsi/scsi.h>       bypass for now */
/* #include <scsi/scsi_ioctl.h> bypass for now */

#define MAX_DXFER_LEN 1024      /* can be increased if necessary */
#define SEND_IOCTL_RESP_SENSE_LEN 16    /* ioctl limitation */
#define DRIVER_SENSE  0x8       /* alternate CHECK CONDITION indication */

#ifndef SCSI_IOCTL_SEND_COMMAND
#define SCSI_IOCTL_SEND_COMMAND 1
#endif
#ifndef SCSI_IOCTL_TEST_UNIT_READY
#define SCSI_IOCTL_TEST_UNIT_READY 2
#endif

struct ioctl_send_command_hack
{
    int inbufsize;
    int outbufsize;
    UINT8 buff[MAX_DXFER_LEN + 16];
};

static int do_scsi_cmnd_io(int dev_fd, struct scsi_cmnd_io * iop)
{
    struct ioctl_send_command_hack wrk;
    int status, buff_offset;
    size_t len;

    memcpy(wrk.buff, iop->cmnd, iop->cmnd_len);
    buff_offset = iop->cmnd_len;
    if (con->reportscsiioctl) {
        int k;
        const unsigned char * ucp = iop->cmnd;

        pout("cmnd: [");
        for (k = 0; k < iop->cmnd_len; ++k)
            pout("%02x ", ucp[k]);
    }
    switch (iop->dxfer_dir) {
        case DXFER_NONE:
            wrk.inbufsize = 0;
            wrk.outbufsize = 0;
            break;
        case DXFER_FROM_DEVICE:
            wrk.inbufsize = 0;
            if (iop->dxfer_len > MAX_DXFER_LEN)
                return -EINVAL;
            wrk.outbufsize = iop->dxfer_len;
            break;
        case DXFER_TO_DEVICE:
            if (iop->dxfer_len > MAX_DXFER_LEN)
                return -EINVAL;
            memcpy(wrk.buff + buff_offset, iop->dxferp, iop->dxfer_len);
            wrk.inbufsize = iop->dxfer_len;
            wrk.outbufsize = 0;
            break;
        default:
            pout("do_scsi_cmnd_io: bad dxfer_dir\n");
            return -EINVAL;
    }
    iop->resp_sense_len = 0;
    iop->scsi_status = 0;
    iop->resid = 0;
    /* The SCSI_IOCTL_SEND_COMMAND ioctl is primitive and it doesn't 
     * support: CDB length (guesses it from opcode), resid and timeout */
    status = ioctl(dev_fd, SCSI_IOCTL_SEND_COMMAND , &wrk);
    if (con->reportscsiioctl) {
      if (-1 == status)
          pout("] status=-1, errno=%d\n", errno);
      else
          pout("] status=0x%x\n", status);
    }
    if (-1 == status)
        return -errno;
    if (0 == status) {
        if (DXFER_FROM_DEVICE == iop->dxfer_dir)
            memcpy(iop->dxferp, wrk.buff, iop->dxfer_len);
        return 0;
    }
    iop->scsi_status = status & 0xff;
    if (DRIVER_SENSE == ((status >> 24) & 0xff))
        iop->scsi_status = 2;
    len = (SEND_IOCTL_RESP_SENSE_LEN < iop->max_sense_len) ?
                SEND_IOCTL_RESP_SENSE_LEN : iop->max_sense_len;
    if ((iop->scsi_status & 0x2) && iop->sensep && (len > 0)) {
        memcpy(iop->sensep, wrk.buff, len);
        iop->resp_sense_len = len;
    }
    if (iop->scsi_status > 0)
        return 0;
    else
        return -ENODEV;      /* give up, assume no device there */
}
#endif

void scsi_do_sense_disect(const struct scsi_cmnd_io * io_buf,
                          struct scsi_sense_disect * out)
{
    memset(out, 0, sizeof(out));
    if ((io_buf->scsi_status & 0x2) && (io_buf->resp_sense_len > 7)) {  
        /* CHECK CONDITION and CMD TERMINATED */
        out->error_code = (io_buf->sensep[0] & 0x7f);
        out->sense_key = (io_buf->sensep[2] & 0xf);
        if (io_buf->resp_sense_len > 13) {
            out->asc = io_buf->sensep[12];
            out->ascq = io_buf->sensep[13];
        }
    }
}

int logsense(int device, int pagenum, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[10];
    int status;

    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = LOG_SENSE;
    cdb[2] = 0x40 | pagenum;  /* Page control (PC)==1 [current cumulative] */
    cdb[7] = (bufLen >> 8) & 0xff;
    cdb[8] = bufLen & 0xff;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);

    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

int modesense(int device, int pagenum, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status;

    if ((bufLen < 0) || (bufLen > 255))
        return -EINVAL;
    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = MODE_SENSE;
    cdb[2] = pagenum;
    cdb[4] = bufLen;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);

    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}
/* Sends a 6 byte MODE SELECT command. Assumes given pBuf is the response
 * from a corresponding 6 byte MODE SENSE command. Such a response should
 * have a 4 byte header folowed by 0 or more 8 byte block descriptors
 * (normally 1) and then 1 mode page.  */
int modeselect(int device, int pagenum, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status, pg_offset, pg_len, hdr_plus_1_pg;
    int sense_len = pBuf[0] + 1;

    if (sense_len < 4)
        return -EINVAL;
    pg_offset = 4 + pBuf[3];
    if (pg_offset + 2 >= bufLen)
        return -EINVAL;
    pg_len = pBuf[pg_offset + 1] + 2;
    hdr_plus_1_pg = pg_offset + pg_len;
    if ((hdr_plus_1_pg > bufLen) || (hdr_plus_1_pg > sense_len))
        return -EINVAL;
    pBuf[0] = 0;    /* Length of returned mode sense data reserved for SELECT */
    pBuf[pg_offset] &= 0x3f;    /* Mask of PS bit from byte 0 of page data */
    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_TO_DEVICE;
    io_hdr.dxfer_len = hdr_plus_1_pg;
    io_hdr.dxferp = pBuf;
    cdb[0] = MODE_SELECT;
    cdb[2] = 0x11;      /* set PF and SP bits */
    cdb[4] = hdr_plus_1_pg; /* make sure only one page sent */
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);

    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

/* Not currently used */
int modesense10(int device, int pagenum, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[10];
    int status;

    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = MODE_SENSE;
    cdb[2] = pagenum;
    cdb[7] = (bufLen >> 8) & 0xff;
    cdb[8] = bufLen & 0xff;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);

    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

/* Not currently used */
int modeselect10(int device, int pagenum, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[10];
    int status, pg_offset, pg_len, hdr_plus_1_pg;
    int sense_len = (pBuf[0] << 8) + pBuf[1] + 2;

    if (sense_len < 4)
        return -EINVAL;
    pg_offset = 8 + (pBuf[6] << 8) + pBuf[7];
    if (pg_offset + 2 >= bufLen)
        return -EINVAL;
    pg_len = pBuf[pg_offset + 1] + 2;
    hdr_plus_1_pg = pg_offset + pg_len;
    if ((hdr_plus_1_pg > bufLen) || (hdr_plus_1_pg > sense_len))
        return -EINVAL;
    pBuf[0] = 0;    
    pBuf[1] = 0; /* Length of returned mode sense data reserved for SELECT */
    pBuf[pg_offset] &= 0x3f;    /* Mask of PS bit from byte 0 of page data */
    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_TO_DEVICE;
    io_hdr.dxfer_len = hdr_plus_1_pg;
    io_hdr.dxferp = pBuf;
    cdb[0] = MODE_SELECT_10;
    cdb[2] = 0x11;      /* set PF and SP bits */
    cdb[8] = hdr_plus_1_pg; /* make sure only one page sent */
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);
    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

/* bufLen should be 36 for unsafe devices (like USB mass storage stuff)
 * otherwise they can lock up!
 */
int stdinquiry(int device, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status;

    if ((bufLen < 0) || (bufLen > 255))
        return -EINVAL;
    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = INQUIRY;
    cdb[4] = bufLen;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);
    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

/* Still unused, INQUIRY to fetch Vital Page Data. */
int inquiry(int device, int pagenum, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status;

    if ((bufLen < 0) || (bufLen > 255))
        return -EINVAL;
    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = INQUIRY;
    cdb[1] = 0x1;       /* set EVPD bit (enable Vital Product Data) */
    cdb[2] = pagenum;
    cdb[4] = bufLen;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);
    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

int requestsense(int device, struct scsi_sense_disect * sense_info)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    UINT8 buff[18];
    int status, len;
    UINT8 ecode;

    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = sizeof(buff);
    io_hdr.dxferp = buff;
    cdb[0] = REQUEST_SENSE;
    cdb[4] = sizeof(buff);
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);
    status = do_scsi_cmnd_io(device, &io_hdr);
    if ((0 == status) && (sense_info)) {
        ecode = buff[0] & 0x7f;
        sense_info->error_code = ecode;
        sense_info->sense_key = buff[2] & 0xf;
        if ((0x70 != ecode) && (0x71 != ecode)) {
            sense_info->asc = 0;
            sense_info->ascq = 0;
            return status;
        }
        len = buff[4] + 8;
        if (len > 13) {
            sense_info->asc = buff[12];
            sense_info->ascq = buff[13];
        }
    }
    return status;
}

int senddiagnostic(int device, int functioncode, UINT8 *pBuf, int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status;

    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = bufLen ? DXFER_TO_DEVICE: DXFER_NONE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = SEND_DIAGNOSTIC;
    if (SCSI_DIAG_DEF_SELF_TEST == functioncode)
        cdb[1] = 0x14;  /* PF and SelfTest bit */
    else if (SCSI_DIAG_NO_SELF_TEST != functioncode)
        cdb[1] = (functioncode << 5 ) | 0x10; /* SelfTest _code_ + PF bit */
    else   /* SCSI_DIAG_NO_SELF_TEST == functioncode */
        cdb[1] = 0x10;  /* PF bit */
    cdb[3] = (bufLen >> 8) & 0xff;
    cdb[4] = bufLen & 0xff;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);
    io_hdr.timeout = 5 * 60 * 60;   /* five hours because a foreground 
                    extended self tests can take 1 hour plus */
    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

/* Not currently used */
int receivediagnostic(int device, int pcv, int pagenum, UINT8 *pBuf, 
                      int bufLen)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status;

    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_FROM_DEVICE;
    io_hdr.dxfer_len = bufLen;
    io_hdr.dxferp = pBuf;
    cdb[0] = RECEIVE_DIAGNOSTIC;
    cdb[1] = pcv;
    cdb[2] = pagenum;
    cdb[3] = (bufLen >> 8) & 0xff;
    cdb[4] = bufLen & 0xff;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);
    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

// See if the device accepts IOCTLs at all...
int testunitready(int device)
{
    struct scsi_cmnd_io io_hdr;
    UINT8 cdb[6];
    int status;

    memset(&io_hdr, 0, sizeof(io_hdr));
    memset(cdb, 0, sizeof(cdb));
    io_hdr.dxfer_dir = DXFER_NONE;
    io_hdr.dxfer_len = 0;
    io_hdr.dxferp = NULL;
    cdb[0] = TEST_UNIT_READY;
    io_hdr.cmnd = cdb;
    io_hdr.cmnd_len = sizeof(cdb);

    status = do_scsi_cmnd_io(device, &io_hdr);
    return status;
}

/* ModePage1C Handler */
#define SMART_SUPPORT   0x00    

int scsiSmartModePage1CHandler(int device, UINT8 setting, UINT8 *retval)
{
    char tBuf[254];
        
    if (modesense(device, 0x1c, tBuf, sizeof(tBuf)))
        return 1;
        
    switch (setting) {
        case DEXCPT_DISABLE:
            tBuf[14] &= 0xf7;
            tBuf[15] = 0x04;
            break;
        case DEXCPT_ENABLE:
            tBuf[14] |= 0x08;
            break;
        case EWASC_ENABLE:
            tBuf[14] |= 0x10;
            break;
        case EWASC_DISABLE:
            tBuf[14] &= 0xef;
            break;
        case SMART_SUPPORT:
            *retval = tBuf[14] & 0x08;
            return 0;
        default:
            return 1;
    }
                        
    if (modeselect(device, 0x1c, tBuf, sizeof(tBuf)))
        return 1;
        
    return 0;
}

int scsiSmartSupport(int device, UINT8 *retval)
{
    return scsiSmartModePage1CHandler( device, SMART_SUPPORT, retval);
}

int scsiSmartEWASCEnable(int device)
{
    return scsiSmartModePage1CHandler(device, EWASC_ENABLE, NULL);
}

int scsiSmartEWASCDisable(int device)
{
    return scsiSmartModePage1CHandler(device, EWASC_DISABLE, NULL);
}

int scsiSmartDEXCPTEnable(int device)
{
    return scsiSmartModePage1CHandler(device, DEXCPT_ENABLE, NULL);
}

int scsiSmartDEXCPTDisable(int device)
{
    return scsiSmartModePage1CHandler(device, DEXCPT_DISABLE, NULL);
}

int scsiGetTemp(int device, UINT8 *currenttemp, UINT8 *triptemp)
{
    UINT8 tBuf[1024];
    int err;

    if ((err = logsense(device, TEMPERATURE_PAGE, tBuf, sizeof(tBuf)))) {
        *currenttemp = 0;
        *triptemp = 0;
        pout("Log Sense failed, err=%d\n", err);
        return 1;
    }
    *currenttemp = tBuf[9];
    *triptemp = tBuf[15];
    return 0;
}

int scsiCheckSmart(int device, UINT8 method, UINT8 *retval,
                   UINT8 *currenttemp, UINT8 *triptemp)
{
    UINT8 tBuf[1024];
    struct scsi_sense_disect sense_info;
    int err;
    unsigned short pagesize;
 
    *currenttemp = *triptemp = 0;
  
    memset(&sense_info, 0, sizeof(sense_info));
    if (method == CHECK_SMART_BY_LGPG_2F) {
        if ((err = logsense(device, SMART_PAGE, tBuf, sizeof(tBuf)))) {
            *currenttemp = 0;
            *triptemp = 0;
            *retval = 0;
            pout("Log Sense failed, err=%d\n", err);
            return 1;
        }
        pagesize = (unsigned short) (tBuf[2] << 8) | tBuf[3];
        if (! pagesize)
            return 1; /* failed read of page 2F\n */
        sense_info.asc = tBuf[8]; 
        sense_info.ascq = tBuf[9];
        if ((pagesize == 8) && currenttemp && triptemp) {
            *currenttemp = tBuf[10];
            *triptemp =  tBuf[11];
        } 
    } else {
        if ((err = requestsense(device, &sense_info))) {
            *currenttemp = 0;
            *triptemp = 0;
            *retval = 0;
            pout("Request Sense failed, err=%d\n", err);
            return 1;
        }
    }
    if (sense_info.asc == 0x5d)
        *retval = sense_info.ascq;
    else
        *retval = 0;
    return 0;
}

static const char * TapeAlertsMessageTable[]= {  
    " ",
   "The tape drive is having problems reading data. No data has been lost, "
       "but there has been a reduction in the performance of the tape.",
   "The tape drive is having problems writing data. No data has been lost, "
       "but there has been a reduction in the performance of the tape.",
   "The operation has stopped because an error has occurred while reading "
       "or writing data which the drive cannot correct.",
   "Your data is at risk:\n1. Copy any data you require from this tape. \n"
       "2. Do not use this tape again.\n"
       "3. Restart the operation with a different tape.",
   "The tape is damaged or the drive is faulty. Call the tape drive "
       "supplier helpline.",
   "The tape is from a faulty batch or the tape drive is faulty:\n"
       "1. Use a good tape to test the drive.\n"
       "2. If problem persists, call the tape drive supplier helpline.",
   "The tape cartridge has reached the end of its calculated useful life: \n"
       "1. Copy data you need to another tape.\n"
       "2. Discard the old tape.",
   "The tape cartridge is not data-grade. Any data you back up to the tape "
       "is at risk. Replace the cartridge with a data-grade tape.",
   "You are trying to write to a write-protected cartridge. Remove the "
       "write-protection or use another tape.",
   "You cannot eject the cartridge because the tape drive is in use. Wait "
       "until the operation is complete before ejecting the cartridge.",
   "The tape in the drive is a cleaning cartridge.",
   "You have tried to load a cartridge of a type which is not supported "
       "by this drive.",
   "The operation has failed because the tape in the drive has snapped:\n"
       "1. Discard the old tape.\n"
       "2. Restart the operation with a different tape.",
   "The operation has failed because the tape in the drive has snapped:\n"
       "1. Do not attempt to extract the tape cartridge\n"
       "2. Call the tape drive supplier helpline.",
   "The memory in the tape cartridge has failed, which reduces performance. "
       "Do not use the cartridge for further backup operations.",
   "The operation has failed because the tape cartridge was manually "
       "ejected while the tape drive was actively writing or reading.",
   "You have loaded of a type that is read-only in this drive. The "
       "cartridge will appear as write-protected.",
   "The directory on the tape cartridge has been corrupted. File search "
       "performance will be degraded. The tape directory can be rebuilt "
       "by reading all the data on the cartridge.",
   "The tape cartridge is nearing the end of its calculated life. It is "
       "recommended that you:\n"
       "1. Use another tape cartridge for your next backup.\n"
       "2. Store this tape in a safe place in case you need to restore "
       "data from it.",
   "The tape drive needs cleaning:\n"
       "1. If the operation has stopped, eject the tape and clean the drive.\n"
       "2. If the operation has not stopped, wait for it to finish and then "
       "clean the drive. Check the tape drive users manual for device "
       "specific cleaning instructions.",
   "The tape drive is due for routine cleaning:\n"
       "1. Wait for the current operation to finish.\n"
       "2. The use a cleaning cartridge. Check the tape drive users manual "
       "for device specific cleaning instructions.",
   "The last cleaning cartridge used in the tape drive has worn out:\n"
       "1. Discard the worn out cleaning cartridge.\n"
       "2. Wait for the current operation to finish.\n"
       "3. Then use a new cleaning cartridge.",
   "The last cleaning cartridge used in the tape drive was an invalid type:\n"
       "1. Do not use this cleaning cartridge in this drive.\n"
       "2. Wait for the current operation to finish.\n"
       "3. Then use a new cleaning cartridge.",
   "The tape drive has requested a retention operation",
   "A redundant interface port on the tape drive has failed",
   "A tape drive cooling fan has failed",
   "A redundant power supply has failed inside the tape drive enclosure. "
       "Check the enclosure users manual for instructions on replacing the "
       "failed power supply.",
   "The tape drive power consumption is outside the specified range.",
   "Preventive maintenance of the tape drive is required. Check the tape "
       "drive users manual for device specific preventive maintenance "
       "tasks or call the tape drive supplier helpline.",
   "The tape drive has a hardware fault:\n"
       "1. Eject the tape or magazine.\n"
       "2. Reset the drive.\n"
       "3. Restart the operation.",
   "The tape drive has a hardware fault:\n"
       "1. Turn the tape drive off and then on again.\n"
       "2. Restart the operation.\n"
       "3. If the problem persists, call the tape drive supplier helpline.\n"
       " Check the tape drive users manual for device specific instructions "
       "on turning the device power in and off.",
   "The tape drive has a problem with the host interface:\n"
       "1. Check the cables and cable connections.\n"
       "2. Restart the operation.",
   "The operation has failed:\n"
       "1. Eject the tape or magazine.\n"
       "2. Insert the tape or magazine again.\n"
       "3. Restart the operation.",
   "The firmware download has failed because you have tried to use the "
       "incorrect firmware for this tape drive. Obtain the correct "
       "firmware and try again.",
   "Environmental conditions inside the tape drive are outside the "
       "specified humidity range.",
   "Environmental conditions inside the tape drive are outside the "
       "specified temperature range.",
   "The voltage supply to the tape drive is outside the specified range.",
   "A hardware failure of the tape drive is predicted. Call the tape "
       "drive supplier helpline.",
   "The tape drive may have a fault. Check for availability of diagnostic "
       "information and run extended diagnostics if applicable. Check the "
       "tape drive users manual for instruction on running extended "
       "diagnostic tests and retrieving diagnostic data",
   "The changer mechanism is having difficulty communicating with the tape "
       "drive:\n"
       "1. Turn the autoloader off then on.\n"
       "2. Restart the operation.\n"
       "3. If problem persists, call the tape drive supplier helpline.",
   "A tape has been left in the autoloader by a previous hardware fault:\n"
       "1. Insert an empty magazine to clear the fault.\n"
       "2. If the fault does not clear, turn the autoloader off and then "
       "on again.\n"
       "3. If the problem persists, call the tape drive supplier helpline.",
   "There is a problem with the autoloader mechanism.",
   "The operation has failed because the autoloader door is open:\n"
       "1. Clear any obstructions from the autoloader door.\n"
       "2. Eject the magazine and then insert it again.\n"
       "3. If the fault does not clear, turn the autoloader off and then "
       "on again.\n"
       "4. If the problem persists, call the tape drive supplier helpline.",
   "The autoloader has a hardware fault:\n"
       "1. Turn the autoloader off and then on again.\n"
       "2. Restart the operation.\n"
       "3. If the problem persists, call the tape drive supplier helpline.\n"
       " Check the autoloader users manual for device specific instructions "
       "on turning the device power on and off.",
   "The autoloader cannot operate without the magazine,\n"
       "1. Insert the magazine into the autoloader.\n"
       "2. Restart the operation.",
   "A hardware failure of the changer mechanism is predicted. Call the "
       "tape drive supplier helpline.",
   " ",
   " ",
   " ",
   "Media statistics have been lost at some time in the past",
   "The tape directory on the tape cartridge just unloaded has been "
       "corrupted. File search performance will be degraded. The tape "
       "directory can be rebuilt by reading all the data.",
   "The tape just unloaded could not write its system area successfully:\n"
       "1. Copy data to another tape cartridge.\n"
       "2. Discard the old cartridge.",
   "The tape system are could not be read successfully at load time:\n"
       "1. Copy data to another tape cartridge.\n"
       "2. Discard the old cartridge.",
   "The start or data could not be found on the tape:\n"
       "1. Check you are using the correct format tape.\n"
       "2. Discard the tape or return the tape to you supplier",
    };

const char * scsiTapeAlertsTapeDevice(unsigned short code)
{
#define NUMENTRIESINTAPEALERTSTABLE 54
    return (code > NUMENTRIESINTAPEALERTSTABLE) ? "Unknown Alert" : 
                                        TapeAlertsMessageTable[code];
}

/* this is a subset of the SCSI additional sense code strings indexed
 * by "ascq" for the case when asc==0x5d
 */
static const char * strs_for_asc_5d[] = {
   /* 0x00 */   "FAILURE PREDICTION THRESHOLD EXCEEDED",
        "MEDIA FAILURE PREDICTION THRESHOLD EXCEEDED",
        "LOGICAL UNIT FAILURE PREDICTION THRESHOLD EXCEEDED",
        "SPARE AREA EXHAUSTION PREDICTION THRESHOLD EXCEEDED",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
   /* 0x10 */   "HARDWARE IMPENDING FAILURE GENERAL HARD DRIVE FAILURE",
        "HARDWARE IMPENDING FAILURE DRIVE ERROR RATE TOO HIGH",
        "HARDWARE IMPENDING FAILURE DATA ERROR RATE TOO HIGH",
        "HARDWARE IMPENDING FAILURE SEEK ERROR RATE TOO HIGH",
        "HARDWARE IMPENDING FAILURE TOO MANY BLOCK REASSIGNS",
        "HARDWARE IMPENDING FAILURE ACCESS TIMES TOO HIGH",
        "HARDWARE IMPENDING FAILURE START UNIT TIMES TOO HIGH",
        "HARDWARE IMPENDING FAILURE CHANNEL PARAMETRICS",
        "HARDWARE IMPENDING FAILURE CONTROLLER DETECTED",
        "HARDWARE IMPENDING FAILURE THROUGHPUT PERFORMANCE",
        "HARDWARE IMPENDING FAILURE SEEK TIME PERFORMANCE",
        "HARDWARE IMPENDING FAILURE SPIN-UP RETRY COUNT",
        "HARDWARE IMPENDING FAILURE DRIVE CALIBRATION RETRY COUNT",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
   /* 0x20 */   "CONTROLLER IMPENDING FAILURE GENERAL HARD DRIVE FAILURE",
        "CONTROLLER IMPENDING FAILURE DRIVE ERROR RATE TOO HIGH",
        "CONTROLLER IMPENDING FAILURE DATA ERROR RATE TOO HIGH",
        "CONTROLLER IMPENDING FAILURE SEEK ERROR RATE TOO HIGH",
        "CONTROLLER IMPENDING FAILURE TOO MANY BLOCK REASSIGNS",
        "CONTROLLER IMPENDING FAILURE ACCESS TIMES TOO HIGH",
        "CONTROLLER IMPENDING FAILURE START UNIT TIMES TOO HIGH",
        "CONTROLLER IMPENDING FAILURE CHANNEL PARAMETRICS",
        "CONTROLLER IMPENDING FAILURE CONTROLLER DETECTED",
        "CONTROLLER IMPENDING FAILURE THROUGHPUT PERFORMANCE",
        "CONTROLLER IMPENDING FAILURE SEEK TIME PERFORMANCE",
        "CONTROLLER IMPENDING FAILURE SPIN-UP RETRY COUNT",
        "CONTROLLER IMPENDING FAILURE DRIVE CALIBRATION RETRY COUNT",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
   /* 0x30 */   "DATA CHANNEL IMPENDING FAILURE GENERAL HARD DRIVE FAILURE",
        "DATA CHANNEL IMPENDING FAILURE DRIVE ERROR RATE TOO HIGH",
        "DATA CHANNEL IMPENDING FAILURE DATA ERROR RATE TOO HIGH",
        "DATA CHANNEL IMPENDING FAILURE SEEK ERROR RATE TOO HIGH",
        "DATA CHANNEL IMPENDING FAILURE TOO MANY BLOCK REASSIGNS",
        "DATA CHANNEL IMPENDING FAILURE ACCESS TIMES TOO HIGH",
        "DATA CHANNEL IMPENDING FAILURE START UNIT TIMES TOO HIGH",
        "DATA CHANNEL IMPENDING FAILURE CHANNEL PARAMETRICS",
        "DATA CHANNEL IMPENDING FAILURE CONTROLLER DETECTED",
        "DATA CHANNEL IMPENDING FAILURE THROUGHPUT PERFORMANCE",
        "DATA CHANNEL IMPENDING FAILURE SEEK TIME PERFORMANCE",
        "DATA CHANNEL IMPENDING FAILURE SPIN-UP RETRY COUNT",
        "DATA CHANNEL IMPENDING FAILURE DRIVE CALIBRATION RETRY COUNT",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
   /* 0x40 */   "SERVO IMPENDING FAILURE GENERAL HARD DRIVE FAILURE",
        "SERVO IMPENDING FAILURE DRIVE ERROR RATE TOO HIGH",
        "SERVO IMPENDING FAILURE DATA ERROR RATE TOO HIGH",
        "SERVO IMPENDING FAILURE SEEK ERROR RATE TOO HIGH",
        "SERVO IMPENDING FAILURE TOO MANY BLOCK REASSIGNS",
        "SERVO IMPENDING FAILURE ACCESS TIMES TOO HIGH",
        "SERVO IMPENDING FAILURE START UNIT TIMES TOO HIGH",
        "SERVO IMPENDING FAILURE CHANNEL PARAMETRICS",
        "SERVO IMPENDING FAILURE CONTROLLER DETECTED",
        "SERVO IMPENDING FAILURE THROUGHPUT PERFORMANCE",
        "SERVO IMPENDING FAILURE SEEK TIME PERFORMANCE",
        "SERVO IMPENDING FAILURE SPIN-UP RETRY COUNT",
        "SERVO IMPENDING FAILURE DRIVE CALIBRATION RETRY COUNT",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
   /* 0x50 */   "SPINDLE IMPENDING FAILURE GENERAL HARD DRIVE FAILURE",
        "SPINDLE IMPENDING FAILURE DRIVE ERROR RATE TOO HIGH",
        "SPINDLE IMPENDING FAILURE DATA ERROR RATE TOO HIGH",
        "SPINDLE IMPENDING FAILURE SEEK ERROR RATE TOO HIGH",
        "SPINDLE IMPENDING FAILURE TOO MANY BLOCK REASSIGNS",
        "SPINDLE IMPENDING FAILURE ACCESS TIMES TOO HIGH",
        "SPINDLE IMPENDING FAILURE START UNIT TIMES TOO HIGH",
        "SPINDLE IMPENDING FAILURE CHANNEL PARAMETRICS",
        "SPINDLE IMPENDING FAILURE CONTROLLER DETECTED",
        "SPINDLE IMPENDING FAILURE THROUGHPUT PERFORMANCE",
        "SPINDLE IMPENDING FAILURE SEEK TIME PERFORMANCE",
        "SPINDLE IMPENDING FAILURE SPIN-UP RETRY COUNT",
        "SPINDLE IMPENDING FAILURE DRIVE CALIBRATION RETRY COUNT",
        "Unknown Failure",
        "Unknown Failure",
        "Unknown Failure",
   /* 0x60 */   "FIRMWARE IMPENDING FAILURE GENERAL HARD DRIVE FAILURE",
        "FIRMWARE IMPENDING FAILURE DRIVE ERROR RATE TOO HIGH",
        "FIRMWARE IMPENDING FAILURE DATA ERROR RATE TOO HIGH",
        "FIRMWARE IMPENDING FAILURE SEEK ERROR RATE TOO HIGH",
        "FIRMWARE IMPENDING FAILURE TOO MANY BLOCK REASSIGNS",
        "FIRMWARE IMPENDING FAILURE ACCESS TIMES TOO HIGH",
        "FIRMWARE IMPENDING FAILURE START UNIT TIMES TOO HIGH",
        "FIRMWARE IMPENDING FAILURE CHANNEL PARAMETRICS",
        "FIRMWARE IMPENDING FAILURE CONTROLLER DETECTED",
        "FIRMWARE IMPENDING FAILURE THROUGHPUT PERFORMANCE",
        "FIRMWARE IMPENDING FAILURE SEEK TIME PERFORMANCE",
        "FIRMWARE IMPENDING FAILURE SPIN-UP RETRY COUNT",
   /* 0x6c */   "FIRMWARE IMPENDING FAILURE DRIVE CALIBRATION RETRY COUNT"};

const char * scsiSmartGetSenseCode(UINT8 ascq)
{
    if (ascq == 0xff)
        return "FAILURE PREDICTION THRESHOLD EXCEEDED (FALSE)";
    else if (ascq <= SMART_SENSE_MAX_ENTRY)
        return strs_for_asc_5d[ascq];
    else
        return "Unknown Failure";
}

/* This is not documented in t10.org, page 0x80 is vendor specific */
/* Some IBM disks do an offline read-scan when they get this command. */
int scsiSmartIBMOfflineTest(int device)
{       
    UINT8 tBuf[256];
        
    memset(tBuf, 0, sizeof(tBuf));
    /* Build SMART Off-line Immediate Diag Header */
    tBuf[0] = 0x80; /* Page Code */
    tBuf[1] = 0x00; /* Reserved */
    tBuf[2] = 0x00; /* Page Length MSB */
    tBuf[3] = 0x04; /* Page Length LSB */
    tBuf[4] = 0x03; /* SMART Revision */
    tBuf[5] = 0x00; /* Reserved */
    tBuf[6] = 0x00; /* Off-line Immediate Time MSB */
    tBuf[7] = 0x00; /* Off-line Immediate Time LSB */
    return senddiagnostic(device, SCSI_DIAG_NO_SELF_TEST, tBuf, 8);
}

int scsiSmartDefaultSelfTest(int device)
{       
    return senddiagnostic(device, SCSI_DIAG_DEF_SELF_TEST, NULL, 0);
}

int scsiSmartShortSelfTest(int device)
{       
    return senddiagnostic(device, SCSI_DIAG_BG_SHORT_SELF_TEST, NULL, 0);
}

int scsiSmartExtendSelfTest(int device)
{       
    return senddiagnostic(device, SCSI_DIAG_BG_EXTENDED_SELF_TEST, NULL, 0);
}

int scsiSmartShortCapSelfTest(int device)
{       
    return senddiagnostic(device, SCSI_DIAG_FG_SHORT_SELF_TEST, NULL, 0);
}

int scsiSmartExtendCapSelfTest(int device)
{
    return senddiagnostic(device, SCSI_DIAG_FG_EXTENDED_SELF_TEST, NULL, 0);
}

int scsiSmartSelfTestAbort(int device)
{
    return senddiagnostic(device, SCSI_DIAG_ABORT_SELF_TEST, NULL, 0);
}
