void amux_init(void);
// select AMUX input source, use the channel defines from the platform header
// only effects the 4067CD analog mux, you cannot get the current measurement from here
bool amux_select_input(uint16_t channel);
// set the AMUX input source using the BIO pin number
bool amux_select_bio(uint8_t bio);
// read from AMUX using channel list in platform header file
uint32_t amux_read(uint8_t channel);
// read from presently selected AMUX channel
uint32_t amux_read_present_channel(void);
// read from AMUX using BIO pin number
uint32_t amux_read_bio(uint8_t bio);
// this is actually on a different ADC and not the AMUX
// but this is the best place for it I think
// voltage is not /2 so we can use the full range of the ADC
uint32_t amux_read_current(void);
// read all the AMUX channels and the current sense
// place into the global arrays hw_adc_raw and hw_adc_voltage
void amux_sweep(void);

// reset the averaging of all channels, start with the current value
// useful if you expect a step-change of the inputs
extern bool reset_adc_average;

// use power-of-two values for efficient division
#define ADC_AVG_TIMES 64

inline uint32_t get_adc_average(uint32_t avgsum)
{
    // add ADC_AVG_TIMES/2 before division for correct rounding instead of cutting of decimals
    return ((avgsum+(ADC_AVG_TIMES/2))/ADC_AVG_TIMES);
}
