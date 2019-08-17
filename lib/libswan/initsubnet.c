/*
 * initialize subnet structure
 * Copyright (C) 2000, 2002  Henry Spencer.
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

#include "ip_subnet.h"
#include "ip_info.h"

/*
 * initsubnet - initialize ip_subnet from address and count
 *
 * The only hard part is checking for host-part bits turned on.
 */
err_t	/* NULL for success, else string literal */
initsubnet(addr, count, clash, dst)
const ip_address * addr;
int count;
int clash;	/* '0' zero host-part bits, 'x' die on them */
ip_subnet *dst;
{
	unsigned char *p;
	int n;
	int c;
	unsigned m;
	int die;

	dst->addr = *addr;
	chunk_t addr_chunk = address_as_chunk(&dst->addr);
	n = addr_chunk.len;
	p = addr_chunk.ptr; /* cast void* */
	if (n == 0)
		return "unknown address family";

	switch (clash) {
	case '0':
		die = 0;
		break;
	case 'x':
		die = 1;
		break;
	default:
		return "unknown clash-control value in initsubnet";
	}

	c = count / 8;
	if (c > n)
		return "impossible mask count";

	p += c;
	n -= c;

	m = 0xff;
	c = count % 8;
	if (n > 0 && c != 0)	/* partial byte */
		m >>= c;
	for (; n > 0; n--) {
		if ((*p & m) != 0) {
			if (die)
				return "improper subnet, host-part bits on";

			*p &= ~m;
		}
		m = 0xff;
		p++;
	}

	dst->maskbits = count;
	return NULL;
}

/*
 * addrtosubnet - initialize ip_subnet from a single address
 */
err_t	/* NULL for success, else string literal */
addrtosubnet(addr, dst)
const ip_address *addr;
ip_subnet *dst;
{
	dst->addr = *addr;
	const struct ip_info *afi = address_type(addr);
	if (afi == NULL) {
		/* actually AF_UNSPEC */
		return "unknown address family";
	}

	dst->maskbits = afi->mask_cnt;
	return NULL;
}
