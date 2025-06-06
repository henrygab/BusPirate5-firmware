void tx_fifo_init(void);
void tx_fifo_service(void);
void tx_fifo_put(char* c);
void tx_fifo_try_put(char* c);
void tx_sb_start(uint32_t valid_characters_in_status_bar);
void bin_tx_fifo_put(const char c);
void bin_tx_fifo_service(void);
bool bin_tx_not_empty(void);
bool bin_tx_fifo_try_get(char* c);

extern char tx_sb_buf[1024];

// extern queue_t sample_fifo;