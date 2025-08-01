void hwspi_init(uint8_t data_bits, uint8_t cpol, uint8_t cpha);
void hwspi_deinit(void);
void hwspi_select(void);
void hwspi_deselect(void);
void hwspi_write(uint32_t data);
void hwspi_write_n(uint8_t* data, uint8_t count);
void hwspi_write_32(const uint32_t data, uint8_t count);
uint32_t hwspi_read(void);
void hwspi_read_n(uint8_t* data, uint32_t count);
uint32_t hwspi_write_read(uint8_t data);
void hwspi_write_read_cs(uint8_t *write_data, uint32_t write_count, uint8_t *read_data, uint32_t read_count);
// Add to your hwspi.h or hwspi.c file
typedef enum {
    SPI_FRF_MOTOROLA = 0,
    SPI_FRF_TI = 1,
    SPI_FRF_MICROWIRE = 2
} spi_frf_t;
void hwspi_set_frame_format(spi_frf_t frf);
bool hwspi_get_cphase(void);
void hwspi_set_cphase(bool cpha);