/*
 * beamtest.c - standalone IR break-beam test (libgpiod v2).
 *
 * Watches one GPIO input line and prints the beam state:
 *   HIGH / rising edge  -> "beam clear"
 *   LOW  / falling edge -> "beam blocked"
 *
 * The line is requested strictly as an input with bias disabled, because
 * the receiver signal arrives through an external 100k/100k voltage
 * divider and an internal pull resistor would shift that divider voltage.
 * No line is ever configured as an output.
 *
 * Stop with Ctrl+C (SIGINT) or SIGTERM; all resources are released.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <gpiod.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHIP_PATH   "/dev/gpiochip0"  /* pinctrl-rp1 on the Pi 5 */
#define BEAM_LINE   17u               /* BCM GPIO17 = physical pin 11 */
#define CONSUMER    "beamtest"
#define EVENT_BUF   16                /* edge events read per wakeup */
#define POLL_MS     500               /* max wait before re-checking stop flag */

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int signo)
{
	(void)signo;
	stop_requested = 1;
}

/* Print local wall-clock time, e.g. "2026-10-14 09:30:01.123". */
static void print_wall_time(void)
{
	struct timespec ts;
	struct tm tm;
	char buf[32];

	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &tm);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
	printf("%s.%03ld", buf, ts.tv_nsec / 1000000L);
}

/* Print one beam-state line. mono_ns is a CLOCK_MONOTONIC timestamp. */
static void print_state(const char *tag, uint64_t mono_ns, int clear)
{
	print_wall_time();
	printf(" [mono %llu.%09llu] %-7s beam %s\n",
	       (unsigned long long)(mono_ns / 1000000000ull),
	       (unsigned long long)(mono_ns % 1000000000ull),
	       tag, clear ? "clear" : "blocked");
	fflush(stdout);
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

/* Read and print every pending edge event. Returns 0 or -1 on error. */
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
		int rising = gpiod_edge_event_get_event_type(ev) ==
			     GPIOD_EDGE_EVENT_RISING_EDGE;

		print_state(rising ? "rising" : "falling",
			    gpiod_edge_event_get_timestamp_ns(ev), rising);
	}
	return 0;
}

int main(void)
{
	const unsigned int offset = BEAM_LINE;
	struct gpiod_chip *chip = NULL;
	struct gpiod_line_settings *settings = NULL;
	struct gpiod_line_config *line_cfg = NULL;
	struct gpiod_request_config *req_cfg = NULL;
	struct gpiod_line_request *req = NULL;
	struct gpiod_edge_event_buffer *evbuf = NULL;
	enum gpiod_line_value val;
	int ret = EXIT_FAILURE;

	if (install_signal_handlers() < 0)
		return EXIT_FAILURE;

	chip = gpiod_chip_open(CHIP_PATH);
	if (!chip) {
		perror("gpiod_chip_open " CHIP_PATH);
		goto out;
	}

	/* Input only, no pull bias, report both edges, monotonic timestamps. */
	settings = gpiod_line_settings_new();
	if (!settings) {
		perror("gpiod_line_settings_new");
		goto out;
	}
	if (gpiod_line_settings_set_direction(settings,
					      GPIOD_LINE_DIRECTION_INPUT) ||
	    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED) ||
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
	if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1,
						settings)) {
		perror("gpiod_line_config_add_line_settings");
		goto out;
	}

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

	/* Initial state, read after the request so no edge is missed. */
	val = gpiod_line_request_get_value(req, offset);
	if (val == GPIOD_LINE_VALUE_ERROR) {
		perror("gpiod_line_request_get_value");
		goto out;
	}
	printf("beamtest: watching %s line %u (GPIO17, pin 11). "
	       "Ctrl+C to stop.\n", CHIP_PATH, offset);
	print_state("initial", mono_now_ns(), val == GPIOD_LINE_VALUE_ACTIVE);

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

	printf("beamtest: stop requested, releasing GPIO line\n");
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
	return ret;
}
