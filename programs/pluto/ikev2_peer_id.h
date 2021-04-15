/* IKEv2 peer id decoding, for libreswan
 *
 * Copyright (C) 2021  Andrew Cagney

 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#ifndef IKEV2_PEER_ID_H
#define IKEV2_PEER_ID_H

#include <stdbool.h>

struct msg_digest;

bool ikev2_decode_peer_id(struct msg_digest *md);

#endif
