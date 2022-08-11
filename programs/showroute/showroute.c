/* routecheck, for libreswan
 *
 * Copyright (C) 2022 Andrew Cagney
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/lgpl-2.1.txt>.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
 * License for more details.
 */

#include "lswtool.h"
#include "lswlog.h"
#include "addr_lookup.h"
#include "ip_address.h"
#include "stdlib.h"

#include "getopt.h"

int main(int argc, char **argv)
{
	struct logger *logger = tool_init_log(argv[0]);

	if (argc == 1) {
		llog(WHACK_STREAM|NO_PREFIX, logger, "Usage:");
		llog(WHACK_STREAM|NO_PREFIX, logger, "  ipsec showroute [--source|--gateway|--destination] <destination>");
		llog(WHACK_STREAM|NO_PREFIX, logger, "prints:");
		llog(WHACK_STREAM|NO_PREFIX, logger, "  <source-address> <gateway-address> <destination-address>");
		llog(WHACK_STREAM|NO_PREFIX, logger, "for the given <destination>");
		exit(1);
	}

	int show_source = false;
	int show_gateway = false;
	int show_destination = false;

	const struct option options[] = {
		{ "source", no_argument, &show_source, true, },
		{ "gateway", no_argument, &show_gateway, true, },
		{ "destination", no_argument, &show_destination, true, },
		{0},
	};

	while (true) {
		opterr = 0;
		int option_index;
		int c = getopt_long(argc, argv, "", options, &option_index);
		if (c == -1) {
			break;
		}
		if (c == '?') {
			llog(ERROR_STREAM, logger, "invalid option: %s", argv[optind]);
			exit(1);
		}
	}

	if (optind == argc) {
		llog(ERROR_STREAM, logger, "missing destination");
		exit(1);
	}

	if (optind + 1 < argc) {
		llog(ERROR_STREAM, logger, "extra parameter %s", argv[optind + 1]);
		exit(1);
	}

	if (!show_source && !show_gateway && !show_destination) {
		show_source = show_gateway = show_destination = true;
	}

	ip_address dst;
	err_t e = ttoaddress_dns(shunk1(argv[optind]), NULL, &dst);
	if (e != NULL) {
		llog(WHACK_STREAM, logger, "%s: %s", argv[1], e);
		exit(1);
	}

	struct ip_route route;
	switch (get_route(dst, &route, logger)) {
	case ROUTE_SUCCESS:
	{
		LLOG_JAMBUF(WHACK_STREAM|NO_PREFIX, logger, buf) {
			const char *sep = "";
			if (show_source) {
				jam_string(buf, sep); sep = " ";
				jam_address(buf, &route.source);
			}
			if (show_gateway) {
				jam_string(buf, sep); sep = " ";
				jam_address(buf, &route.gateway);
			}
			if (show_destination) {
				jam_string(buf, sep); sep = " ";
				jam_address(buf, &dst);
			}
		}
		exit(0);
	}
	case ROUTE_GATEWAY_FAILED:
	{
		address_buf ab;
		llog(ERROR_STREAM, logger, "%s: gateway failed",
		     str_address(&dst, &ab));
		exit(1);
	}
	case ROUTE_SOURCE_FAILED:
	{
		address_buf ab;
		llog(ERROR_STREAM, logger, "%s: source failed",
		     str_address(&dst, &ab));
		exit(1);
	}
	case ROUTE_FATAL:
	{
		address_buf ab;
		llog(ERROR_STREAM, logger, "%s: fatal",
		     str_address(&dst, &ab));
		exit(1);
	}
	}

	exit(1);
}
