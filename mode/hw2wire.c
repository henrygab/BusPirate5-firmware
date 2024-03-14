#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include <stdint.h>
#include "pirate.h"
#include "system_config.h"
#include "opt_args.h"
#include "bytecode.h"
#include "mode/hw2wire.h"
#include "bio.h"
#include "ui/ui_prompt.h"

#include "hw2wire.pio.h"
#include "mode/hw2wire_pio.h"
#include "storage.h"
#include "ui/ui_term.h"
#include "ui/ui_command.h"
#include "ui/ui_format.h"

#define M_I2C_PIO pio0
#define M_I2C_SDA BIO0
#define M_I2C_SCL BIO1

static const char pin_labels[][5]={
	"SDA",
	"SCL",
};

static struct _hw2wire_mode_config mode_config;

static PIO pio = M_I2C_PIO;
static uint pio_state_machine = 3;
static uint pio_loaded_offset;

static uint8_t checkshort(void);
static void i2c_search_addr(bool verbose);

uint32_t hw2wire_setup(void)
{
	uint32_t temp;

	// menu items options
	static const struct prompt_item i2c_data_bits_menu[]={{T_HWI2C_DATA_BITS_MENU_1},{T_HWI2C_DATA_BITS_MENU_2}};
	static const struct prompt_item i2c_speed_menu[]={{T_HWI2C_SPEED_MENU_1}};		

	static const struct ui_prompt i2c_menu[]={
		{T_HWI2C_SPEED_MENU,i2c_speed_menu,	count_of(i2c_speed_menu),T_HWI2C_SPEED_PROMPT, 1,1000,400,		0,&prompt_int_cfg},
		{T_HWI2C_DATA_BITS_MENU,i2c_data_bits_menu,	count_of(i2c_data_bits_menu),T_HWI2C_DATA_BITS_PROMPT, 0,0,1, 	0,&prompt_list_cfg}
	};

	const char config_file[]="bpi2c.bp";

	struct _mode_config_t config_t[]={
		{"$.baudrate", &mode_config.baudrate},
		{"$.data_bits", &mode_config.data_bits}
	};
	prompt_result result;

	if(storage_load_mode(config_file, config_t, count_of(config_t)))
	{
		printf("\r\n\r\n%s%s%s\r\n", ui_term_color_info(), t[T_USE_PREVIOUS_SETTINGS], ui_term_color_reset());
		printf(" %s: %dKHz\r\n", t[T_HWI2C_SPEED_MENU], mode_config.baudrate);			
		//printf(" %s: %s\r\n", t[T_HWI2C_DATA_BITS_MENU], t[i2c_data_bits_menu[mode_config.data_bits].description]);
		
		bool user_value;
		if(!ui_prompt_bool(&result, true, true, true, &user_value)) return 0;		
		if(user_value) return 1; //user said yes, use the saved settings
	}
	ui_prompt_uint32(&result, &i2c_menu[0], &mode_config.baudrate);
	if(result.exit) return 0;
	//printf("Result: %d\r\n", mode_config.baudrate);
	//ui_prompt_uint32(&result, &i2c_menu[1], &temp);
	//if(result.exit) return 0;
	//mode_config.data_bits=(uint8_t)temp-1;

	storage_save_mode(config_file, config_t, count_of(config_t));
	
	return 1;
}

uint32_t hw2wire_setup_exc(void)
{
	pio_loaded_offset = pio_add_program(pio, &hw2wire_program);
    hw2wire_program_init(pio, pio_state_machine, pio_loaded_offset, bio2bufiopin[M_I2C_SDA], bio2bufiopin[M_I2C_SCL], bio2bufdirpin[M_I2C_SDA], bio2bufdirpin[M_I2C_SCL], mode_config.baudrate);
	system_bio_claim(true, M_I2C_SDA, BP_PIN_MODE, pin_labels[0]);
	system_bio_claim(true, M_I2C_SCL, BP_PIN_MODE, pin_labels[1]);
	mode_config.start_sent=false;
	pio_hw2wire_rx_enable(pio, pio_state_machine, false);
	pio_hw2wire_reset(pio, pio_state_machine);
	printf("PLEASE: feel free to test this mode, but no bug reports.\r\nThis is a work in progress and I am aware of the issues.\r\n");	return 1;
}

bool hw2wire_error(uint32_t error, struct _bytecode *result)
{
/*	switch(error)
	{
		case 1:
			result->error_message=t[T_HWI2C_I2C_ERROR];
			result->error=SRES_ERROR; 
			pio_hw2wire_resume_after_error(pio, pio_state_machine);
			return true;
			break;
		case 2:
			result->error_message=t[T_HWI2C_TIMEOUT];
			result->error=SRES_ERROR; 
			pio_hw2wire_resume_after_error(pio, pio_state_machine);
			return true;
			break;
		default:
			return false;
	}*/
}

void hw2wire_start(struct _bytecode *result, struct _bytecode *next)
{
	result->data_message=t[T_HWI2C_START];

	if(checkshort())
	{
		result->error_message=t[T_HWI2C_NO_PULLUP_DETECTED];
		result->error=SRES_WARN; 
	}
	
	pio_hw2wire_clock_tick(pio, pio_state_machine);
	#if 0
	uint8_t error=pio_hw2wire_start_timeout(pio, pio_state_machine, 0xfffff);

	if(!hw2wire_error(error, result))
	{
		mode_config.start_sent=true;
	}
	#endif
}

void hw2wire_stop(struct _bytecode *result, struct _bytecode *next)
{
	result->data_message=t[T_HWI2C_STOP];

	uint32_t error=pio_hw2wire_stop_timeout(pio, pio_state_machine, 0xffff);

	hw2wire_error(error, result);
}

void hw2wire_write(struct _bytecode *result, struct _bytecode *next)
{
	//if a start was just sent, determine if this is a read or write address
	// and configure the PIO I2C
	#if 0
	if(mode_config.start_sent)
	{
		pio_hw2wire_rx_enable(pio, pio_state_machine, (result->out_data & 1u));
		mode_config.start_sent=false;
	}
	#endif

 
	pio_hw2wire_rx_enable(pio, pio_state_machine, false);
	//uint32_t error=pio_hw2wire_write_timeout(pio, pio_state_machine, result->out_data, 0xffff);
	pio_hw2wire_put_or_err(pio, pio_state_machine, (result->out_data << 1)|(1u));

	printf("Wrote: %d\r\n", result->out_data);

	//hw2wire_error(error, result);

	//result->data_message=(error?t[T_HWI2C_NACK]:t[T_HWI2C_ACK]);

}

void hw2wire_read(struct _bytecode *result, struct _bytecode *next)
{
	bool ack=(next?(next->command!=4):true);

	pio_hw2wire_rx_enable(pio, pio_state_machine, true);

	//uint32_t error=pio_hw2wire_read_timeout(pio, pio_state_machine, &result->in_data, ack, 0xffff);
    //hw2wire_error(error, result);
	pio_hw2wire_get16(pio, pio_state_machine, &result->in_data); 

	//result->data_message=(ack?t[T_HWI2C_ACK]:t[T_HWI2C_NACK]);
}

	typedef struct __attribute__((packed)) sle44xx_atr_struct {
		uint16_t protocol_type:4;
		uint16_t rfu1:1;
		uint16_t structure_identifier:3;
		uint16_t read_with_defined_length:1;
		uint16_t data_units:4;
		uint16_t data_units_bits:3;
	} sle44xx_atr_t;

void hw2wire_macro(uint32_t macro)
{


	uint32_t result=0;
	switch(macro)
	{
		case 0:		printf(" 1. ISO/IEC 7816 Answer to Reset\r\n");
				break;
		case 1:
				//>a.2 D:1 @.2 [ d:10 a.2 r:4
				// IO2 low
				bio_output(2);
				bio_put(2,0);
				// delay
				busy_wait_ms(1);
				// IO2 high
				bio_input(2);
				// clock tick
				pio_hw2wire_clock_tick(pio, pio_state_machine);
				// wait till end of clock tick
				busy_wait_us(50);
				// IO2 low
				bio_output(2);
				bio_put(2,0);
				// read 4 bytes (32 bits)
				pio_hw2wire_rx_enable(pio, pio_state_machine, true);
				uint32_t temp;
				uint8_t atr[4]; 
				printf("ATR: ");
				for(uint i =0; i<4; i++)
				{
					pio_hw2wire_get16(pio, pio_state_machine, &temp);
					atr[i]=(uint8_t)ui_format_bitorder_manual(temp, 8, 1);
					printf("0x%02x ", atr[i] );
				}
				printf("\r\n");	
				if(atr[0]==0x00 || atr[0]==0xFF)
				{
					result=1;
					break;
				}
				sle44xx_atr_t *atr_head;
    			atr_head = (sle44xx_atr_t *)&atr;
				//lets try to decode that
				printf("SLE44xx decoder:\r\n");
				printf("Protocol Type: %s\r\n", (atr[0]>>4)==0b1010?"S":"unknown");
				temp=atr[0]&0b111;
				printf("Structure Identifier: %s\r\n", (temp&0b11==0b000?"ISO Reserved": (temp==0b010)?"General Purpose (Structure 1)":(temp==0b110)?"Proprietary":"Special Application"));
				printf("Read: %s\r\n", ((atr[1]&0b10000000)?"Defined Length":"Read to end"));
				temp = atr[1];
				temp=temp>>3;
				temp=(temp&0b1111);

				printf("Data Units: ");
				if(temp==0b0000) printf("Undefined\r\n");
				else printf("%ld, %d\r\n", pow(2,temp+6), temp);
				printf("Data Units Bits: %d, %d\r\n", pow(2, (atr[1]&0b00000111)), (atr[1]&0b00000111));		
				break;
				//no idea why this isn't working...
				#if 0
				printf("SLE44xx decoder:\r\n");
				printf("Protocol Type: %s %d\r\n", (atr_head->protocol_type==0b1010?"S":"unknown"), atr_head->protocol_type);
				printf("Structure Identifier: %s\r\n", (atr_head->structure_identifier&0b11==0b000?"ISO Reserved": (atr_head->structure_identifier==0b010)?"General Purpose (Structure 1)":(atr_head->structure_identifier==0b110)?"Proprietary":"Special Application"));
				printf("Read: %s\r\n", (atr_head->read_with_defined_length?"Defined Length":"Read to end"));
				printf("Data Units: %d ", atr_head->data_units);
				if(atr_head->data_units==0b0000) printf("Undefined\r\n");
				else printf("%ld\r\n", pow(2,atr_head->data_units+6));
				printf("Data Units Bits: %d, %d\r\n", pow(2, atr_head->data_units_bits), atr_head->data_units_bits);		
				#endif
				break;
		default:	printf("%s\r\n", t[T_MODE_ERROR_MACRO_NOT_DEFINED]);
				system_config.error=1;
	}

	if(result)
	{
		printf("Device not found\r\n");
	}
}

void hw2wire_cleanup(void)
{
	pio_remove_program (pio, &hw2wire_program, pio_loaded_offset);
	//pio_clear_instruction_memory(pio);

	bio_init();

	system_bio_claim(false, M_I2C_SDA, BP_PIN_MODE,0);
	system_bio_claim(false, M_I2C_SCL, BP_PIN_MODE,0);
}

/*void hw2wire_pins(void)
{
	printf("-\t-\tSCL\tSDA");
}*/

void hw2wire_settings(void)
{
	printf("HWI2C (speed)=(%d)", mode_config.baudrate_actual);
}

void hw2wire_printI2Cflags(void)
{
	uint32_t temp;
/*
	temp=I2C_SR1(BP_I2C);

	if(temp&I2C_SR1_SMBALERT) printf(" SMBALERT");
	if(temp&I2C_SR1_TIMEOUT) printf(" TIMEOUT");
	if(temp&I2C_SR1_PECERR) printf(" PECERR");
	if(temp&I2C_SR1_OVR) printf(" OVR");
	if(temp&I2C_SR1_AF) printf(" AF");
	if(temp&I2C_SR1_ARLO) printf(" ARLO");
	if(temp&I2C_SR1_BERR) printf(" BERR");
	if(temp&I2C_SR1_TxE) printf(" TxE");
	if(temp&I2C_SR1_RxNE) printf(" RxNE");
	if(temp&I2C_SR1_STOPF) printf(" STOPF");
	if(temp&I2C_SR1_ADD10) printf(" ADD10");
	if(temp&I2C_SR1_BTF) printf(" BTF");
	if(temp&I2C_SR1_ADDR) printf(" ADDR");
	if(temp&I2C_SR1_SB) printf(" SB");

	temp=I2C_SR2(BP_I2C);

	if(temp&I2C_SR2_DUALF) printf(" DUALF");
	if(temp&I2C_SR2_SMBHOST) printf(" SMBHOST");
	if(temp&I2C_SR2_SMBDEFAULT) printf(" SMBDEFAULT");
	if(temp&I2C_SR2_GENCALL) printf(" GENCALL");
	if(temp&I2C_SR2_TRA) printf(" TRA");
	if(temp&I2C_SR2_BUSY) printf(" BUSY");
	if(temp&I2C_SR2_MSL) printf(" MSL");
	*/
}

void hw2wire_help(void)
{
	printf("Muli-Master-multi-slave 2 wire protocol using a CLOCK and a bidirectional DATA\r\n");
	printf("line in opendrain configuration. Standard clock frequencies are 100KHz, 400KHz\r\n");
	printf("and 1MHz.\r\n");
	printf("\r\n");
	printf("More info: https://en.wikipedia.org/wiki/I2C\r\n");
	printf("\r\n");
	printf("Electrical:\r\n");
	printf("\r\n");
	printf("BPCMD\t   { |            ADDRES(7bits+R/!W bit)             |\r\n");
	printf("CMD\tSTART| A6  | A5  | A4  | A3  | A2  | A1  | A0  | R/!W| ACK* \r\n");
	printf("\t-----|-----|-----|-----|-----|-----|-----|-----|-----|-----\r\n");
	printf("SDA\t\"\"___|_###_|_###_|_###_|_###_|_###_|_###_|_###_|_###_|_###_ ..\r\n");
	printf("SCL\t\"\"\"\"\"|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__ ..\r\n");
	printf("\r\n");
	printf("BPCMD\t   |                      DATA (8bit)              |     |  ]  |\r\n");
	printf("CMD\t.. | D7  | D6  | D5  | D4  | D3  | D2  | D1  | D0  | ACK*| STOP|  \r\n");
	printf("\t  -|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|\r\n");
	printf("SDA\t.. |_###_|_###_|_###_|_###_|_###_|_###_|_###_|_###_|_###_|___\"\"|\r\n");
	printf("SCL\t.. |__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|__\"__|\"\"\"\"\"|\r\n");
	printf("\r\n");
	printf("* Receiver needs to pull SDA down when address/byte is received correctly\r\n");
	printf("\r\n");
	printf("Connection:\r\n");
	printf("\t\t  +--[2k]---+--- +3V3 or +5V0\r\n");
	printf("\t\t  | +-[2k]--|\r\n");
	printf("\t\t  | |\r\n");
	printf("\tSDA \t--+-|------------- SDA\r\n");
	printf("{BP}\tSCL\t----+------------- SCL  {DUT}\r\n");
	printf("\tGND\t------------------ GND\r\n");			
}


static uint8_t checkshort(void)
{
	uint8_t temp;

	temp=(bio_get(M_I2C_SDA)==0?1:0);
	temp|=(bio_get(M_I2C_SCL)==0?2:0);

	return (temp==3);			// there is only a short when both are 0 otherwise repeated start wont work
}


