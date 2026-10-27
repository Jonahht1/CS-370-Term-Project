/*
 * sensortest.c - watch all doorway sensors together (libgpiod v2).
 *
 * Grown from tools/beamtest/beamtest.c: same poll loop, signal handling,
 * and cleanup, but one line request now covers every line in the sensor
 * table below, each with the bias and debounce its circuit needs.
 *
 * The two beams will be mounted one above the other (beam_lo under
 * beam_hi; not mounted yet) to tell human-sized from pet-sized activity;
 * they are not used for direction.
 *
 * Every edge is printed with the sensor name and its new state, and the
 * same line is appended to a log file (default events.log, or the path
 * given as the only argument). The log is fsync'd after each batch of
 * events; if opening, writing, or syncing it fails, the program exits
 * with an error rather than keep running without saving events. All
 * lines are inputs; no line is ever configured as an output.
 *
 * Usage: sensortest [logfile]
 * Stop with Ctrl+C (SIGINT) or SIGTERM; all resources are released.
 * `make test` checks the applied line settings and a clean ASan exit.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <gpiod.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CHIP_PATH   "/dev/gpiochip0"  /* pinctrl-rp1 on the Pi 5 */
#define CONSUMER    "sensortest"
#define EVENT_BUF   16                /* edge events read per wakeup */
#define POLL_MS     500               /* max wait before re-checking stop flag */
#define DEFAULT_LOG "events.log"      /* relative to the current directory */
#define LOG_LINE    256               /* max bytes in one formatted line */

struct sensor {
	const char *name;
	unsigned int offset;       /* BCM GPIO number == line offset */
	enum gpiod_line_bias bias;
	unsigned long debounce_us; /* 0 = no debounce */
	const char *high_label;    /* state when the line reads HIGH */
	const char *low_label;     /* state when the line reads LOW */
};

/*
 * Beams (physical pins 11 and 15): the receiver signal comes through an
 * external 100k/100k divider, so an internal pull would shift it; no
 * debounce, since delaying edges would distort when both beams overlap.
 * Door (physical pin 13): the reed switch only shorts the line to ground,
 * so it needs the pull-up; its contacts bounce, so debounce 50 ms.
 */
static const struct sensor sensors[] = {
	/* name      GPIO  bias                      debounce  high     low */
	{ "beam_lo", 17u,  GPIOD_LINE_BIAS_DISABLED, 0,        "clear", "blocked" },
	{ "beam_hi", 22u,  GPIOD_LINE_BIAS_DISABLED, 0,        "clear", "blocked" },
	{ "door",    27u,  GPIOD_LINE_BIAS_PULL_UP,  50000,    "open",  "closed" },
};

#define NUM_SENSORS (sizeof(sensors) / sizeof(sensors[0]))

static volatile sig_atomic_t stop_requested = 0;

static const char *log_path = DEFAULT_LOG;
static int log_fd = -1;

static void on_signal(int signo)
{
	(void)signo;
	stop_requested = 1;
}

/* Return the sensor on this line offset, or NULL if none is. */
static const struct sensor *sensor_by_offset(unsigned int offset)
{
	for (size_t i = 0; i < NUM_SENSORS; i++) {
		if (sensors[i].offset == offset)
			return &sensors[i];
	}
	return NULL;
}

/* Format local wall-clock time, e.g. "2026-10-14 09:30:01.123", into buf. */
static void format_wall_time(char *buf, size_t size)
{
	struct timespec ts;
	struct tm tm;
	char secs[32];

	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &tm);
	strftime(secs, sizeof(secs), "%Y-%m-%d %H:%M:%S", &tm);
	snprintf(buf, size, "%s.%03ld", secs, ts.tv_nsec / 1000000L);
}

/* Write all len bytes, retrying short writes. Returns 0 or -1 (errno set). */
static int write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR)
				continue; /* interrupted before writing: retry */
			return -1;
		}
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

/*
 * Send one formatted line to the terminal and append it to the log.
 * The line is not on disk until sync_log() runs. Returns 0 or -1.
 */
static int output_line(const char *line, int len)
{
	if (len < 0 || len >= LOG_LINE) {
		fprintf(stderr, "sensortest: log line too long\n");
		return -1;
	}
	fputs(line, stdout);
	fflush(stdout);
	if (write_all(log_fd, line, (size_t)len) < 0) {
		fprintf(stderr, "sensortest: write %s: %s\n", log_path,
			strerror(errno));
		return -1;
	}
	return 0;
}

/* Block until everything written to the log is on disk. Returns 0 or -1. */
static int sync_log(void)
{
	if (fsync(log_fd) < 0) {
		fprintf(stderr, "sensortest: fsync %s: %s\n", log_path,
			strerror(errno));
		return -1;
	}
	return 0;
}

/* Record one sensor-state line. mono_ns is a CLOCK_MONOTONIC timestamp. */
static int record_state(const struct sensor *s, const char *tag,
			uint64_t mono_ns, int high)
{
	char wall[48];
	char line[LOG_LINE];
	int len;

	format_wall_time(wall, sizeof(wall));
	len = snprintf(line, sizeof(line),
		       "%s [mono %llu.%09llu] %-7s (GPIO%u) %-7s %s\n", wall,
		       (unsigned long long)(mono_ns / 1000000000ull),
		       (unsigned long long)(mono_ns % 1000000000ull),
		       s->name, s->offset, tag,
		       high ? s->high_label : s->low_label);
	return output_line(line, len);
}

/* Record a session marker such as "start" or "stop". */
static int record_marker(const char *what)
{
	char wall[48];
	char line[LOG_LINE];
	int len;

	format_wall_time(wall, sizeof(wall));
	len = snprintf(line, sizeof(line), "%s sensortest %s\n", wall, what);
	return output_line(line, len);
}

static uint64_t mono_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int install_signal_handlers(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0; /* no SA_RESTART: let poll() return EINTR */

	if (sigaction(SIGINT, &sa, NULL) < 0 ||
	    sigaction(SIGTERM, &sa, NULL) < 0) {
		perror("sigaction");
		return -1;
	}
	return 0;
}

/*
 * Add every sensor in the table to line_cfg. One settings object is
 * reused: add_line_settings copies it, so changing the bias afterwards
 * does not affect lines already added.
 */
static int add_sensor_lines(struct gpiod_line_config *line_cfg,
			    struct gpiod_line_settings *settings)
{
	for (size_t i = 0; i < NUM_SENSORS; i++) {
		const struct sensor *s = &sensors[i];

		if (gpiod_line_settings_set_bias(settings, s->bias)) {
			perror("gpiod_line_settings_set_bias");
			return -1;
		}
		gpiod_line_settings_set_debounce_period_us(settings,
							   s->debounce_us);
		if (gpiod_line_config_add_line_settings(line_cfg, &s->offset, 1,
							settings)) {
			perror("gpiod_line_config_add_line_settings");
			return -1;
		}
	}
	return 0;
}

/* Record every pending edge event, then sync the log. Returns 0 or -1. */
static int drain_events(struct gpiod_line_request *req,
			struct gpiod_edge_event_buffer *buf)
{
	int n = gpiod_line_request_read_edge_events(req, buf, EVENT_BUF);

	if (n < 0) {
		perror("gpiod_line_request_read_edge_events");
		return -1;
	}
	for (int i = 0; i < n; i++) {
		struct gpiod_edge_event *ev =
			gpiod_edge_event_buffer_get_event(buf, (unsigned long)i);
		unsigned int offset = gpiod_edge_event_get_line_offset(ev);
		const struct sensor *s = sensor_by_offset(offset);
		int rising = gpiod_edge_event_get_event_type(ev) ==
			     GPIOD_EDGE_EVENT_RISING_EDGE;

		if (!s) {
			/* Cannot happen: we only requested table lines. */
			fprintf(stderr, "event on unexpected line %u\n", offset);
			continue;
		}
		if (record_state(s, rising ? "rising" : "falling",
				 gpiod_edge_event_get_timestamp_ns(ev),
				 rising) < 0)
			return -1;
	}
	return sync_log(); /* one fsync per batch, not per line */
}

/* Record the current level of every sensor. Returns 0 or -1 on error. */
static int record_initial_states(struct gpiod_line_request *req)
{
	uint64_t now = mono_now_ns();

	for (size_t i = 0; i < NUM_SENSORS; i++) {
		const struct sensor *s = &sensors[i];
		enum gpiod_line_value val =
			gpiod_line_request_get_value(req, s->offset);

		if (val == GPIOD_LINE_VALUE_ERROR) {
			perror("gpiod_line_request_get_value");
			return -1;
		}
		if (record_state(s, "initial", now,
				 val == GPIOD_LINE_VALUE_ACTIVE) < 0)
			return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct gpiod_chip *chip = NULL;
	struct gpiod_line_settings *settings = NULL;
	struct gpiod_line_config *line_cfg = NULL;
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *req = NULL;
	struct gpiod_edge_event_buffer *evbuf = NULL;
	int ret = EXIT_FAILURE;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [logfile]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (argc == 2)
		log_path = argv[1];

	if (install_signal_handlers() < 0)
		return EXIT_FAILURE;

	/*
	 * Append so earlier runs are kept. 0600: the event log reveals when
	 * someone is home, so only the owner may read it. The mode applies
	 * only when the file is created; an existing file keeps its mode.
	 */
	log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
		      0600);
	if (log_fd < 0) {
		fprintf(stderr, "sensortest: open %s: %s\n", log_path,
			strerror(errno));
		goto out;
	}

	chip = gpiod_chip_open(CHIP_PATH);
	if (!chip) {
		perror("gpiod_chip_open " CHIP_PATH);
		goto out;
	}

	/*
	 * Settings shared by every line: input, both edges, monotonic
	 * timestamps. Bias and debounce are set per sensor in add_sensor_lines.
	 */
	settings = gpiod_line_settings_new();
	if (!settings) {
		perror("gpiod_line_settings_new");
		goto out;
	}
	if (gpiod_line_settings_set_direction(settings,
					      GPIOD_LINE_DIRECTION_INPUT) ||
	    gpiod_line_settings_set_edge_detection(settings,
						   GPIOD_LINE_EDGE_BOTH) ||
	    gpiod_line_settings_set_event_clock(settings,
						GPIOD_LINE_CLOCK_MONOTONIC)) {
		perror("gpiod_line_settings_set_*");
		goto out;
	}

	line_cfg = gpiod_line_config_new();
	if (!line_cfg) {
		perror("gpiod_line_config_new");
		goto out;
	}
	if (add_sensor_lines(line_cfg, settings) < 0)
		goto out;

	req_cfg = gpiod_request_config_new();
	if (!req_cfg) {
		perror("gpiod_request_config_new");
		goto out;
	}
	gpiod_request_config_set_consumer(req_cfg, CONSUMER);

	req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
	if (!req) {
		perror("gpiod_chip_request_lines (line busy or no permission?)");
		goto out;
	}

	evbuf = gpiod_edge_event_buffer_new(EVENT_BUF);
	if (!evbuf) {
		perror("gpiod_edge_event_buffer_new");
		goto out;
	}

	printf("sensortest: watching %zu lines on %s, logging to %s. "
	       "Ctrl+C to stop.\n", NUM_SENSORS, CHIP_PATH, log_path);
	/* Initial states, read after the request so no edge is missed. */
	if (record_marker("start") < 0 || record_initial_states(req) < 0 ||
	    sync_log() < 0)
		goto out;

	struct pollfd pfd = {
		.fd = gpiod_line_request_get_fd(req),
		.events = POLLIN,
	};

	while (!stop_requested) {
		int pr = poll(&pfd, 1, POLL_MS);

		if (pr < 0) {
			if (errno == EINTR)
				continue; /* signal: loop re-checks the flag */
			perror("poll");
			goto out;
		}
		if (pr == 0)
			continue; /* timeout, nothing happened */
		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fprintf(stderr, "poll: error on line request fd\n");
			goto out;
		}
		if (drain_events(req, evbuf) < 0)
			goto out;
	}

	printf("sensortest: stop requested, releasing GPIO lines\n");
	if (record_marker("stop") < 0 || sync_log() < 0)
		goto out;
	ret = EXIT_SUCCESS;

out:
	/* Free in reverse order of creation; all are safe to skip if NULL. */
	if (evbuf)
		gpiod_edge_event_buffer_free(evbuf);
	if (req)
		gpiod_line_request_release(req);
	if (req_cfg)
		gpiod_request_config_free(req_cfg);
	if (line_cfg)
		gpiod_line_config_free(line_cfg);
	if (settings)
		gpiod_line_settings_free(settings);
	if (chip)
		gpiod_chip_close(chip);
	if (log_fd >= 0)
		close(log_fd); /* data was already fsync'd on success */
	return ret;
}
