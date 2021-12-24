/* IKEv1 peer ID, for libreswan
 *
 * Copyright (C) 1997 Angelos D. Keromytis.
 * Copyright (C) 1998-2010,2013-2016 D. Hugh Redelmeier <hugh@mimosa.com>
 * Copyright (C) 2003-2008 Michael Richardson <mcr@xelerance.com>
 * Copyright (C) 2008-2009 David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2008-2010 Paul Wouters <paul@xelerance.com>
 * Copyright (C) 2011 Avesh Agarwal <avagarwa@redhat.com>
 * Copyright (C) 2008 Hiren Joshi <joshihirenn@gmail.com>
 * Copyright (C) 2009 Anthony Tong <atong@TrustedCS.com>
 * Copyright (C) 2012-2019 Paul Wouters <pwouters@redhat.com>
 * Copyright (C) 2013 Wolfgang Nothdurft <wolfgang@linogate.de>
 * Copyright (C) 2019-2021 Andrew Cagney <cagney@gnu.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include "defs.h"
#include "demux.h"
#include "state.h"
#include "connections.h"
#include "ikev1_peer_id.h"
#include "log.h"
#include "unpack.h"
#include "pluto_x509.h"
#include "ikev1_xauth.h"
#include "keys.h"
#include "ike_alg_hash.h"
#include "secrets.h"

static bool decode_peer_id(struct state *st, struct msg_digest *md, struct id *peer);

bool ikev1_decode_peer_id_initiator(struct state *st, struct msg_digest *md)
{
	struct id peer[1]; /* hack for pointer */
	if (!decode_peer_id(st, md, peer)) {
		/* already logged */
		return false;
	}

	struct connection *c = st->st_connection;

	/*
	 * XXX: this logic seems to overlap a tighter check for
	 * ID_FROMCERT below.
	 */
	if (c->spd.that.id.kind == ID_FROMCERT) {
		/* breaks API, connection modified by %fromcert */
		replace_connection_that_id(c, peer);
	}

	/* check for certificates */

	pexpect(st->st_v1_aggr_mode_responder_found_peer_id == false);
	bool peer_alt_id = false;
	if (md->chain[ISAKMP_NEXT_CERT] == NULL) {
		dbg("Peer ID has no certs");
	} else if (st->st_remote_certs.verified == NULL) {
		dbg("Peer ID has no verified certs");
	} else {
		struct certs *certs = st->st_remote_certs.verified;
		/* end cert is at the front */
		CERTCertificate *end_cert = certs->cert;
		log_state(RC_LOG, st, "certificate verified OK: %s",
			  end_cert->subjectName);

		if (LIN(POLICY_ALLOW_NO_SAN, c->policy)) {
			dbg("SAN ID matching skipped due to policy (require-id-on-certificate=no)");
			/* XXX: !main-mode-responder: that.ID unchanged */
		} else {
			struct id cert_id = empty_id;
			/* XXX: !main-mode-responder: can't change ID */
			const struct id remote_id = c->spd.that.id;
			diag_t d = match_end_cert_id(certs, &remote_id, &cert_id);
			if (d != NULL) {
				/* cannot switch connection so fail */
				dbg("SAN ID did not match");
				llog_diag(RC_LOG_SERIOUS, st->st_logger, &d, "%s", "");
				log_state(RC_LOG, st,
					  "X509: CERT payload does not match connection ID");
				return false;
			}
			dbg("SAN ID matched, updating that.cert");
			/* XXX: !main-mode-responder: ???? */
			if (cert_id.kind != ID_NONE) {
				replace_connection_that_id(c, &cert_id);
			}
		}
		peer_alt_id = true;
		if (c->spd.that.cert.nss_cert != NULL) {
			CERT_DestroyCertificate(c->spd.that.cert.nss_cert);
		}
		c->spd.that.cert.nss_cert = CERT_DupCertificate(certs->cert);
	}

	/*
	 * Now that we've decoded the ID payload, let's see if we
	 * need to switch connections.
	 * Aggressive mode cannot switch connections.
	 * We must not switch horses if we initiated:
	 * - if the initiation was explicit, we'd be ignoring user's intent
	 * - if opportunistic, we'll lose our HOLD info
	 */

	if (!peer_alt_id &&
	    !same_id(&c->spd.that.id, peer) &&
	    c->spd.that.id.kind != ID_FROMCERT) {
		id_buf expect;
		id_buf found;

		log_state(RC_LOG_SERIOUS, st,
			  "we require IKEv1 peer to have ID '%s', but peer declares '%s'",
			  str_id(&c->spd.that.id, &expect),
			  str_id(peer, &found));
		return false;
	} else if (c->spd.that.id.kind == ID_FROMCERT) {
		/*
		 * XXX: this logic seems to overlap a weaker check for
		 * ID_FROMCERT above.
		 */
		if (peer->kind != ID_DER_ASN1_DN) {
			log_state(RC_LOG_SERIOUS, st,
				  "peer ID is not a certificate type");
			return false;
		}
		replace_connection_that_id(c, peer);
	}

	return true;
}

bool ikev1_decode_peer_id_aggr_mode_responder(struct state *st,
					      struct msg_digest *md)
{
	struct id peer;
	if (!decode_peer_id(st, md, &peer)) {
		/* already logged */
		return false;
	}

	struct connection *c = st->st_connection;
	if (c->spd.that.id.kind == ID_FROMCERT) {
		/* breaks API, connection modified by %fromcert */
		replace_connection_that_id(c, &peer);
	}

	/* check for certificates */

	pexpect(st->st_v1_aggr_mode_responder_found_peer_id == false);
	bool peer_alt_id = false;
	if (md->chain[ISAKMP_NEXT_CERT] == NULL) {
		dbg("Peer ID has no certs");
	} else if (st->st_remote_certs.verified == NULL) {
		dbg("Peer ID has no verified certs");
	} else {
		struct certs *certs = st->st_remote_certs.verified;
		/* end cert is at the front */
		CERTCertificate *end_cert = certs->cert;
		log_state(RC_LOG, st, "certificate verified OK: %s",
			  end_cert->subjectName);

		if (LIN(POLICY_ALLOW_NO_SAN, c->policy)) {
			dbg("SAN ID matching skipped due to policy (require-id-on-certificate=no)");
			/* XXX: !main-mode-responder: that.ID unchanged */
		} else {
			struct id cert_id = empty_id;
			/* XXX: !main-mode-responder: can't change ID */
			const struct id remote_id = c->spd.that.id;
			diag_t d = match_end_cert_id(certs, &remote_id, &cert_id);
			if (d != NULL) {
				/* cannot switch connection so fail */
				dbg("SAN ID did not match");
				llog_diag(RC_LOG_SERIOUS, st->st_logger, &d, "%s", "");
				log_state(RC_LOG, st,
					  "X509: CERT payload does not match connection ID");
				return false;
			}
			dbg("SAN ID matched, updating that.cert");
			/* XXX: !main-mode-responder: ???? */
			if (cert_id.kind != ID_NONE) {
				replace_connection_that_id(c, &cert_id);
			}
		}
		peer_alt_id = true;
		if (c->spd.that.cert.nss_cert != NULL) {
			CERT_DestroyCertificate(c->spd.that.cert.nss_cert);
		}
		c->spd.that.cert.nss_cert = CERT_DupCertificate(certs->cert);
	}

	st->st_v1_aggr_mode_responder_found_peer_id = peer_alt_id;
	return true;
}

/*
 * note: may change which connection is referenced by md->v1_st->st_connection.
 * But only if we are a Main Mode Responder.
 */

bool ikev1_decode_peer_id_main_mode_responder(struct state *st, struct msg_digest *md)
{
	struct id peer[1]; /* pointer hack */
	if (!decode_peer_id(st, md, peer)) {
		/* already logged */
		return false;
	}

	/*
	 * Now that we've decoded the ID payload, let's see if we
	 * need to switch connections.
	 * Aggressive mode cannot switch connections.
	 * We must not switch horses if we initiated:
	 * - if the initiation was explicit, we'd be ignoring user's intent
	 * - if opportunistic, we'll lose our HOLD info
	 */

	/* Main Mode Responder */
	uint16_t auth = xauth_calcbaseauth(st->st_oakley.auth);

	/*
	 * Translate the IKEv1 policy onto IKEv2(?) auth enum.
	 * Saves duplicating the checks for v1 and v2, and the
	 * v1 policy is a subset of the v2 policy.
	 */

	lset_t this_authbys;
	switch (auth) {
	case OAKLEY_PRESHARED_KEY:
		this_authbys = LELEM(AUTHBY_PSK);
		break;
	case OAKLEY_RSA_SIG:
		this_authbys = LELEM(AUTHBY_RSASIG);
		break;
		/* Not implemented */
	case OAKLEY_DSS_SIG:
	case OAKLEY_RSA_ENC:
	case OAKLEY_RSA_REVISED_MODE:
	case OAKLEY_ECDSA_P256:
	case OAKLEY_ECDSA_P384:
	case OAKLEY_ECDSA_P521:
	default:
		dbg("ikev1 ike_decode_peer_id bad_case due to not supported policy");
		return false;
	}

	bool get_id_from_cert;
	struct connection *r =
		refine_host_connection_on_responder(st, this_authbys, peer,
						    /* IKEv1 does not support 'you Tarzan, me Jane' */NULL,
						    &get_id_from_cert);

	/* check for certificates */

	pexpect(st->st_v1_aggr_mode_responder_found_peer_id == false);
	bool peer_alt_id = false;
	if (md->chain[ISAKMP_NEXT_CERT] == NULL) {
		dbg("Peer ID has no certs");
	} else if (st->st_remote_certs.verified == NULL) {
		dbg("Peer ID has no verified certs");
	} else {
		/* end cert is at the front */
		struct certs *certs = st->st_remote_certs.verified;
		CERTCertificate *end_cert = certs->cert;
		log_state(RC_LOG, st, "certificate verified OK: %s",
			  end_cert->subjectName);

		/* going to switch? */
		struct connection *c = r != NULL ? r : st->st_connection;

		if (LIN(POLICY_ALLOW_NO_SAN, c->policy)) {
			dbg("SAN ID matching skipped due to policy (require-id-on-certificate=no)");
			/* XXX: main-mode-responder: that.ID updated */
			if (c->spd.that.id.kind == ID_FROMCERT) {
				/* breaks API, connection modified by %fromcert */
				replace_connection_that_id(c, peer);
			}
		} else {
			struct id remote_cert_id = empty_id;
			/* XXX: presumably PEER contain's the cert's ID */
			/* XXX: main-mode-responder: can change ID */
			const struct id *remote_id = (c->spd.that.id.kind == ID_FROMCERT ||
						      get_id_from_cert ||
						      c->spd.that.has_id_wildcards) ? peer : &c->spd.that.id;
			diag_t d = match_end_cert_id(certs, remote_id, &remote_cert_id);
			if (d != NULL) {
				dbg("SAN ID did not match");
				/* already switched connection so fail */
				llog_diag(RC_LOG_SERIOUS, st->st_logger, &d, "%s", "");
				return false;
			}
			dbg("SAN ID matched, updating that.cert");
			/* XXX: main-mode-responder: update that.ID */
			if (remote_cert_id.kind != ID_NONE) {
				replace_connection_that_id(c, &remote_cert_id);
			} else if (c->spd.that.id.kind == ID_FROMCERT) {
				/* breaks API, connection modified by %fromcert */
				replace_connection_that_id(c, peer);
			}
		}
		peer_alt_id = true;
		if (c->spd.that.cert.nss_cert != NULL) {
			CERT_DestroyCertificate(c->spd.that.cert.nss_cert);
		}
		c->spd.that.cert.nss_cert = CERT_DupCertificate(certs->cert);
	}

	if (r == NULL) {
		id_buf buf;
		dbg("no more suitable connection for peer '%s'",
		    str_id(peer, &buf));
		/* can we continue with what we had? */
		struct connection *c = st->st_connection;
		if (!peer_alt_id &&
		    !same_id(&c->spd.that.id, peer) &&
		    c->spd.that.id.kind != ID_FROMCERT) {
			log_state(RC_LOG, md->v1_st, "Peer mismatch on first found connection and no better connection found");
			return false;
		}
		dbg("Peer ID matches and no better connection found - continuing with existing connection");
		r = c;
	}

	passert(r != NULL);
	dn_buf buf;
	dbg("offered CA: '%s'",
	    str_dn_or_null(r->spd.this.ca, "%none", &buf));

	if (r != st->st_connection) {
		/*
		 * We are changing st->st_connection!
		 * Our caller might be surprised!
		 */
		struct connection *c = st->st_connection;
		if (r->kind == CK_TEMPLATE || r->kind == CK_GROUP) {
			/* instantiate it, filling in peer's ID */
			r = rw_instantiate(r, &c->spd.that.host_addr,
					   NULL, peer);
		}
		connswitch_state_and_log(st, r);
	} else if (r->spd.that.has_id_wildcards) {
		replace_connection_that_id(r, peer);
		r->spd.that.has_id_wildcards = false;
	} else if (get_id_from_cert) {
		dbg("copying ID for get_id_from_cert");
		replace_connection_that_id(r, peer);
	}

	return true;
}

static bool decode_peer_id(struct state *st, struct msg_digest *md, struct id *peer)
{
	/* check for certificate requests */
	decode_v1_certificate_requests(st, md);

	const struct payload_digest *const id_pld = md->chain[ISAKMP_NEXT_ID];
	const struct isakmp_id *const id = &id_pld->payload.id;

	/*
	 * I think that RFC2407 (IPSEC DOI) 4.6.2 is confused.
	 * It talks about the protocol ID and Port fields of the ID
	 * Payload, but they don't exist as such in Phase 1.
	 * We use more appropriate names.
	 * isaid_doi_specific_a is in place of Protocol ID.
	 * isaid_doi_specific_b is in place of Port.
	 * Besides, there is no good reason for allowing these to be
	 * other than 0 in Phase 1.
	 */
	if (st->hidden_variables.st_nat_traversal != LEMPTY &&
	    id->isaid_doi_specific_a == IPPROTO_UDP &&
	    (id->isaid_doi_specific_b == 0 ||
	     id->isaid_doi_specific_b == NAT_IKE_UDP_PORT)) {
		dbg("protocol/port in Phase 1 ID Payload is %d/%d. accepted with port_floating NAT-T",
		    id->isaid_doi_specific_a, id->isaid_doi_specific_b);
	} else if (!(id->isaid_doi_specific_a == 0 &&
		     id->isaid_doi_specific_b == 0) &&
		   !(id->isaid_doi_specific_a == IPPROTO_UDP &&
		     id->isaid_doi_specific_b == IKE_UDP_PORT)) {
		log_state(RC_LOG_SERIOUS, st,
			  "protocol/port in Phase 1 ID Payload MUST be 0/0 or %d/%d but are %d/%d (attempting to continue)",
			  IPPROTO_UDP, IKE_UDP_PORT,
			  id->isaid_doi_specific_a,
			  id->isaid_doi_specific_b);
		/*
		 * We have turned this into a warning because of bugs
		 * in other vendors' products. Specifically CISCO
		 * VPN3000.
		 */
		/* return false; */
	}

	diag_t d = unpack_peer_id(id->isaid_idtype, peer, &id_pld->pbs);
	if (d != NULL) {
		llog_diag(RC_LOG, st->st_logger, &d, "%s", "");
		return false;
	}

	/*
	 * For interop with SoftRemote/aggressive mode we need to remember some
	 * things for checking the hash
	 */
	st->st_peeridentity_protocol = id->isaid_doi_specific_a;
	st->st_peeridentity_port = ntohs(id->isaid_doi_specific_b);

	id_buf buf;
	enum_buf b;
	log_state(RC_LOG, st, "Peer ID is %s: '%s'",
		  str_enum(&ike_id_type_names, id->isaid_idtype, &b),
		  str_id(peer, &buf));

	return true;
}

/*
 * Process the Main Mode ID Payload and the Authenticator
 * (Hash or Signature Payload).
 * XXX: This is used by aggressive mode too, move to ikev1.c ???
 */
stf_status oakley_auth(struct msg_digest *md, bool initiator)
{
	struct state *st = md->v1_st;
	stf_status r = STF_OK;

	/*
	 * Hash the ID Payload.
	 * main_mode_hash requires idpl->cur to be at end of payload
	 * so we temporarily set if so.
	 */
	struct crypt_mac hash;
	{
		pb_stream *idpl = &md->chain[ISAKMP_NEXT_ID]->pbs;
		uint8_t *old_cur = idpl->cur;

		idpl->cur = idpl->roof;
		/* authenticating other end, flip role! */
		hash = main_mode_hash(st, initiator ? SA_RESPONDER : SA_INITIATOR, idpl);
		idpl->cur = old_cur;
	}

	switch (st->st_oakley.auth) {
	case OAKLEY_PRESHARED_KEY:
	{
		pb_stream *const hash_pbs = &md->chain[ISAKMP_NEXT_HASH]->pbs;

		/*
		 * XXX: looks a lot like the hack CHECK_QUICK_HASH(),
		 * except this one doesn't return.  Strong indicator
		 * that CHECK_QUICK_HASH should be changed to a
		 * function and also not magically force caller to
		 * return.
		 */
		if (pbs_left(hash_pbs) != hash.len ||
			!memeq(hash_pbs->cur, hash.ptr, hash.len)) {
			if (DBGP(DBG_CRYPT)) {
				DBG_dump("received HASH:",
					 hash_pbs->cur, pbs_left(hash_pbs));
			}
			log_state(RC_LOG_SERIOUS, st,
				  "received Hash Payload does not match computed value");
			/* XXX Could send notification back */
			r = STF_FAIL + INVALID_HASH_INFORMATION;
		} else {
			dbg("received message HASH_%s data ok",
			    initiator ? "R" : "I" /*reverse*/);
		}
		break;
	}

	case OAKLEY_RSA_SIG:
	{
		shunk_t signature = pbs_in_left_as_shunk(&md->chain[ISAKMP_NEXT_SIG]->pbs);
		diag_t d = authsig_and_log_using_pubkey(ike_sa(st, HERE), &hash, signature,
							&ike_alg_hash_sha1, /*always*/
							&pubkey_type_rsa,
							authsig_using_RSA_pubkey);
		if (d != NULL) {
			llog_diag(RC_LOG_SERIOUS, st->st_logger, &d, "%s", "");
			dbg("received message SIG_%s data did not match computed value",
			    initiator ? "R" : "I" /*reverse*/);
			r = STF_FAIL + INVALID_KEY_INFORMATION;
		}
		break;
	}
	/* These are the only IKEv1 AUTH methods we support */
	default:
		bad_case(st->st_oakley.auth);
	}

	if (r == STF_OK)
		dbg("authentication succeeded");
	return r;
}
