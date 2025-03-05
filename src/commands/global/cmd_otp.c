// TODO: BIO use, pullups, psu
/*
    Welcome to dummy.c, a growing demonstration of how to add commands to the Bus Pirate firmware.
    You can also use this file as the basis for your own commands.
    Type "dummy" at the Bus Pirate prompt to see the output of this command
    Temporary info available at https://forum.buspirate.com/t/command-line-parser-for-developers/235
*/
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "command_struct.h"       // File system related
#include "fatfs/ff.h"       // File system related
#include "pirate/storage.h" // File system related
#include "ui/ui_cmdln.h"    // This file is needed for the command line parsing functions
// #include "ui/ui_prompt.h" // User prompts and menu system
// #include "ui/ui_const.h"  // Constants and strings
#include "ui/ui_help.h"    // Functions to display help in a standardized way
#include "system_config.h" // Stores current Bus Pirate system configuration
#include "pirate/amux.h"   // Analog voltage measurement functions
#include "pirate/button.h" // Button press functions
#include "msc_disk.h"
#include "otp/bp_otp.h"
#include "ui/ui_term.h"

#define OTPDIR_ROOT ((uint16_t)(0xF00u - 0x4u)) // 0xEFC


// This array of strings is used to display help USAGE examples for the dummy command
static const char* const usage[] = { "dummy [init|test]\r\n\t[-b(utton)] [-i(nteger) <value>] [-f <file>]",
                                     "Initialize: dummy init",
                                     "Test: dummy test",
                                     "Test, require button press: dummy test -b",
                                     "Integer, value required: dummy -i 123",
                                     "Create/write/read file: dummy -f dummy.txt",
                                     "Kitchen sink: dummy test -b -i 123 -f dummy.txt" };

// This is a struct of help strings for each option/flag/variable the command accepts
// Record type 1 is a section header
// Record type 0 is a help item displayed as: "command" "help text"
// This system uses the T_ constants defined in translation/ to display the help text in the user's preferred language
// To add a new T_ constant:
//      1. open the master translation en-us.h
//      2. add a T_ tag and the help text
//      3. Run json2h.py, which will rebuild the translation files, adding defaults where translations are missing
//      values
//      4. Use the new T_ constant in the help text for the command
static const struct ui_help_options options[] = {
    { 1, "", T_HELP_DUMMY_COMMANDS },    // section heading
    { 0, "init", T_HELP_DUMMY_INIT },    // init is an example we'll find by position
    { 0, "test", T_HELP_DUMMY_TEST },    // test is an example we'll find by position
    { 1, "", T_HELP_DUMMY_FLAGS },       // section heading for flags
    { 0, "-b", T_HELP_DUMMY_B_FLAG },    //-a flag, with no optional string or integer
    { 0, "-i", T_HELP_DUMMY_I_FLAG },    //-b flag, with optional integer
    { 0, "-f", T_HELP_DUMMY_FILE_FLAG }, //-f flag, a file name string
};

void otp_command_handler(struct command_result* res) {
    uint32_t value; // somewhere to keep an integer value
    char file[13];  // somewhere to keep a string value (8.3 filename + 0x00 = 13 characters max)

    // the help -h flag can be serviced by the command line parser automatically, or from within the command
    // the action taken is set by the help_text variable of the command struct entry for this command
    // 1. a single T_ constant help entry assigned in the commands[] struct in commands.c will be shown automatically
    // 2. if the help assignment in commands[] struct is 0x00, it can be handled here (or ignored)
    // res.help_flag is set by the command line parser if the user enters -h
    // we can use the ui_help_show function to display the help text we configured above
    if (ui_help_show(res->help_flag, usage, count_of(usage), &options[0], count_of(options))) {
        return;
    }

    // OTP_DIRECTORY_ITEM directory_item[] = {
    //     {OTP_DIRECTORY_ITEM_TYPE_CERT, 0x100, 7*64, 0},
    //     {OTP_DIRECTORY_ITEM_TYPE_DEVICE_INFO, 0x2c0, 1*64, 0},
    //     {OTP_DIRECTORY_ITEM_TYPE_USB_WHITELABEL, 0xd0, 1*64, 0},
    // };    

    // for(uint32_t i=0; i < count_of(directory_item); i++){
    //     //read the directory item - does it exist?
    //     OTP_DIRECTORY_ITEM existing_directory_item;
    //     if(otp_directory_entry_read(directory_item[i].EntryType, &existing_directory_item)){
    //         printf("Found existing directory item, type %d, row %03X, length %03X\r\n", existing_directory_item.EntryType, existing_directory_item.StartRow, existing_directory_item.RowCount);
    //         continue;
    //     }else{
    //         printf("No existing directory item found, type %d\r\n", directory_item[i].EntryType);
    //         if(!otp_directory_entry_write(directory_item[i].EntryType, directory_item[i].StartRow, directory_item[i].RowCount)){
    //             printf("Failed to write directory item\r\n");
    //         }else{
    //             printf("Directory item written\r\n");
    //         }
    //     }
    // }

    return;

    const char manuf_string_01[] = "Bus Pirate";
    const char manuf_string_02[] = "6";
    const char manuf_string_03[] = "2";
    const char manuf_string_04[] = "2025-2-16 16:39";
    const char manuf_string_05[] = "Shenzhen China";

    struct {
        uint8_t type;
        uint8_t length;
        const char *data;
    } manuf_strings[] = { //includes trailing 0x00
        {0x01, sizeof(manuf_string_01), manuf_string_01},
        {0x02, sizeof(manuf_string_02), manuf_string_02},
        {0x03, sizeof(manuf_string_03), manuf_string_03},
        {0x04, sizeof(manuf_string_04), manuf_string_04},
        {0x05, sizeof(manuf_string_05), manuf_string_05},
    };
  
    return; 
}