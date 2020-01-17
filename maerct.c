char* INFO[3] = {
	"Ma_Sys.ma Emergency Remote Control 1.0.0.3.",
	"Copyright (c) 2015, 2017 Ma_Sys.ma.",
	"For further info send an e-mail to Ma_Sys.ma@web.de.",
};

/* -> http://stackoverflow.com/questions/7546252/what-can-i-use-besides-usleep-
						in-a-modern-posix-environment */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/input.h>
#include <sys/sysinfo.h>

#include "slist.h"
#include "z_getusage.h"

enum blink_states { OFF = 0, SLOW = 1, FAST = 2 };
enum led_values   { LOW = 0, HIGH = 2 };

struct cblst {
	char b[3];       /* blink rates NumLock, CapsLock, ScrollLock */
	int fd;          /* file descriptor */
};

struct wdata {
	pid_t pid;
	struct cblst* cb;
};

/* unit is ms */
#define BSHORT 100
#define BLONG  1000
#define SYSRES 500

#define SEARCHFOR "/usr/bin/X"
#define SLEN      11

#define MAXLEN 256

static void help();
static void run(char* file);
static void doublefork(char* file);
static void* blinkthread(void* raw);
static void inc(struct timespec* tv, long ms);
static char is_alive(struct cblst* blink_state);
static void initev(struct input_event* ev, char code);
static void* sysstatthread(void* raw);
static unsigned long get_cached_kib();
static void get_x_servers(struct list* known_x_servers);
static int is_x_server(struct dirent* dp);
static char process_status_fail(struct list* known_x_servers);
static char process_code(struct cblst* blink_state, int code);
static void kill_all_x_servers();
static void try_new_x_server(struct cblst* cb);
static void startx_sub();
static void waitthread(struct cblst* cb, pid_t pid);
static void* waitthread_sub(void* in);

int main(int argc, char** argv)
{
	if(argc == 1 || strcmp(argv[1], "--help") == 0)
		help(argv[0]);
	else if(argc == 2)
		run(argv[1]);
	else if(argc == 3 && strcmp(argv[1], "-f") == 0)
		doublefork(argv[2]);
	else
		help(argv[0]);

	return EXIT_SUCCESS;
}

static void help(char* appname)
{
	size_t i;
	for(i = 0; i < 3; i++)
		printf("%s\n", INFO[i]);

	putchar('\n');
	printf("USAGE %s [-f] file\n\n", appname);
	puts(
"This program has to be run by the root user. It takes a linux keyboard event\n"
"device, e.g. /dev/input/event0 and grabs it exclusively. -f forks twice.\n\n"
"Table of blink codes (ssl := short short long (off) blink mode)\n"
	);
	puts(
"          NumLock  CapsLock  ScrollLock  Description\n"
"          slow     off       off         Program operating correctly.\n"
"          fast     off       off         Program expecting input.\n"
"          off      fast      off         Process `X` takes >80% CPU.\n"
"          off      slow      off         RAM exceeded."
	);
	puts(
"          off      off       slow        Shorttime Load AVG greater 10.\n"
"          off      off       fast        Command is being executed.\n"
"          off      off       off         Program not operating.\n"
	);
	puts(
"Table of possible inputs\n\n"
"          Key  Description\n"
"          ESC  Cancel pending operation\n"
"          F1   Attempt to kill X11 process\n"
"          F2   Attempt to start (and immediately terminate) new X server.\n"
"          F4   Exit this program (no unlock required)\n"
"          u    Press this before another operation to unlock input."
	);
}

/* -> people.cs.aau.dk/~bnielsen/Tricks/apue/proc/fork2.c */
static void doublefork(char* file)
{
	pid_t pid;

	if((pid = fork()) < 0) {
		perror("First fork failed.");
	} else if(pid == 0) {
		/* first child */
		if((pid = fork()) < 0)
			perror("Second fork failed.");
		else if(pid > 0)
			exit(0); /* exit parent / first child */
		else
			run(file); /* continue in second child process */
	} else {
		/* original / parent */
		waitpid(pid, NULL, 0); /* wait for first child to exit */
		exit(0);
	}
}

static void run(char* file)
{
	pthread_t blinkpid;
	pthread_t statpid;
	struct cblst blink_state = { { OFF, OFF, OFF }, -1 };
	struct input_event ev[2];
	char on = 1;
	int rd;
	char fail;

	blink_state.b[0] = SLOW;

	fail = (blink_state.fd = open(file, O_RDWR)) == -1 ||
		ioctl(blink_state.fd, EVIOCGRAB, 1) ||
		pthread_create(&blinkpid, NULL,
					&blinkthread, &blink_state) != 0 ||
		pthread_create(&statpid, NULL,
					&sysstatthread, &blink_state) != 0;

	if(fail) {
		perror("Error");
		exit(EXIT_FAILURE);
	}

	while(on) {
		if((rd = read(blink_state.fd, ev,
					sizeof(struct input_event) * 2)) <
					sizeof(struct input_event)) {
			perror("End of data? ");
			on = 0;
		} else if(ev[0].value != ' ' && ev[1].value == 1 &&
							ev[1].type == 1) {
			on = process_code(&blink_state, ev[1].code);
		}
	}

	blink_state.b[0] = OFF;
	blink_state.b[1] = OFF;
	blink_state.b[2] = OFF;

	if(pthread_join(blinkpid, NULL) || pthread_join(statpid, NULL)) {
		perror("Join failed");
		exit(EXIT_FAILURE);
	}

	close(blink_state.fd);
}

static void* blinkthread(void* raw)
{
	char nowon = 0;
	struct cblst* blink_state = raw;
	unsigned short_per_long = BLONG / BSHORT;
	unsigned short_pass = 0; /* rotates if it reaches short_per_long */

	struct input_event ev[3];
	struct timespec tv;

	unsigned i;
	unsigned to_invert;

	initev(ev+0, LED_NUML);
	initev(ev+1, LED_CAPSL);
	initev(ev+2, LED_SCROLLL);

	while(is_alive(blink_state)) {
		if(clock_gettime(CLOCK_MONOTONIC, &tv)) {
			perror("Blinkthread could not get clock (DEG).");
			sleep(1);
		}

		to_invert = FAST;
		if(short_pass == 0)
			to_invert |= SLOW;

		nowon = !nowon;

		for(i = 0; i < 3; i++) {
			if(blink_state->b[i] == OFF)
				ev[i].value = 0;
			else if(blink_state->b[i] & to_invert)
				ev[i].value = 2 * nowon;
				/* ev[i].value = 2 - ev[i].value; */
		}

		if(blink_state->fd != -1)
			write(blink_state->fd, ev,
						3 * sizeof(struct input_event));

		if(blink_state->b[0] <= SLOW && blink_state->b[1] <= SLOW &&
						blink_state->b[2] <= SLOW) {
			inc(&tv, BLONG);
			short_pass = short_per_long; /* cheat forward */
		} else {
			inc(&tv, BSHORT);
		}

		if(short_pass >= short_per_long)
			short_pass = 0;
		else
			short_pass++;

		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tv, NULL);
	}

	return NULL;
}

static void inc(struct timespec* tv, long ms)
{
	#define ASECOND 1000000000l
	long unsigned ns = ms * 1000 * 1000;
	if(tv->tv_nsec + ns > ASECOND) {
		tv->tv_nsec = tv->tv_nsec + ns - ASECOND;
		tv->tv_sec++;
	} else {
		tv->tv_nsec += ns;
	}
}

static char is_alive(struct cblst* blink_state)
{
	return !(blink_state->b[0] == OFF && blink_state->b[1] == OFF &&
						blink_state->b[2] == OFF);
}

static void initev(struct input_event* ev, char code)
{
	memset(ev, 0, sizeof(struct input_event));
	ev->type  = EV_LED;
	ev->value = LOW;
	ev->code  = code;
}

static void* sysstatthread(void* raw)
{
	struct cblst* blink_state = raw;
	struct sysinfo si;
	struct timespec tv;
	float shift = (float)(1 << SI_LOAD_SHIFT);
	struct list* known_x_servers = alloc_list(); /* list of process IDs */
	int ccount = 0;
	int memory_free_perc;

	while(is_alive(blink_state)) {
		if(clock_gettime(CLOCK_MONOTONIC, &tv)) {
			perror("Sysstatthread could not get clock (DEG).");
			sleep(1);
		}

		if(ccount++ == 0)
			get_x_servers(known_x_servers);
		else if(ccount == 30)
			ccount = 0;

		sysinfo(&si);
		memory_free_perc = (get_cached_kib() * 1024 + si.freeram +
				si.bufferram) * 100l / (si.totalram);

		if(process_status_fail(known_x_servers))
			blink_state->b[1] = FAST;
		else if(memory_free_perc < 5) /* 5% border */
			blink_state->b[1] = SLOW;
		else
			blink_state->b[1] = OFF;

		if(blink_state->b[2] != FAST) { /* do not modify in operation */
			if(si.loads[0] / shift > 10)
				blink_state->b[2] = SLOW;
			else
				blink_state->b[2] = OFF;
		}

		inc(&tv, SYSRES);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tv, NULL);
	}

	free_list_ncnt(known_x_servers);
	free(known_x_servers);
	return NULL;
}

static unsigned long get_cached_kib()
{
	/* -> https://geekwentfreak-raviteja.rhcloud.com/blog/2011/08/25/
				querying-system-memory-usage-from-c-in-linux/ */
	unsigned long cached_kib = 0;
	FILE* fp = fopen("/proc/meminfo", "r");

	char buf[MAXLEN];
	char* p2;
	char* p1;

	while(!feof(fp)) {
		fgets(buf, MAXLEN, fp);
		p1 = strchr(buf, ':');
		/* not separated => skip line */
		if(p1 == NULL)
			continue;
		/* separate */
		*p1 = 0;
		if(strcmp(buf, "Cached") == 0) {
			/* skip over zero (field after caches) */
			p1++;
			/* trim leading space */
			while(*p1 == ' ' && *p1 != 0)
				p1++;

			/* cut off after trailing space */
			p2 = strchr(p1, ' ');
			/* if no trailing space ignore that for now */
			if(p2 != NULL)
				*p2 = 0;
			cached_kib = atol(p1);
			/* we are only interested in that single line */
			break;
		}
	}
	fclose(fp);
	return cached_kib;
}

static void get_x_servers(struct list* known_x_servers)
{
	DIR* dir;
	struct dirent* dp;
	long pid;

	if((dir = opendir("/proc")) == NULL) {
		perror("Failed to open /proc");
		return;
	}

	free_list_ncnt(known_x_servers);

	while((dp = readdir(dir)) != NULL)
		if(dp->d_type == DT_DIR && (pid = atol(dp->d_name)) != 0
							&& is_x_server(dp))
			add_to_list(known_x_servers, (void*)pid);

	closedir(dir);
}

static int is_x_server(struct dirent* dp)
{
	int ret = 0;
	FILE* res;
	char cmdline[SLEN];
	char fn[NAME_MAX+1];

	fn[0] = 0;
	strcat(fn, "/proc/");
	strcat(fn, dp->d_name);
	strcat(fn, "/cmdline");

	if((res = fopen(fn, "r")) == NULL) {
		perror(fn);
		return 0;
	}

	ret = fread(cmdline, sizeof(char), SLEN, res) == SLEN &&
					(memcmp(cmdline, SEARCHFOR, SLEN) == 0);

	fclose(res);
	return ret;
}

static char process_status_fail(struct list* known_x_servers)
{
	struct list_data* i;
	FOREACH(known_x_servers, i) {
		if(get_usage_simple((long)i->data) > 70)
			return 1;
	}
	return 0;
}

static char process_code(struct cblst* blink_state, int code)
{ 
	if(code == KEY_F4)
		return 0;

	if(code == KEY_U) {
		blink_state->b[0] = FAST;
	} else if(blink_state->b[0] == FAST) {
		blink_state->b[0] = SLOW; /* lock again */
		blink_state->b[2] = FAST; /* operating */
		/* TODO CANCEL DOES NOT CURRENTLY WORK (N_NECESSARY FOR NOW) */
		switch(code) {
		case KEY_ESC: blink_state->b[2] = OFF;             break;
		case KEY_F1:  kill_all_x_servers();                break;
		case KEY_F2:  try_new_x_server(blink_state);       return 1;
		default:      printf("Unknown code: %d\n", code);  break;
		}
		blink_state->b[2] = OFF; /* operation complete */
	}

	return 1;
}

static void kill_all_x_servers()
{
	struct timespec tv;
	unsigned i;
	char found = 1;
	struct list_data* l;
	struct list* known_x_servers = alloc_list();

	tv.tv_sec = 0;
	tv.tv_nsec = 100000000;

	for(i = 0; i < 10 && found; i++) {
		get_x_servers(known_x_servers);
		found = 0;
		FOREACH(known_x_servers, l) {
			found = 1;
			if(!kill((long)l->data, SIGKILL))
				perror("Kill failed");
		}
		if(found)
			nanosleep(&tv, NULL);
	}

	free_list_ncnt(known_x_servers);
	free(known_x_servers);
}

static void try_new_x_server(struct cblst* cb)
{
	pid_t pid;
	if((pid = fork()) == 0)
		startx_sub();
	else
		waitthread(cb, pid);
}

static void startx_sub()
{
	char* env[] = { "HOME=/tmp", (char*)0 };
	if(execle("/usr/bin/startx", "startx", "/bin/false", 0, env) == -1)
		perror("External startx invocation failed.");
	exit(64);
}

static void waitthread(struct cblst* cb, pid_t pid)
{
	pthread_t waitpid;
	struct wdata w;
	w.pid = pid;
	w.cb = cb;
	if(pthread_create(&waitpid, NULL, &waitthread_sub, &w) != 0)
		perror("Failed to create wait thread");
	else
		pthread_detach(waitpid);
}

static void* waitthread_sub(void* in)
{
	struct wdata* w = in;
	pid_t pid = w->pid;
	struct cblst* cb = w->cb;

	waitpid(pid, NULL, 0);
	cb->b[2] = OFF;

	return NULL;
}
