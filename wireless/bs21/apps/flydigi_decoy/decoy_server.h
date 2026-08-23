#ifndef FLYDIGI_DECOY_SERVER_H
#define FLYDIGI_DECOY_SERVER_H

/* Register SSAP callbacks. Call BEFORE enable_sle(). */
void decoy_server_early_init(void);

/* Register server id and the mirrored attribute table.
 * Call from the SLE-enable callback. */
void decoy_services_add(void);

#endif /* FLYDIGI_DECOY_SERVER_H */
