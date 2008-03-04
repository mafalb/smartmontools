/*
 * os_win32/daemon_win32.cpp
 *
 * Home page of code is: http://smartmontools.sourceforge.net
 *
 * Copyright (C) 2004-8 Christian Franke <smartmontools-support@lists.sourceforge.net>
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

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <io.h>

#define WIN32_LEAN_AND_MEAN
// Need MB_SERVICE_NOTIFICATION (NT4/2000/XP), IsDebuggerPresent() (Win98/ME/NT4/2000/XP)
#define _WIN32_WINNT 0x0400 
#include <windows.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "daemon_win32.h"

const char *daemon_win32_c_cvsid = "$Id: daemon_win32.cpp,v 1.12 2008/03/04 22:09:48 ballen4705 Exp $"
DAEMON_WIN32_H_CVSID;


/////////////////////////////////////////////////////////////////////////////

#define ARGUSED(x) ((void)(x))

// Prevent spawning of child process if debugging
#ifdef _DEBUG
#define debugging() IsDebuggerPresent()
#else
#define debugging() FALSE
#endif


#define EVT_NAME_LEN 260

// Internal events (must be > SIGUSRn)
#define EVT_RUNNING   100 // Exists when running, signaled on creation
#define EVT_DETACHED  101 // Signaled when child detaches from console
#define EVT_RESTART   102 // Signaled when child should restart

static void make_name(char * name, int sig)
{
	int i;
	if (!GetModuleFileNameA(NULL, name, EVT_NAME_LEN-10))
		strcpy(name, "DaemonEvent");
	for (i = 0; name[i]; i++) {
		char c = name[i];
		if (!(   ('0' <= c && c <= '9')
		      || ('A' <= c && c <= 'Z')
		      || ('a' <= c && c <= 'z')))
			  name[i] = '_';
	}
	sprintf(name+strlen(name), "-%d", sig);
}


static HANDLE create_event(int sig, BOOL initial, BOOL errmsg, BOOL * exists)
{
	char name[EVT_NAME_LEN];
	HANDLE h;
	if (sig >= 0)
		make_name(name, sig);
	else
		name[0] = 0;
	if (exists)
		*exists = FALSE;
	if (!(h = CreateEventA(NULL, FALSE, initial, (name[0] ? name : NULL)))) {
		if (errmsg)
			fprintf(stderr, "CreateEvent(.,\"%s\"): Error=%ld\n", name, GetLastError());
		return 0;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		if (!exists) {
			if (errmsg)
				fprintf(stderr, "CreateEvent(.,\"%s\"): Exists\n", name);
			CloseHandle(h);
			return 0;
		}
		*exists = TRUE;
	}
	return h;
}


static HANDLE open_event(int sig)
{
	char name[EVT_NAME_LEN];
	make_name(name, sig);
	return OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
}


static int event_exists(int sig)
{
	char name[EVT_NAME_LEN];
	HANDLE h;
	make_name(name, sig);
	if (!(h = OpenEventA(EVENT_MODIFY_STATE, FALSE, name)))
		return 0;
	CloseHandle(h);
	return 1;
}


static int sig_event(int sig)
{
	char name[EVT_NAME_LEN];
	HANDLE h;
	make_name(name, sig);
	if (!(h = OpenEventA(EVENT_MODIFY_STATE, FALSE, name))) {
		make_name(name, EVT_RUNNING);
		if (!(h = OpenEvent(EVENT_MODIFY_STATE, FALSE, name)))
			return -1;
		CloseHandle(h);
		return 0;
	}
	SetEvent(h);
	CloseHandle(h);
	return 1;
}


static void daemon_help(FILE * f, const char * ident, const char * message)
{
	fprintf(f,
		"%s: %s.\n"
		"Use \"%s status|stop|reload|restart|sigusr1|sigusr2\" to control daemon.\n",
		ident, message, ident);
	fflush(f);
}


/////////////////////////////////////////////////////////////////////////////
// Parent Process


static BOOL WINAPI parent_console_handler(DWORD event)
{
	switch (event) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT:
			return TRUE; // Ignore
	}
	return FALSE; // continue with next handler ...
}


static int parent_main(HANDLE rev)
{
	HANDLE dev;
	HANDLE ht[2];
	char * cmdline;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD rc, exitcode;

	// Ignore ^C, ^BREAK in parent
	SetConsoleCtrlHandler(parent_console_handler, TRUE/*add*/);

	// Create event used by child to signal daemon_detach()
	if (!(dev = create_event(EVT_DETACHED, FALSE/*not signaled*/, TRUE, NULL/*must not exist*/))) {
		CloseHandle(rev);
		return 101;
	}

	// Restart process with same args
	cmdline = GetCommandLineA();
	memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
	
	if (!CreateProcessA(
		NULL, cmdline,
		NULL, NULL, TRUE/*inherit*/,
		0, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "CreateProcess(.,\"%s\",.) failed, Error=%ld\n", cmdline, GetLastError());
		CloseHandle(rev); CloseHandle(dev);
		return 101;
	}
	CloseHandle(pi.hThread);

	// Wait for daemon_detach() or exit()
	ht[0] = dev; ht[1] = pi.hProcess;
	rc = WaitForMultipleObjects(2, ht, FALSE/*or*/, INFINITE);
	if (!(/*WAIT_OBJECT_0(0) <= rc && */ rc < WAIT_OBJECT_0+2)) {
		fprintf(stderr, "WaitForMultipleObjects returns %lX\n", rc);
		TerminateProcess(pi.hProcess, 200);
	}
	CloseHandle(rev); CloseHandle(dev);

	// Get exit code
	if (!GetExitCodeProcess(pi.hProcess, &exitcode))
		exitcode = 201;
	else if (exitcode == STILL_ACTIVE) // detach()ed, assume OK
		exitcode = 0;

	CloseHandle(pi.hProcess);
	return exitcode;
}


/////////////////////////////////////////////////////////////////////////////
// Child Process


static int svc_mode;   // Running as service?
static int svc_paused; // Service paused?

static void service_report_status(int state, int waithint);


// Tables of signal handler and corresponding events
typedef void (*sigfunc_t)(int);

#define MAX_SIG_HANDLERS 8

static int num_sig_handlers = 0;
static sigfunc_t sig_handlers[MAX_SIG_HANDLERS];
static int sig_numbers[MAX_SIG_HANDLERS];
static HANDLE sig_events[MAX_SIG_HANDLERS];

static HANDLE sighup_handle, sigint_handle, sigbreak_handle;
static HANDLE sigterm_handle, sigusr1_handle;

static HANDLE running_event;

static int reopen_stdin, reopen_stdout, reopen_stderr;


// Handler for windows console events

static BOOL WINAPI child_console_handler(DWORD event)
{
	// Caution: runs in a new thread
	// TODO: Guard with a mutex
	HANDLE h = 0;
	switch (event) {
		case CTRL_C_EVENT: // <CONTROL-C> (SIGINT)
			h = sigint_handle; break;
		case CTRL_BREAK_EVENT: // <CONTROL-Break> (SIGBREAK/SIGQUIT)
		case CTRL_CLOSE_EVENT: // User closed console or abort via task manager
			h = sigbreak_handle; break;
		case CTRL_LOGOFF_EVENT: // Logout/Shutdown (SIGTERM)
		case CTRL_SHUTDOWN_EVENT:
			h = sigterm_handle; break;
	}
	if (!h)
		return FALSE; // continue with next handler
	// Signal event
	if (!SetEvent(h))
		return FALSE;
	return TRUE;
}


static void child_exit(void)
{
	int i;
	char * cmdline;
	HANDLE rst;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	for (i = 0; i < num_sig_handlers; i++)
		CloseHandle(sig_events[i]);
	num_sig_handlers = 0;
	CloseHandle(running_event); running_event = 0;

	// Restart?
	if (!(rst = open_event(EVT_RESTART)))
		return; // No => normal exit

	// Yes => Signal exit and restart process
	Sleep(500);
	SetEvent(rst);
	CloseHandle(rst);
	Sleep(500);

	cmdline = GetCommandLineA();
	memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;

	if (!CreateProcessA(
		NULL, cmdline,
		NULL, NULL, TRUE/*inherit*/,
		0, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "CreateProcess(.,\"%s\",.) failed, Error=%ld\n", cmdline, GetLastError());
	}
	CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

static int child_main(HANDLE hev,int (*main_func)(int, char **), int argc, char **argv)
{
	// Keep EVT_RUNNING open until exit
	running_event = hev;

	// Install console handler
	SetConsoleCtrlHandler(child_console_handler, TRUE/*add*/);

	// Install restart handler
	atexit(child_exit);

	// Continue in main_func() to do the real work
	return main_func(argc, argv);
}


// Simulate signal()

sigfunc_t daemon_signal(int sig, sigfunc_t func)
{
	int i;
	HANDLE h;
	if (func == SIG_DFL || func == SIG_IGN)
		return func; // TODO
	for (i = 0; i < num_sig_handlers; i++) {
		if (sig_numbers[i] == sig) {
			sigfunc_t old = sig_handlers[i];
			sig_handlers[i] = func;
			return old;
		}
	}
	if (num_sig_handlers >= MAX_SIG_HANDLERS)
		return SIG_ERR;
	if (!(h = create_event((!svc_mode ? sig : -1), FALSE, TRUE, NULL)))
		return SIG_ERR;
	sig_events[num_sig_handlers]   = h;
	sig_numbers[num_sig_handlers]  = sig;
	sig_handlers[num_sig_handlers] = func;
	switch (sig) {
		case SIGHUP:   sighup_handle   = h; break;
		case SIGINT:   sigint_handle   = h; break;
		case SIGTERM:  sigterm_handle  = h; break;
		case SIGBREAK: sigbreak_handle = h; break;
		case SIGUSR1:  sigusr1_handle  = h; break;
	}
	num_sig_handlers++;
	return SIG_DFL;
}


// strsignal()

const char * daemon_strsignal(int sig)
{
	switch (sig) {
		case SIGHUP:  return "SIGHUP";
		case SIGINT:  return "SIGINT";
		case SIGTERM: return "SIGTERM";
		case SIGBREAK:return "SIGBREAK";
		case SIGUSR1: return "SIGUSR1";
		case SIGUSR2: return "SIGUSR2";
		default:      return "*UNKNOWN*";
	}
}


// Simulate sleep()

void daemon_sleep(int seconds)
{
	do {
		if (num_sig_handlers <= 0) {
			Sleep(seconds*1000L);
		}
		else {
			// Wait for any signal or timeout
			DWORD rc = WaitForMultipleObjects(num_sig_handlers, sig_events,
				FALSE/*OR*/, seconds*1000L);
			if (rc != WAIT_TIMEOUT) {
				if (!(/*WAIT_OBJECT_0(0) <= rc && */ rc < WAIT_OBJECT_0+(unsigned)num_sig_handlers)) {
					fprintf(stderr,"WaitForMultipleObjects returns %lu\n", rc);
					Sleep(seconds*1000L);
					return;
				}
				// Call Handler
				sig_handlers[rc-WAIT_OBJECT_0](sig_numbers[rc-WAIT_OBJECT_0]);
				break;
			}
		}
	} while (svc_paused);
}


// Disable/Enable console

void daemon_disable_console()
{
	SetConsoleCtrlHandler(child_console_handler, FALSE/*remove*/);
	reopen_stdin = reopen_stdout = reopen_stderr = 0;
	if (isatty(fileno(stdin))) {
		fclose(stdin); reopen_stdin = 1;
	}
	if (isatty(fileno(stdout))) {
		fclose(stdout); reopen_stdout = 1;
	}
	if (isatty(fileno(stderr))) {
		fclose(stderr); reopen_stderr = 1;
	}
	FreeConsole();
	SetConsoleCtrlHandler(child_console_handler, TRUE/*add*/);
}

int daemon_enable_console(const char * title)
{
	BOOL ok;
	SetConsoleCtrlHandler(child_console_handler, FALSE/*remove*/);
	ok = AllocConsole();
	SetConsoleCtrlHandler(child_console_handler, TRUE/*add*/);
	if (!ok)
		return -1;
	if (title)
		SetConsoleTitleA(title);
	if (reopen_stdin)
		freopen("conin$",  "r", stdin);
	if (reopen_stdout)
		freopen("conout$", "w", stdout);
	if (reopen_stderr)
		freopen("conout$", "w", stderr);
	reopen_stdin = reopen_stdout = reopen_stderr = 0;
	return 0;
}


// Detach daemon from console & parent

int daemon_detach(const char * ident)
{
	if (!svc_mode) {
		if (ident) {
			// Print help
			FILE * f = ( isatty(fileno(stdout)) ? stdout
					   : isatty(fileno(stderr)) ? stderr : NULL);
			if (f)
				daemon_help(f, ident, "now detaches from console into background mode");
		}
		// Signal detach to parent
		if (sig_event(EVT_DETACHED) != 1) {
			if (!debugging())
				return -1;
		}
		daemon_disable_console();
	}
	else {
		// Signal end of initialization to service control manager
		service_report_status(SERVICE_RUNNING, 0);
		reopen_stdin = reopen_stdout = reopen_stderr = 1;
	}

	return 0;
}


/////////////////////////////////////////////////////////////////////////////
// MessageBox

#ifndef _MT
//MT runtime not necessary, because mbox_thread uses no unsafe lib functions
//#error Program must be linked with multithreaded runtime library
#endif

static LONG mbox_count; // # mbox_thread()s
static HANDLE mbox_mutex; // Show 1 box at a time (not necessary for service)

typedef struct mbox_args_s {
	HANDLE taken; const char * title, * text; int mode;
} mbox_args;


// Thread to display one message box

static ULONG WINAPI mbox_thread(LPVOID arg)
{
	// Take args
	mbox_args * mb = (mbox_args *)arg;
	char title[100]; char text[1000]; int mode;
	strncpy(title, mb->title, sizeof(title)-1); title[sizeof(title)-1] = 0;
	strncpy(text , mb->text , sizeof(text )-1); text [sizeof(text )-1] = 0;
	mode = mb->mode;
	SetEvent(mb->taken);

	// Show only one box at a time
	WaitForSingleObject(mbox_mutex, INFINITE);
	MessageBoxA(NULL, text, title, mode);
	ReleaseMutex(mbox_mutex);

	InterlockedDecrement(&mbox_count);
	return 0;
}


// Display a message box
int daemon_messagebox(int system, const char * title, const char * text)
{
	mbox_args mb;
	HANDLE ht; DWORD tid;

	// Create mutex during first call
	if (!mbox_mutex)
		mbox_mutex = CreateMutex(NULL/*!inherit*/, FALSE/*!owned*/, NULL/*unnamed*/);

	// Allow at most 10 threads
	if (InterlockedIncrement(&mbox_count) > 10) {
		InterlockedDecrement(&mbox_count);
		return -1;
	}

	// Create thread
	mb.taken = CreateEvent(NULL/*!inherit*/, FALSE, FALSE/*!signaled*/, NULL/*unnamed*/);
	mb.mode = MB_OK|MB_ICONWARNING
	         |(svc_mode?MB_SERVICE_NOTIFICATION:0)
	         |(system?MB_SYSTEMMODAL:MB_APPLMODAL);
	mb.title = title; mb.text = text;
	mb.text = text;
	if (!(ht = CreateThread(NULL, 0, mbox_thread, &mb, 0, &tid)))
		return -1;

	// Wait for args taken
	if (WaitForSingleObject(mb.taken, 10000) != WAIT_OBJECT_0)
		TerminateThread(ht, 0);
	CloseHandle(mb.taken);
	CloseHandle(ht);
	return 0;
}


/////////////////////////////////////////////////////////////////////////////

// Spawn a command and redirect <inpbuf >outbuf
// return command's exitcode or -1 on error

int daemon_spawn(const char * cmd,
                 const char * inpbuf, int inpsize,
                 char *       outbuf, int outsize )
{
	HANDLE pipe_inp_r, pipe_inp_w, pipe_out_r, pipe_out_w, pipe_err_w, h;
	char temp_path[MAX_PATH];
	DWORD flags, num_io, exitcode;
	int use_file, state, i;
	SECURITY_ATTRIBUTES sa;
	STARTUPINFO si; PROCESS_INFORMATION pi;
	HANDLE self = GetCurrentProcess();

	if (GetVersion() & 0x80000000L) {
		// Win9x/ME: A calling process never receives EOF if output of COMMAND.COM or
		// any other DOS program is redirected via a pipe. Using a temp file instead.
		use_file = 1; flags = DETACHED_PROCESS;
	}
	else {
		// NT4/2000/XP: If DETACHED_PROCESS is used, CMD.EXE opens a new console window
		// for each external command in a redirected .BAT file.
		// Even (DETACHED_PROCESS|CREATE_NO_WINDOW) does not work.
		use_file = 0; flags = CREATE_NO_WINDOW;
	}

	// Create stdin pipe with inheritable read side
	memset(&sa, 0, sizeof(sa)); sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if (!CreatePipe(&pipe_inp_r, &h, &sa/*inherit*/, inpsize*2+13))
		return -1;
	if (!DuplicateHandle(self, h, self, &pipe_inp_w,
		0, FALSE/*!inherit*/, DUPLICATE_SAME_ACCESS|DUPLICATE_CLOSE_SOURCE)) {
		CloseHandle(pipe_inp_r);
		return -1;
	}

	memset(&sa, 0, sizeof(sa)); sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;
	if (!use_file) {
		// Create stdout pipe with inheritable write side
		if (!CreatePipe(&h, &pipe_out_w, &sa/*inherit*/, outsize)) {
			CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
			return -1;
		}
	}
	else {
		// Create temp file with inheritable write handle
		char temp_dir[MAX_PATH];
		if (!GetTempPathA(sizeof(temp_dir), temp_dir))
			strcpy(temp_dir, ".");
		if (!GetTempFileNameA(temp_dir, "out"/*prefix*/, 0/*create unique*/, temp_path)) {
			CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
			return -1;
		}
		if ((h = CreateFileA(temp_path, GENERIC_READ|GENERIC_WRITE,
			0/*no sharing*/, &sa/*inherit*/, OPEN_EXISTING, 0, NULL)) == INVALID_HANDLE_VALUE) {
			CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
			return -1;
		}
		if (!DuplicateHandle(self, h, self, &pipe_out_w,
			GENERIC_WRITE, TRUE/*inherit*/, 0)) {
			CloseHandle(h); CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
			return -1;
		}
	}

	if (!DuplicateHandle(self, h, self, &pipe_out_r,
		GENERIC_READ, FALSE/*!inherit*/, DUPLICATE_CLOSE_SOURCE)) {
		CloseHandle(pipe_out_w); CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
		return -1;
	}

	// Create stderr handle as dup of stdout write side
	if (!DuplicateHandle(self, pipe_out_w, self, &pipe_err_w,
		0, TRUE/*inherit*/, DUPLICATE_SAME_ACCESS)) {
		CloseHandle(pipe_out_r); CloseHandle(pipe_out_w);
		CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
		return -1;
	}

	// Create process with pipes/file as stdio
	memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
	si.hStdInput  = pipe_inp_r;
	si.hStdOutput = pipe_out_w;
	si.hStdError  = pipe_err_w;
	si.dwFlags = STARTF_USESTDHANDLES;
	if (!CreateProcessA(
		NULL, (char*)cmd,
		NULL, NULL, TRUE/*inherit*/,
		flags/*DETACHED_PROCESS or CREATE_NO_WINDOW*/,
		NULL, NULL, &si, &pi)) {
		CloseHandle(pipe_err_w);
		CloseHandle(pipe_out_r); CloseHandle(pipe_out_w);
		CloseHandle(pipe_inp_r); CloseHandle(pipe_inp_w);
		return -1;
	}
	CloseHandle(pi.hThread);
	// Close inherited handles
	CloseHandle(pipe_inp_r);
	CloseHandle(pipe_out_w);
	CloseHandle(pipe_err_w);

	// Copy inpbuf to stdin
	// convert \n => \r\n 
	for (i = 0; i < inpsize; ) {
		int len = 0;
		while (i+len < inpsize && inpbuf[i+len] != '\n')
			len++;
		if (len > 0)
			WriteFile(pipe_inp_w, inpbuf+i, len, &num_io, NULL);
		i += len;
		if (i < inpsize) {
			WriteFile(pipe_inp_w, "\r\n", 2, &num_io, NULL);
			i++;
		}
	}
	CloseHandle(pipe_inp_w);

	exitcode = 42;
	for (state = 0; state < 2; state++) {
		// stdout pipe: read pipe first
		// stdout file: wait for process first
		if (state == use_file) {
			// Copy stdout to output buffer until full, rest to /dev/null
			// convert \r\n => \n
			if (use_file)
				SetFilePointer(pipe_out_r, 0, NULL, FILE_BEGIN);
			for (i = 0; ; ) {
				char buf[256];
				int j;
				if (!ReadFile(pipe_out_r, buf, sizeof(buf), &num_io, NULL) || num_io == 0)
					break;
				for (j = 0; i < outsize-1 && j < (int)num_io; j++) {
					if (buf[j] != '\r')
						outbuf[i++] = buf[j];
				}
			}
			outbuf[i] = 0;
			CloseHandle(pipe_out_r);
			if (use_file)
				DeleteFileA(temp_path);
		}
		else {
			// Wait for process exitcode
			WaitForSingleObject(pi.hProcess, INFINITE);
			GetExitCodeProcess(pi.hProcess, &exitcode);
			CloseHandle(pi.hProcess);
		}
	}
	return exitcode;
}


/////////////////////////////////////////////////////////////////////////////
// Initd Functions

static int wait_signaled(HANDLE h, int seconds)
{
	int i;
	for (i = 0; ; ) {
		if (WaitForSingleObject(h, 1000L) == WAIT_OBJECT_0)
			return 0;
		if (++i >= seconds)
			return -1;
		fputchar('.'); fflush(stdout);
	}
}


static int wait_evt_running(int seconds, int exists)
{
	int i;
	if (event_exists(EVT_RUNNING) == exists)
		return 0;
	for (i = 0; ; ) {
		Sleep(1000);
		if (event_exists(EVT_RUNNING) == exists)
			return 0;
		if (++i >= seconds)
			return -1;
		fputchar('.'); fflush(stdout);
	}
}


static int is_initd_command(char * s)
{
	if (!strcmp(s, "status"))
		return EVT_RUNNING;
	if (!strcmp(s, "stop"))
		return SIGTERM;
	if (!strcmp(s, "reload"))
		return SIGHUP;
	if (!strcmp(s, "sigusr1"))
		return SIGUSR1;
	if (!strcmp(s, "sigusr2"))
		return SIGUSR2;
	if (!strcmp(s, "restart"))
		return EVT_RESTART;
	return -1;
}


static int initd_main(const char * ident, int argc, char **argv)
{
	int rc;
	if (argc < 2)
		return -1;
	if ((rc = is_initd_command(argv[1])) < 0)
		return -1;
	if (argc != 2) {
		printf("%s: no arguments allowed for command %s\n", ident, argv[1]);
		return 1;
	}

	switch (rc) {
		default:
		case EVT_RUNNING:
			printf("Checking for %s:", ident); fflush(stdout);
			rc = event_exists(EVT_RUNNING);
			puts(rc ? " running" : " not running");
			return (rc ? 0 : 1);

		case SIGTERM:
			printf("Stopping %s:", ident); fflush(stdout);
			rc = sig_event(SIGTERM);
			if (rc <= 0) {
				puts(rc < 0 ? " not running" : " error");
				return (rc < 0 ? 0 : 1);
			}
			rc = wait_evt_running(10, 0);
			puts(!rc ? " done" : " timeout");
			return (!rc ? 0 : 1);

		case SIGHUP:
			printf("Reloading %s:", ident); fflush(stdout);
			rc = sig_event(SIGHUP);
			puts(rc > 0 ? " done" : rc == 0 ? " error" : " not running");
			return (rc > 0 ? 0 : 1);

		case SIGUSR1:
		case SIGUSR2:
			printf("Sending SIGUSR%d to %s:", (rc-SIGUSR1+1), ident); fflush(stdout);
			rc = sig_event(rc);
			puts(rc > 0 ? " done" : rc == 0 ? " error" : " not running");
			return (rc > 0 ? 0 : 1);

		case EVT_RESTART:
			{
				HANDLE rst;
				printf("Stopping %s:", ident); fflush(stdout);
				if (event_exists(EVT_DETACHED)) {
					puts(" not detached, cannot restart");
					return 1;
				}
				if (!(rst = create_event(EVT_RESTART, FALSE, FALSE, NULL))) {
					puts(" error");
					return 1;
				}
				rc = sig_event(SIGTERM);
				if (rc <= 0) {
					puts(rc < 0 ? " not running" : " error");
					CloseHandle(rst);
					return 1;
				}
				rc = wait_signaled(rst, 10);
				CloseHandle(rst);
				if (rc) {
					puts(" timeout");
					return 1;
				}
				puts(" done");
				Sleep(100);

				printf("Starting %s:", ident); fflush(stdout);
				rc = wait_evt_running(10, 1);
				puts(!rc ? " done" : " error");
				return (!rc ? 0 : 1);
			}
	}
}


/////////////////////////////////////////////////////////////////////////////
// Windows Service Functions

int daemon_winsvc_exitcode; // Set by app to exit(code)

static SERVICE_STATUS_HANDLE svc_handle;
static SERVICE_STATUS svc_status;


// Report status to SCM

static void service_report_status(int state, int seconds)
{
	// TODO: Avoid race
	static DWORD checkpoint = 1;
	static DWORD accept_more = SERVICE_ACCEPT_PARAMCHANGE; // Win2000/XP
	svc_status.dwCurrentState = state;
	svc_status.dwWaitHint = seconds*1000;
	switch (state) {
		default:
			svc_status.dwCheckPoint = checkpoint++;
			break;
		case SERVICE_RUNNING:
		case SERVICE_STOPPED:
			svc_status.dwCheckPoint = 0;
	}
	switch (state) {
		case SERVICE_START_PENDING:
		case SERVICE_STOP_PENDING:
			svc_status.dwControlsAccepted = 0;
			break;
		default:
			svc_status.dwControlsAccepted =
				SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN|
				SERVICE_ACCEPT_PAUSE_CONTINUE|accept_more;
			break;
	}
	if (!SetServiceStatus(svc_handle, &svc_status)) {
		if (svc_status.dwControlsAccepted & accept_more) {
			// Retry without SERVICE_ACCEPT_PARAMCHANGE (WinNT4)
			svc_status.dwControlsAccepted &= ~accept_more;
			accept_more = 0;
			SetServiceStatus(svc_handle, &svc_status);
		}
	}
}


// Control the service, called by SCM

static void WINAPI service_control(DWORD ctrlcode)
{
	switch (ctrlcode) {
		case SERVICE_CONTROL_STOP:
		case SERVICE_CONTROL_SHUTDOWN:
			service_report_status(SERVICE_STOP_PENDING, 30);
			svc_paused = 0;
			SetEvent(sigterm_handle);
			break;
		case SERVICE_CONTROL_PARAMCHANGE: // Win2000/XP
			service_report_status(svc_status.dwCurrentState, 0);
			svc_paused = 0;
			SetEvent(sighup_handle); // reload
			break;
		case SERVICE_CONTROL_PAUSE:
			service_report_status(SERVICE_PAUSED, 0);
			svc_paused = 1;
			break;
		case SERVICE_CONTROL_CONTINUE:
			service_report_status(SERVICE_RUNNING, 0);
			{
				int was_paused = svc_paused;
				svc_paused = 0;
				SetEvent(was_paused ? sighup_handle : sigusr1_handle); // reload:recheck
			}
			break;
		case SERVICE_CONTROL_INTERROGATE:
		default: // unknown
			service_report_status(svc_status.dwCurrentState, 0);
			break;
	}
}


// Exit handler for service

static void service_exit(void)
{
	// Close signal events
	int i;
	for (i = 0; i < num_sig_handlers; i++)
		CloseHandle(sig_events[i]);
	num_sig_handlers = 0;

	// Set exitcode
	if (daemon_winsvc_exitcode) {
		svc_status.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
		svc_status.dwServiceSpecificExitCode = daemon_winsvc_exitcode;
	}
	// Report stopped
	service_report_status(SERVICE_STOPPED, 0);
}


// Variables for passing main(argc, argv) from daemon_main to service_main()
static int (*svc_main_func)(int, char **);
static int svc_main_argc;
static char ** svc_main_argv;

// Main function for service, called by service dispatcher

static void WINAPI service_main(DWORD argc, LPSTR * argv)
{
	char path[MAX_PATH], *p;
	ARGUSED(argc);

	// Register control handler
	svc_handle = RegisterServiceCtrlHandler(argv[0], service_control);

	// Init service status
	svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS|SERVICE_INTERACTIVE_PROCESS;
	service_report_status(SERVICE_START_PENDING, 10);

	// Service started in \windows\system32, change to .exe directory
	if (GetModuleFileNameA(NULL, path, sizeof(path)) && (p = strrchr(path, '\\'))) {
		*p = 0; SetCurrentDirectoryA(path);
	}
	
	// Install exit handler
	atexit(service_exit);

	// Do the real work, service status later updated by daemon_detach()
	daemon_winsvc_exitcode = svc_main_func(svc_main_argc, svc_main_argv);

	exit(daemon_winsvc_exitcode);
	// ... continued in service_exit()
}


/////////////////////////////////////////////////////////////////////////////
// Windows Service Admin Functions

// Set Service description (Win2000/XP)

static int svcadm_setdesc(SC_HANDLE hs, const char * desc)
{
	HINSTANCE hdll;
	BOOL (WINAPI * ChangeServiceConfig2A_p)(SC_HANDLE, DWORD, LPVOID);
	BOOL ret;
	if (!(hdll = LoadLibraryA("ADVAPI32.DLL")))
		return FALSE;
	if (!((ChangeServiceConfig2A_p = (BOOL (WINAPI *)(SC_HANDLE, DWORD, LPVOID))GetProcAddress(hdll, "ChangeServiceConfig2A"))))
		ret = FALSE;
	else {
		SERVICE_DESCRIPTIONA sd = { (char *)desc };
		ret = ChangeServiceConfig2A_p(hs, SERVICE_CONFIG_DESCRIPTION, &sd);
	}
	FreeLibrary(hdll);
	return ret;
}


// Service install/remove commands

static int svcadm_main(const char * ident, const daemon_winsvc_options * svc_opts,
                       int argc, char **argv                                      )
{
	int remove; long err;
	SC_HANDLE hm, hs;

	if (argc < 2)
		return -1;
	if (!strcmp(argv[1], "install"))
		remove = 0;
	else if (!strcmp(argv[1], "remove")) {
		if (argc != 2) {
			printf("%s: no arguments allowed for command remove\n", ident);
			return 1;
		}
		remove = 1;
	}
	else
		return -1;

	printf("%s service %s:", (!remove?"Installing":"Removing"), ident); fflush(stdout);

	// Open SCM
	if (!(hm = OpenSCManager(NULL/*local*/, NULL/*default*/, SC_MANAGER_ALL_ACCESS))) {
		if ((err = GetLastError()) == ERROR_ACCESS_DENIED)
			puts(" access to SCManager denied");
		else if (err == ERROR_CALL_NOT_IMPLEMENTED)
			puts(" services not implemented on this version of Windows");
		else
			printf(" cannot open SCManager, Error=%ld\n", err);
		return 1;
	}

	if (!remove) {
		char path[MAX_PATH+100];
		int i;
		// Get program path
		if (!GetModuleFileNameA(NULL, path, MAX_PATH)) {
			printf(" unknown program path, Error=%ld\n", GetLastError());
			CloseServiceHandle(hm);
			return 1;
		}
		// Append options
		strcat(path, " "); strcat(path, svc_opts->cmd_opt);
		for (i = 2; i < argc; i++) {
			const char * s = argv[i];
			if (strlen(path)+strlen(s)+1 >= sizeof(path))
				break;
			strcat(path, " "); strcat(path, s);
		}
		// Create
		if (!(hs = CreateService(hm,
			svc_opts->svcname, svc_opts->dispname,
			SERVICE_ALL_ACCESS,
			SERVICE_WIN32_OWN_PROCESS|SERVICE_INTERACTIVE_PROCESS,
			SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, path,
			NULL/*no load ordering*/, NULL/*no tag id*/,
			""/*no depedencies*/, NULL/*local system account*/, NULL/*no pw*/))) {
			if ((err = GetLastError()) == ERROR_SERVICE_EXISTS)
				puts(" the service is already installed");
			else if (err == ERROR_SERVICE_MARKED_FOR_DELETE)
				puts(" service is still running and marked for deletion\n"
				     "Stop the service and retry install");
			else
				printf(" failed, Error=%ld\n", err);
			CloseServiceHandle(hm);
			return 1;
		}
		// Set optional description
		if (svc_opts->descript)
			svcadm_setdesc(hs, svc_opts->descript);
	}
	else {
		// Open
		if (!(hs = OpenService(hm, svc_opts->svcname, SERVICE_ALL_ACCESS))) {
			puts(" not found");
			CloseServiceHandle(hm);
			return 1;
		}
		// TODO: Stop service if running
		// Remove
		if (!DeleteService(hs)) {
			if ((err = GetLastError()) == ERROR_SERVICE_MARKED_FOR_DELETE)
				puts(" service is still running and marked for deletion\n"
				     "Stop the service to remove it");
			else
				printf(" failed, Error=%ld\n", err);
			CloseServiceHandle(hs); CloseServiceHandle(hm);
			return 1;
		}
	}
	puts(" done");
	CloseServiceHandle(hs); CloseServiceHandle(hm);
	return 0;
}


/////////////////////////////////////////////////////////////////////////////
// Main Function

// This function must be called from main()
// main_func is the function doing the real work

int daemon_main(const char * ident, const daemon_winsvc_options * svc_opts,
                int (*main_func)(int, char **), int argc, char **argv      )
{
	int rc;
#ifdef _DEBUG
	// Enable Debug heap checks
	_CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG)
		|_CRTDBG_ALLOC_MEM_DF|_CRTDBG_CHECK_ALWAYS_DF|_CRTDBG_LEAK_CHECK_DF);
#endif

	// Check for [status|stop|reload|restart|sigusr1|sigusr2] parameters
	if ((rc = initd_main(ident, argc, argv)) >= 0)
		return rc;
	// Check for [install|remove] parameters
	if (svc_opts && (rc = svcadm_main(ident, svc_opts, argc, argv)) >= 0)
		return rc;

	// Run as service if svc_opts.cmd_opt is given as first(!) argument
	svc_mode = (svc_opts && argc >= 2 && !strcmp(argv[1], svc_opts->cmd_opt));

	if (!svc_mode) {
		// Daemon: Try to simulate a Unix-like daemon
		HANDLE rev;
		BOOL exists;

		// Create main event to detect process type:
		// 1. new: parent process => start child and wait for detach() or exit() of child.
		// 2. exists && signaled: child process => do the real work, signal detach() to parent
		// 3. exists && !signaled: already running => exit()
		if (!(rev = create_event(EVT_RUNNING, TRUE/*signaled*/, TRUE, &exists)))
			return 100;

		if (!exists && !debugging()) {
			// Event new => parent process
			return parent_main(rev);
		}

		if (WaitForSingleObject(rev, 0) == WAIT_OBJECT_0) {
			// Event was signaled => In child process
			return child_main(rev, main_func, argc, argv);
		}

		// Event no longer signaled => Already running!
		daemon_help(stdout, ident, "already running");
		CloseHandle(rev);
		return 1;
	}
	else {
		// Service: Start service_main() via SCM
		SERVICE_TABLE_ENTRY service_table[] = {
			{ (char*)svc_opts->svcname, service_main }, { NULL, NULL }
		};

		svc_main_func = main_func;
		svc_main_argc = argc;
		svc_main_argv = argv;
		if (!StartServiceCtrlDispatcher(service_table)) {
			printf("%s: cannot dispatch service, Error=%ld\n"
				"Option \"%s\" cannot be used to start %s as a service from console.\n"
				"Use \"%s install ...\" to install the service\n"
				"and \"net start %s\" to start it.\n",
				ident, GetLastError(), svc_opts->cmd_opt, ident, ident, ident);

#ifdef _DEBUG
			if (debugging())
				service_main(argc, argv);
#endif
			return 100;
		}
		Sleep(1000);
		ExitThread(0); // Do not redo exit() processing
		/*NOTREACHED*/
		return 0;
	}
}


/////////////////////////////////////////////////////////////////////////////
// Test Program

#ifdef TEST

static volatile sig_atomic_t caughtsig = 0;

static void sig_handler(int sig)
{
	caughtsig = sig;
}

static void test_exit(void)
{
	printf("Main exit\n");
}

int test_main(int argc, char **argv)
{
	int i;
	int debug = 0;
	char * cmd = 0;

	printf("PID=%ld\n", GetCurrentProcessId());
	for (i = 0; i < argc; i++) {
		printf("%d: \"%s\"\n", i, argv[i]);
		if (!strcmp(argv[i],"-d"))
			debug = 1;
	}
	if (argc > 1 && argv[argc-1][0] != '-')
		cmd = argv[argc-1];

	daemon_signal(SIGINT, sig_handler);
	daemon_signal(SIGBREAK, sig_handler);
	daemon_signal(SIGTERM, sig_handler);
	daemon_signal(SIGHUP, sig_handler);
	daemon_signal(SIGUSR1, sig_handler);
	daemon_signal(SIGUSR2, sig_handler);

	atexit(test_exit);

	if (!debug) {
		printf("Preparing to detach...\n");
		Sleep(2000);
		daemon_detach("test");
		printf("Detached!\n");
	}

	for (;;) {
		daemon_sleep(1);
		printf("."); fflush(stdout);
		if (caughtsig) {
			if (caughtsig == SIGUSR2) {
				debug ^= 1;
				if (debug)
					daemon_enable_console("Daemon[Debug]");
				else
					daemon_disable_console();
			}
			else if (caughtsig == SIGUSR1 && cmd) {
				char inpbuf[200], outbuf[1000]; int rc;
				strcpy(inpbuf, "Hello\nWorld!\n");
				rc = daemon_spawn(cmd, inpbuf, strlen(inpbuf), outbuf, sizeof(outbuf));
				if (!debug)
					daemon_enable_console("Command output");
				printf("\"%s\" returns %d\n", cmd, rc);
				if (rc >= 0)
					printf("output:\n%s.\n", outbuf);
				fflush(stdout);
				if (!debug) {
					Sleep(10000); daemon_disable_console();
				}
			}
			printf("[PID=%ld: Signal=%d]", GetCurrentProcessId(), caughtsig); fflush(stdout);
			if (caughtsig == SIGTERM || caughtsig == SIGBREAK)
				break;
			caughtsig = 0;
		}
	}
	printf("\nExiting on signal %d\n", caughtsig);
	return 0;
}


int main(int argc, char **argv)
{
	static const daemon_winsvc_options svc_opts = {
	"-s", "test", "Test Service", "Service to test daemon_win32.c Module"
	};

	return daemon_main("testd", &svc_opts, test_main, argc, argv);
}

#endif
