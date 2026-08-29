#ifndef FLYDIGI_DECOY_SERVER_H
#define FLYDIGI_DECOY_SERVER_H

/* Register SSAP callbacks. Call BEFORE enable_sle(). */
void decoy_server_early_init(void);

/* Register server id and the mirrored attribute table.
 * Call from the SLE-enable callback. */
void decoy_services_add(void);

/* Stop the notify stream on disconnection. */
void decoy_on_disconnected(void);

/* Register low-latency EM callbacks. Call BEFORE enable_sle(). */
void decoy_low_latency_init(void);

/* Mark connection/pairing state for RX/TX logging. Called from conn callbacks. */
void decoy_mark_connected(void);
void decoy_mark_pair_complete(void);

#endif /* FLYDIGI_DECOY_SERVER_H */
