/*
 * os_win32/syslog_win32.c
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2004 Christian Franke <smartmontools-support@lists.sourceforge.net>
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
 */

// Win32 Emulation of syslog() for smartd
// Writes to windows event log on NT4/2000/XP
// (Register syslogevt.exe as event message file)
// Writes to file "<ident>.log" on 9x/ME.
// If faility is set to LOG_LOCAL[0-7], log is written to
// file "<ident>.log", "<ident>[1-7].log".


#include <stdio.h>
#include <time.h>
#include <errno.h>

#include "syslog.h"

const char *syslog_win32_c_cvsid = "$Id: syslog_win32.cpp,v 1.2 2004/03/24 21:08:44 chrfranke Exp $"
SYSLOG_H_CVSID;

#ifdef _MSC_VER
// MSVC
#include <process.h> // getpid()
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#else
// MinGW
#include <unistd.h> // getpid()
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h> // RegisterEventSourceA(), ReportEventA(), ...

#define ARGUSED(x) ((void)(x))


// Only this event message id is used
// It should be identical to MSG_SYSLOG in "syslogevt.h"
// (generated by "mc" from "syslogevt.mc")
#define MSG_SYSLOG                       0x00000000L


static char sl_ident[100];
static char sl_logpath[sizeof(sl_ident) + sizeof("0.log")-1];
static HANDLE sl_hevtsrc;


void openlog(const char *ident, int logopt, int facility)
{
	strncpy(sl_ident, ident, sizeof(sl_ident)-1);

	if (sl_logpath[0] || sl_hevtsrc)
		return; // Already open

	if (LOG_LOCAL0 <= facility && facility <= LOG_LOCAL7) {
		// Use logfile
		if (facility == LOG_LOCAL0) // "ident.log"
			strcat(strcpy(sl_logpath, sl_ident), ".log");
		else // "ident[1-7].log"
			snprintf(sl_logpath, sizeof(sl_logpath)-1, "%s%d.log",
				sl_ident, LOG_FAC(facility)-LOG_FAC(LOG_LOCAL0));
	}
	else // Assume LOG_DAEMON, use event log if possible
	if (!(sl_hevtsrc = RegisterEventSourceA(NULL/*localhost*/, sl_ident))) {
		// Cannot open => Use logfile
		long err = GetLastError();
		strcat(strcpy(sl_logpath, sl_ident), ".log");
		if (GetVersion() & 0x80000000)
			fprintf(stderr, "%s: No event log on Win9x/ME, writing to %s\n",
				sl_ident, sl_logpath);
		else
			fprintf(stderr, "%s: Cannot register event source (Error=%ld), writing to %s\n",
				sl_ident, err, sl_logpath);
	}
	//assert(sl_logpath[0] || sl_hevtsrc);

	// logopt==LOG_PID assumed
	ARGUSED(logopt);
}


void closelog()
{
#if 0
	if (sl_hevtsrc) {
		DeregisterEventSource(sl_hevtsrc); sl_hevtsrc = 0;
	}
#else
	// Leave event message source open to prevent losing messages during shutdown
#endif
}


// Map syslog priority to event type

static WORD pri2evtype(int priority)
{
	switch (priority) {
		default:
		case LOG_EMERG: case LOG_ALERT:
		case LOG_CRIT:  case LOG_ERR:
			return EVENTLOG_ERROR_TYPE;
		case LOG_WARNING:
			return EVENTLOG_WARNING_TYPE;
		case LOG_NOTICE: case LOG_INFO:
		case LOG_DEBUG:
			return EVENTLOG_INFORMATION_TYPE;
	}
}


// Map syslog priority to string

static const char * pri2text(int priority)
{
	switch (priority) {
		case LOG_EMERG:   return "EMERG";
		case LOG_ALERT:   return "ALERT";
		case LOG_CRIT:    return "CRIT ";
		default:
		case LOG_ERR:     return "ERROR";
		case LOG_WARNING: return "Warn ";
		case LOG_NOTICE:  return "Note ";
		case LOG_INFO:    return "Info ";
		case LOG_DEBUG:   return "Debug";
	}
}


// Format the log message
// logfmt=0 => Event Log, logfmt=1 => Logfile

static int format_msg(char * buffer, int bufsiz, int logfmt,
                      int priority, const char * message, va_list args)
{
	int pid, n;
	int len = 0;
	if (logfmt) {
		time_t now = time((time_t*)0);
		n = strftime (buffer, bufsiz-1, "%Y-%m-%d %H:%M:%S ", localtime(&now));
		if (n == 0)
			return -1;
		len = strlen(buffer);
	}
	// Assume logopt = LOG_PID 
	pid = getpid();
	if (pid < 0) // ugly Win9x/ME pid
		n = snprintf(buffer+len, bufsiz-1-len-30, "%s[0x%X]: ",  sl_ident, pid);
	else
		n = snprintf(buffer+len, bufsiz-1-len-30, "%s[%d]: ",  sl_ident, pid);
	if (n < 0)
		return -1;
	len += n;
	// Append pri
	if (logfmt) {
		n = snprintf(buffer+len, bufsiz-1-len-20, "%s: ", pri2text(priority));
		if (n < 0)
			return -1;
		len += n;
	}
	// Format message
	n = vsnprintf(buffer+len, bufsiz-1-len-1, message, args);
	if (n < 0)
		return -1;
	len += n;
	// Remove trailing EOLs
	while (buffer[len-1] == '\n')
		len--;
	if (logfmt)
		buffer[len++] = '\n';
	// TODO: Format multiline messages
	buffer[len] = 0;
	return len;
}


void vsyslog(int priority, const char * message, va_list args)
{
	char buffer[1000];

	// Translation of %m to error text not supported yet
	if (strstr(message, "%m"))
		message = "Internal error: \"%%m\" in log message";

	// Format message
	if (format_msg(buffer, sizeof(buffer), !sl_hevtsrc, priority, message, args) < 0)
		strcpy(buffer, "Internal Error: buffer overflow\n");

	if (sl_hevtsrc) {
		// Write to event log
		const char * msgs[1] = { buffer };
		ReportEventA(sl_hevtsrc,
			pri2evtype(priority), // type
			0, MSG_SYSLOG,        // category, message id
			NULL,                 // no security id
			1   , 0,              // 1 string, ...
			msgs, NULL);          // ...     , no data
	}
	else {
		// Append to logfile
		FILE * f;
		if (!(f = fopen(sl_logpath, "a")))
			return;
		fputs(buffer, f);
		fclose(f);
	}
}


#ifdef TEST

void syslog(int priority, const char *message, ...)
{
	va_list args;
	va_start(args, message);
	vsyslog(priority, message, args);
	va_end(args);
}

int main(int argc, char* argv[])
{
	openlog(argc < 2 ? "test" : argv[1], LOG_PID, LOG_DAEMON);
	syslog(LOG_INFO,    "Info\n");
	syslog(LOG_WARNING, "Warning %d\n\n", 42);
	syslog(LOG_ERR,     "Error %s", "Fatal");
	closelog();
	return 0;
}

#endif