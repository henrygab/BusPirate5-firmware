#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "queue.h"
#include "hardware/dma.h"
//#include "buf.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "usb_rx.h"
#include "usb_tx.h"
#include "tusb.h"
#include "pirate/bio.h"
#include "system_config.h"
#include "debug.h"

// BUGBUG -- Rename all external functions with `bp_usb_cdc_` prefix
// BUGBUG -- make all internal functions and variables static
// BUGBUG -- more use of `const`?


// USB TX:
// This file contains the code for the user interface ring buffer
// and IO method. USB is the normal IO, but running a chip under debug 
// on a USB attached device is a nightmare. BP_DEBUG_ENABLED In the /platform/ folder
// configuration file enables a UART debugging option. All user interface IO
// will be routed to one of the two UARTs (selectable) on the Bus Pirate buffered IO pins
// UART debug mode is way over engineered using DMA et al, and has some predilection for bugs
// in status bar updates

#define TX_FIFO_LENGTH_IN_BITS 10 // 2^n buffer size. 2^3=8, 2^9=512, 2^10=1024 -- name as TX_FIFO_LOG2_SIZE?
#define TX_FIFO_LENGTH_IN_BYTES (0x0001<<TX_FIFO_LENGTH_IN_BITS)

/// @brief This set of states is used to reduce chance of interrupting in the midst of a VT100 sequence.
/// @details
///        Condition mitigated could occur when VT100 sequence is being added to the tx_fifo queue, and that
///        tx_fifo queue becomes full in the midst of that sequence.  In this case, sending a status bar
///        update would have undefined behavior, and likely not display on the terminal correctly.
///        Typically, the SB (statusbar) is idle.
///        IDLE  --> DELAY :== occurs only when no bytes were transferred from the tx_fifo _AND_ SB has updates
///        DELAY --> IDLE  :== occurs when tx_fifo has more bytes to transfer in next loop
///        DELAY --> SB_TX :== two calls to the handler with no bytes in tx_fifo, and status bar having updates
///        SB_TX --> IDLE  :== unconditional... all SB data is sent and SB update flag is cleared
/// @note
///        Helps for Core0:  Writes to tx_fifo queue from Core0 are blocking.  Core1 will eventually service
///        the queue.  As soon as it does, Core0 is immediately unblocked and more data is added to the queue.
///        While not a 100% guarantee, no reasonable scenario is known where Core0's writes could have a VT100
///        sequence from a single printf() call be interrupted.
/// @bug
///        PROBLEM:
///        Code executing in Core1 should NOT be able to ADD to `tx_fifo()`, as the queue synchronization is
///        on a per-byte (char) basis, and thus cannot synchronize with Core0's outputting of multi-character
///        VT100 sequences.   At the same time, Core1 needs to be able to send output, such as for debug messages
///        or logging.
///        PROPOSED SOLUTIONS:
///        Generally, want to have anything output from Core1 go to a unique set of channels.  This should apply
///        to all output modalities, whether sending to the Terminal, Binmode, or Debug channels (regardless of
///        where they physically get output).
///        OPTION A:
///        Modify `printf()` and related code to support unique output channels, based on which core called
///        the function.  This is the most flexible and reliable mechanism currently envisaged, and should have
///        negligible performance impact (single instruction to get core, use to index array for output channel).
///        OPTION B:
///        Modify the `tx_fifo_put()` function to support a core number as an argument.
///        Similar to the above, but may be simpler to implement.  However, it increases costs for each character
///        output, vs. once per `printf()` call.
///        
typedef enum _UI_WITH_STATUSBAR_STATE {
    UI_WITH_STATUSBAR_STATE__IDLE,
    UI_WITH_STATUSBAR_STATE__DELAY,
    UI_WITH_STATUSBAR_STATE__STATUSBAR_TX
} UI_WITH_STATUSBAR_STATE;

// TODO: rework all the TX stuff into a nice struct with clearer naming 
// typedef struct _BP_LOGGING_CHANNEL {
//     char buffer[TX_FIFO_LENGTH_IN_BYTES] __attribute__((aligned(2048))); // BUGBUG ... why not align by TX_FIFO_LENGTH_IN_BYTES (aka 1024)?
//     queue_t       fifo;
//     UI_WITH_STATUSBAR_STATE state; // only used by CDC0 (main UI)
//     uint16_t      endpoint_size; // e.g., USB_ENDPOINT_SIZE_CDC_0 ... or zero if not using USB CDC
//     uint8_t       itf_num;       // e.g., ITF_NUM_CDC_0 (from usb_descriptors.c)
//     // TBD: Each CDC may output to up to two interfaces (e.g., UART and USB)
//     // TBD: Maybe two function pointers per CDC?
//     //      1. bool () which determine if the  to 
// } BP_USB_CDC_TX_STATE __attribute__((aligned(2048)));                 // BUGBUG ... why not align by TX_FIFO_LENGTH_IN_BYTES (aka 1024)?

queue_t tx_fifo;
queue_t bin_tx_fifo;
#if defined(ENABLE_THIRD_CDC_PORT)
queue_t dbg_tx_fifo;
#endif

char tx_buf    [TX_FIFO_LENGTH_IN_BYTES] __attribute__((aligned(2048))); // BUGBUG ... why not align by TX_FIFO_LENGTH_IN_BYTES (aka 1024)?
char bin_tx_buf[TX_FIFO_LENGTH_IN_BYTES] __attribute__((aligned(2048))); // BUGBUG ... why not align by TX_FIFO_LENGTH_IN_BYTES (aka 1024)?
#if defined(ENABLE_THIRD_CDC_PORT)
char dbg_tx_buf[TX_FIFO_LENGTH_IN_BYTES] __attribute__((aligned(2048))); // BUGBUG ... why not align by TX_FIFO_LENGTH_IN_BYTES (aka 1024)?
#endif

// STATUS BAR BUFFER
// N.B. - tx_sb_buf[1024] is declared externally, sprintf()'d to by ui_statusbar.c ... complete with overflow!
char     tx_sb_buf[1024]; // BUGBUG -- Magic Number ... should be a #define in same header as defined as `extern`

// BUGBUG -- Why isn't the status bar also using the queue2 mechanism?  Because the buffer is directly exposed.
uint16_t tx_sb_buf_cnt   = 0;
uint16_t tx_sb_buf_index = 0;
bool     tx_sb_buf_ready = false;

void tx_fifo_init(void) // BUGBUG -- need to update function name to reflect that it's initializing all the TX FIFOs
{
    // Initialize the three queues used for the USB CDC transmit buffer
    queue2_init(&tx_fifo,     tx_buf,     TX_FIFO_LENGTH_IN_BYTES); //buffer size must be 2^n for queue AND DMA rollover
    queue2_init(&bin_tx_fifo, bin_tx_buf, TX_FIFO_LENGTH_IN_BYTES); //buffer size must be 2^n for queue AND DMA rollover
#if defined(ENABLE_THIRD_CDC_PORT)
    queue2_init(&dbg_tx_fifo, dbg_tx_buf, TX_FIFO_LENGTH_IN_BYTES); //buffer size must be 2^n for queue AND DMA rollover
#endif
}

void tx_sb_start(uint32_t len)
{
    // Indicates that the status bar data has `len` bytes ready to be sent
    tx_sb_buf_cnt=len;
    tx_sb_buf_ready=true;
}

void tx_fifo_service(void)
{
    // Generally, three steps:
    // 1. Pre-check:
    //    if USB output is enabled, but tinyUSB hasn't been setup, return early.
    //    N.B. - this means UART will not output data until USB is ready.
    // 2. Prepare data to be sent:
    //    For CDC0, this is a state machine to allow separate update of the
    //    status bar, without (hopefully) interrupting partial VT100 sequences.
    // 3. Send data:
    //    If USB output is enabled, send the data via USB CDC.
    //    If UART output is enabled, send the data via HW UART.


    //state machine:
    static UI_WITH_STATUSBAR_STATE tx_state = UI_WITH_STATUSBAR_STATE__IDLE; // BUGBUG -- move into struct with buffer, queue, etc.

    // Generally, the USB terminal is always set to TRUE.
    // However, can be compiled to NOT enable the USB terminal, and instead use the UART instead.
    // Also, want this to still output to a UART if configured to do so.
    if (system_config.terminal_usb_enable) {   // is tinyUSB CDC ready?
        // N.B. - Always waits for entirely empty USB CDC buffer
        if(tud_cdc_n_write_available(0) < 64) // Magic numbers... should these be CDC_ITF_IDX_TERMINAL and CFG_TUD_CDC_TX_BUFSIZE?
        {
            return;
        }
    }

    uint16_t queue_bytes_available;

    char data[64]; // Magic number... should this be CFG_TUD_CDC_TX_BUFSIZE?

#pragma region    // Prepare data to be sent
    uint8_t i=0;   // during this switch statement, `i` will be set to the number of bytes copied into `data`, and thus ready to be sent.
    switch(tx_state)
    {
        case UI_WITH_STATUSBAR_STATE__IDLE:
            // check if bytes are available in the queue
            queue_available_bytes(&tx_fifo, &queue_bytes_available);
            if (queue_bytes_available)
            {             
                i=0;
                while(queue2_try_remove(&tx_fifo, &data[i])) 
                {
                    i++;
                    if(i>=64) { // BUGBUG -- Magic number ... should be CFG_TUD_CDC_TX_BUFSIZE?
                        break;
                    }
                } 
                break; //break out of switch and continue below (where USB TX actually occurs)
            }
            // N.B. - due to use of `break` above, the following is equivalent to an `else` clause....

            // No bytes in queue, so check if status bar update is ready (only for CDC0)
            if (tx_sb_buf_ready)
            {
                tx_state = UI_WITH_STATUSBAR_STATE__DELAY;
                tx_sb_buf_index = 0;
                return; //return for next state
            }
            
            return; //nothing, just return

            break;
        case UI_WITH_STATUSBAR_STATE__DELAY:
            // test: check that no bytes in tx_fifo minimum 2 cycles in a row
            // prevent the status bar from being wiped out by the VT100 setup commands 
            // that might be pending in the TX FIFO
            // Note: this is a HACK ... presumes that no bytes being available in two
            //       consecutive calls to this routine somehow implies that there is
            //       no partially written VT100 escape sequence.  FRAGILE!
            queue_available_bytes(&tx_fifo, &queue_bytes_available);
            tx_state=(queue_bytes_available ? UI_WITH_STATUSBAR_STATE__IDLE : UI_WITH_STATUSBAR_STATE__STATUSBAR_TX);   
            return; //return for next cycle

            break;
        case UI_WITH_STATUSBAR_STATE__STATUSBAR_TX:
            // send up to CFG_TUD_CDC_TX_BUFSIZE bytes of data at a time until complete
            // TODO: pass a pointer to the array cause this is inefficient
            i=0;
            while ((i < 64) && (tx_sb_buf_index < tx_sb_buf_cnt)) // BUGBUG -- Magic Number should be CFG_TUD_CDC_TX_BUFSIZE?
            {
                data[i] = tx_sb_buf[tx_sb_buf_index]; 
                tx_sb_buf_index++;
                i++;
                if (tx_sb_buf_index >= tx_sb_buf_cnt)
                {
                    tx_sb_buf_ready=false;
                    tx_state=UI_WITH_STATUSBAR_STATE__IDLE; //done, next cycle go to idle
                    // N.B. - next comment seems important, but its meaning is unclear
                    system_config.terminal_ansi_statusbar_update=true; //after first draw of status bar, then allow updates by core1 service loop
                    break;
                }
                if(i>=64) // BUGBUG -- Magic Number should be CFG_TUD_CDC_TX_BUFSIZE?
                {
                    break;
                }
            } 
            break;
        default:
            tx_state=UI_WITH_STATUSBAR_STATE__IDLE;
            break;    

    }
#pragma endregion // Prepare data to be sent

    // If code reaches this point, then `data` buffer has `i` bytes to be sent
 
    //if(i==0) return; //safety check

    //write to terminal usb
    if (system_config.terminal_usb_enable)
    {           
        tud_cdc_n_write(0, &data, i); // BUGBUG -- Magic Number should be CDC_ITF_IDX_TERMINAL?
        tud_cdc_n_write_flush(0);     // BUGBUG -- Magic Number should be CDC_ITF_IDX_TERMINAL?
        if (system_config.terminal_uart_enable) { // makes it nicer if we service when the UART is enabled
            tud_task(); 
        }
    }
    
    //write to terminal debug uart
    if (system_config.terminal_uart_enable){
        for (uint8_t j=0; j<i; j++){
            uart_putc(debug_uart[system_config.terminal_uart_number].uart, data[j]);
        }
    }
    
    return;
}

// basis of stdio output such as printf()
void tx_fifo_put(char *c) {
    // BUGBUG: Can this deadlock, if called from Core1 with full queue, since Core1 calls the tx_fifo_service() function?
    //         Potential fix: try to add, and if fails and on core1, then loop calling tx_fifo_service() and trying to add again.
    queue2_add_blocking(&tx_fifo, c);
}

// basis of stdio output to binary mode COM port
void bin_tx_fifo_put(const char c){
    // BUGBUG: Can this deadlock, if called from Core1 with full queue, since Core1 calls the tx_fifo_service() function?
    //         Potential fix: try to add, and if fails and on core1, then loop calling bin_tx_fifo_service() and trying to add again.
    queue2_add_blocking(&bin_tx_fifo, &c);
}

bool bin_tx_fifo_try_get(char *c){
    return queue2_try_remove(&bin_tx_fifo, c);
}

void bin_tx_fifo_service(void)
{
    uint16_t bytes_available;
    char data[64]; // Magic number... should this be CFG_TUD_CDC_TX_BUFSIZE?
    uint8_t i=0;

    // is tinyUSB CDC ready?
    if(tud_cdc_n_write_available(1)<64) // BUGBUG -- Magic Numbers should be CDC_ITF_IDX_BINMODE and CFG_TUD_CDC_TX_BUFSIZE?
    {        
        return;
    }

    queue_available_bytes(&bin_tx_fifo, &bytes_available);
    if(bytes_available)
    {             
        i=0;
        while(queue2_try_remove(&bin_tx_fifo, &data[i])) 
        {
            i++;
            if(i>=64) break; // BUGBUG -- Magic Number should be USB_ENDPOINT_SIZE_CDC
        } 
    }
   
    tud_cdc_n_write(1, &data, i); // BUGBUG -- Magic Number should be CDC_ITF_IDX_BINMODE
    tud_cdc_n_write_flush(1);     // BUGBUG -- Magic Number should be CDC_ITF_IDX_BINMODE
}

bool bin_tx_not_empty(void) // BUGBUG -- Unused function should be removed
{
    uint16_t cnt;
    queue_available_bytes(&bin_tx_fifo, &cnt);
    return !!(TX_FIFO_LENGTH_IN_BYTES - cnt); // force to bool
}

// BUGBUG -- Need to add at least functions to add to debug TX FIFO, and to service the debug TX FIFO
#if defined(ENABLE_THIRD_CDC_PORT)
void dbg_tx_fifo_put(const char c) {
    // BUGBUG: Can this deadlock, if called from Core1 with full queue, since Core1 calls the dbg_fifo_service() function?
    //         Potential fix: try to add, and if fails and on core1, then loop calling dbg_tx_fifo_service() and trying to add again.
    queue2_add_blocking(&dbg_tx_fifo, &c);
}
void dbg_fifo_service(void) {
    uint16_t bytes_available;
    char data[64]; // Magic number... should this be CFG_TUD_CDC_TX_BUFSIZE?
    uint8_t i=0;

    // is tinyUSB CDC ready?
    if(tud_cdc_n_write_available(2)<64) // BUGBUG -- Magic Numbers should be CDC_ITF_IDX_DEBUG and CFG_TUD_CDC_TX_BUFSIZE?
    {        
        return;
    }

    queue_available_bytes(&bin_tx_fifo, &bytes_available);
    if(bytes_available)
    {             
        i=0;
        while(queue2_try_remove(&bin_tx_fifo, &data[i])) 
        {
            i++;
            if(i>=64) break; // BUGBUG -- Magic Number should be CFG_TUD_CDC_TX_BUFSIZE
        }
    }
   
    tud_cdc_n_write(2, &data, i); // BUGBUG -- Magic Number should be CDC_ITF_IDX_DEBUG
    tud_cdc_n_write_flush(2);     // BUGBUG -- Magic Number should be CDC_ITF_IDX_DEBUG
}
#endif

