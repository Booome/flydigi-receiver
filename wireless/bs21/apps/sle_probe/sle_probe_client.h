#ifndef SLE_PROBE_CLIENT_H
#define SLE_PROBE_CLIENT_H

void probe_init(void);

extern uint16_t g_conn_id;
extern uint16_t g_write_hdls[8];
extern uint8_t g_write_cnt;

#endif /* SLE_PROBE_CLIENT_H */
