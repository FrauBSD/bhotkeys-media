/*
 * Copyright (c) 2026 Devin Teske <dteske@FreeBSD.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Drive the active MPRIS player (Brave, mpv, ...) over the session
 * bus: play-pause, rewind, forward.  Wired to the media keys
 * by bhotkeys.
 *
 * Player selection: every bus name under org.mpris.MediaPlayer2.* is
 * a candidate; the first one whose PlaybackStatus is "Playing" wins,
 * else the first candidate.  Chromium maps Seek() onto the page's
 * MediaSession seek handlers, which is how Netflix rewind/forward
 * works.
 *
 * When no player is found on the default session bus (or there is no
 * session bus at all, as under a bus-less window manager where the
 * brave wrapper falls back to a private dbus-run-session bus), hunt
 * for private buses by scavenging DBUS_SESSION_BUS_ADDRESS from the
 * environments of our own processes via libkvm.
 *
 * Usage: mpris-control play-pause | rewind [sec] | forward [sec]
 * Default seek is 10 seconds.  Rebuild with make.
 */

#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <dbus/dbus.h>
#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MPRIS_PREFIX	"org.mpris.MediaPlayer2."
#define MPRIS_PATH	"/org/mpris/MediaPlayer2"
#define MPRIS_PLAYER	"org.mpris.MediaPlayer2.Player"

#define MAX_PLAYERS	16
#define MAX_ADDRS	8
#define ADDR_MAX	256
#define CALL_TIMEOUT_MS	2000

static char *players[MAX_PLAYERS];
static int nplayers;

static void
reset_players(void)
{

	while (nplayers > 0)
		free(players[--nplayers]);
}

static void
list_players(DBusConnection *conn)
{
	DBusMessage *msg, *reply;
	DBusMessageIter iter, elem;
	const char *name;

	msg = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
	    DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, "ListNames");
	if (msg == NULL)
		return;
	reply = dbus_connection_send_with_reply_and_block(conn, msg,
	    CALL_TIMEOUT_MS, NULL);
	dbus_message_unref(msg);
	if (reply == NULL)
		return;
	if (dbus_message_iter_init(reply, &iter) &&
	    dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
		dbus_message_iter_recurse(&iter, &elem);
		while (dbus_message_iter_get_arg_type(&elem) ==
		    DBUS_TYPE_STRING && nplayers < MAX_PLAYERS) {
			dbus_message_iter_get_basic(&elem, &name);
			if (strncmp(name, MPRIS_PREFIX,
			    sizeof(MPRIS_PREFIX) - 1) == 0)
				players[nplayers++] = strdup(name);
			dbus_message_iter_next(&elem);
		}
	}
	dbus_message_unref(reply);
}

static int
is_playing(DBusConnection *conn, const char *player)
{
	DBusMessage *msg, *reply;
	DBusMessageIter iter, var;
	const char *iface = MPRIS_PLAYER, *prop = "PlaybackStatus";
	const char *status;
	int playing = 0;

	msg = dbus_message_new_method_call(player, MPRIS_PATH,
	    DBUS_INTERFACE_PROPERTIES, "Get");
	if (msg == NULL)
		return (0);
	dbus_message_append_args(msg,
	    DBUS_TYPE_STRING, &iface,
	    DBUS_TYPE_STRING, &prop,
	    DBUS_TYPE_INVALID);
	reply = dbus_connection_send_with_reply_and_block(conn, msg,
	    CALL_TIMEOUT_MS, NULL);
	dbus_message_unref(msg);
	if (reply == NULL)
		return (0);
	if (dbus_message_iter_init(reply, &iter) &&
	    dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT) {
		dbus_message_iter_recurse(&iter, &var);
		if (dbus_message_iter_get_arg_type(&var) ==
		    DBUS_TYPE_STRING) {
			dbus_message_iter_get_basic(&var, &status);
			playing = strcmp(status, "Playing") == 0;
		}
	}
	dbus_message_unref(reply);
	return (playing);
}

static const char *
pick_player(DBusConnection *conn)
{
	int i;

	if (nplayers == 0)
		return (NULL);
	for (i = 0; i < nplayers; i++)
		if (is_playing(conn, players[i]))
			return (players[i]);
	return (players[0]);
}

static int
call_player(DBusConnection *conn, const char *player, const char *member,
    int64_t *offset)
{
	DBusMessage *msg, *reply;
	dbus_int64_t us;

	msg = dbus_message_new_method_call(player, MPRIS_PATH,
	    MPRIS_PLAYER, member);
	if (msg == NULL)
		return (1);
	if (offset != NULL) {
		us = *offset;
		dbus_message_append_args(msg,
		    DBUS_TYPE_INT64, &us, DBUS_TYPE_INVALID);
	}
	reply = dbus_connection_send_with_reply_and_block(conn, msg,
	    CALL_TIMEOUT_MS, NULL);
	dbus_message_unref(msg);
	if (reply == NULL)
		return (1);
	dbus_message_unref(reply);
	return (0);
}

/*
 * Collect unique DBUS_SESSION_BUS_ADDRESS values from the
 * environments of this user's processes.
 */
static int
scavenge_addrs(char addrs[][ADDR_MAX], int max)
{
	kvm_t *kd;
	struct kinfo_proc *procs;
	char **env, errbuf[_POSIX2_LINE_MAX];
	const char *val;
	int cnt, i, j, k, n = 0;

	kd = kvm_openfiles(NULL, _PATH_DEVNULL, NULL, O_RDONLY, errbuf);
	if (kd == NULL)
		return (0);
	procs = kvm_getprocs(kd, KERN_PROC_UID, getuid(), &cnt);
	for (i = 0; procs != NULL && i < cnt && n < max; i++) {
		env = kvm_getenvv(kd, &procs[i], 0);
		if (env == NULL)
			continue;
		for (j = 0; env[j] != NULL; j++) {
			if (strncmp(env[j], "DBUS_SESSION_BUS_ADDRESS=",
			    25) != 0)
				continue;
			val = env[j] + 25;
			if (*val == '\0' || strlen(val) >= ADDR_MAX)
				continue;
			for (k = 0; k < n; k++)
				if (strcmp(addrs[k], val) == 0)
					break;
			if (k == n)
				strlcpy(addrs[n++], val, ADDR_MAX);
			break;
		}
	}
	kvm_close(kd);
	return (n);
}

/*
 * Open a private connection to a scavenged bus address and look for
 * a player on it.  Returns the connection with *playerp set, or NULL.
 */
static DBusConnection *
try_private_bus(const char *addr, const char **playerp)
{
	DBusConnection *conn;

	conn = dbus_connection_open_private(addr, NULL);
	if (conn == NULL)
		return (NULL);
	if (!dbus_bus_register(conn, NULL)) {
		dbus_connection_close(conn);
		dbus_connection_unref(conn);
		return (NULL);
	}
	reset_players();
	list_players(conn);
	*playerp = pick_player(conn);
	if (*playerp != NULL)
		return (conn);
	dbus_connection_close(conn);
	dbus_connection_unref(conn);
	return (NULL);
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mpris-control play-pause | rewind [sec] | forward [sec]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	DBusConnection *conn;
	const char *player;
	int64_t offset;
	long sec = 10;

	if (argc < 2)
		usage();
	if (argc > 2) {
		sec = strtol(argv[2], NULL, 10);
		if (sec <= 0)
			usage();
	}

	player = NULL;
	conn = dbus_bus_get(DBUS_BUS_SESSION, NULL);
	if (conn != NULL) {
		list_players(conn);
		player = pick_player(conn);
	}
	if (player == NULL) {
		char addrs[MAX_ADDRS][ADDR_MAX];
		const char *sess = getenv("DBUS_SESSION_BUS_ADDRESS");
		int i, naddrs;

		naddrs = scavenge_addrs(addrs, MAX_ADDRS);
		for (i = 0; i < naddrs; i++) {
			if (sess != NULL && strcmp(addrs[i], sess) == 0)
				continue;
			conn = try_private_bus(addrs[i], &player);
			if (conn != NULL)
				break;
		}
	}
	if (player == NULL) {
		fprintf(stderr, "mpris-control: no MPRIS player found\n");
		return (1);
	}

	if (strcmp(argv[1], "play-pause") == 0)
		return (call_player(conn, player, "PlayPause", NULL));
	if (strcmp(argv[1], "rewind") == 0) {
		offset = -(int64_t)sec * 1000000;
		return (call_player(conn, player, "Seek", &offset));
	}
	if (strcmp(argv[1], "forward") == 0) {
		offset = (int64_t)sec * 1000000;
		return (call_player(conn, player, "Seek", &offset));
	}
	usage();
	return (2);
}
