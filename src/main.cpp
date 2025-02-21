#include "fabgl.h"
#include "zdi.h"
#include "flashloader.h"
#include "eZ80F92.h"
#include <esp_task_wdt.h>

#define PAGESIZE       1024
#define USERLOAD    0x40000
#define BREAKPOINT  0x40158

fabgl::PS2Controller    PS2Controller;
fabgl::VGA16Controller  DisplayController;
fabgl::Terminal         terminal;

void term_printf (const char* format, ...) {
	va_list list;
   	int size = vsnprintf(nullptr, 0, format, list) + 1;
    char buffer[size + 1];

   	va_start(list, format);
   	if (size > 0) {
    	va_end(list);
     	va_start(list, format);
     	vsnprintf(buffer, size, format, list);
        terminal.write (buffer);
   	}
   	va_end(list);
}

uint32_t waitcontinueLoader(void) {
    uint32_t result, pc;

    while(!zdi_debug_breakpoint_reached()); // wait for ez80 to hit breakpoint
    zdi_debug_break;
    pc = zdi_read_cpu (REG_PC);
    zdi_write_cpu (REG_BC, 0x100); // write '1' to register B, don't care about 'C'
    result = zdi_read_cpu (REG_HL);
    zdi_write_cpu (REG_PC, pc);
    //zdi_debug_breakpoint_enable (0, BREAKPOINT);
    zdi_debug_continue();
    return result;
}

void boot_screen() {
    // initialize terminal
    term_printf("\e[44;37m"); // background: blue, foreground: white
    term_printf("\e[2J");     // clear screen
    term_printf("\e[1;1H");   // move cursor to 1,1
    term_printf("Agon MOS ZDI flash utility - version 0.3\r\n\r\n");
}

void waitforKey(uint8_t key) {
    fabgl::Keyboard *kb = PS2Controller.keyboard();
    fabgl::VirtualKeyItem item;
    byte keycode = 0;						// Last pressed key code
    byte modifiers = 0;						// Last pressed key modifiers
    
    while(1) {
        if(kb->getNextVirtualKey(&item, 0)) {
            if(item.ASCII == key) break;
        }
    }
}

void ask_initial() {
    term_printf("This utility will (baremetal) program a new MOS to the Agon flash,\r\n");
    term_printf("from a file on the SD card. This can be performed in case of\r\n");
    term_printf("an initially erased or corrupted flash, or having previously flashed\r\n");
    term_printf("an incompatible MOS version.\r\n");
    term_printf("\r\nRequirements before proceeding:\r\n");
    term_printf(" 1) Connect two cables between the GPIO and ZDI ports:\r\n");
    term_printf("    - ESP GPIO26 to ZDI TCK (pin 4)\r\n");
    term_printf("    - ESP GPIO27 to ZDI TDI (pin 6)\r\n");
    term_printf(" 2) Place the required MOS version on the SD card's root\r\n"); 
    term_printf("    with filename \"MOS.bin\"\r\n");
    term_printf(" 3) Reset the board after inserting the SD card\r\n\r\n");
    term_printf("Press ENTER to proceed:");
    waitforKey(0x0D);
    term_printf("\r\n\r\n");
}

void ask_proceed() {
    term_printf("\r\nPress [y] to start programming MOS:");
    waitforKey('y');
    term_printf("\r\n\r\n");
}

void setup() {
    // Disable the watchdog timers
    disableCore0WDT(); delay(200);								
	disableCore1WDT(); delay(300);

    esp_task_wdt_init(30, false); // in case WDT cannot be removed

    // setup keyboard/PS2
    PS2Controller.begin(PS2Preset::KeyboardPort0, KbdMode::CreateVirtualKeysQueue);

    // setup VGA display
    DisplayController.begin();
    DisplayController.setResolution(VGA_640x480_60Hz);
    
    // setup terminal
    terminal.begin(&DisplayController);
    terminal.enableCursor(true);

    // setup ZDI interface
    zdi_enter();
}

void init_ez80(void) {
    zdi_debug_break ();
    zdi_read_cpu (SET_ADL);
    zdi_cpu_instruction_di ();
    
    
    // configure default GPIO
    zdi_cpu_instruction_out (PB_DDR, 0xff);
    zdi_cpu_instruction_out (PC_DDR, 0xff);
    zdi_cpu_instruction_out (PD_DDR, 0xff);
    
    zdi_cpu_instruction_out (PB_ALT1, 0x0);
    zdi_cpu_instruction_out (PC_ALT1, 0x0);
    zdi_cpu_instruction_out (PD_ALT1, 0x0);
    zdi_cpu_instruction_out (PB_ALT2, 0x0);
    zdi_cpu_instruction_out (PC_ALT2, 0x0);
    zdi_cpu_instruction_out (PD_ALT2, 0x0);

    zdi_cpu_instruction_out (TMR0_CTL, 0x0);
    zdi_cpu_instruction_out (TMR1_CTL, 0x0);
    zdi_cpu_instruction_out (TMR2_CTL, 0x0);
    zdi_cpu_instruction_out (TMR3_CTL, 0x0);
    zdi_cpu_instruction_out (TMR4_CTL, 0x0);
    zdi_cpu_instruction_out (TMR5_CTL, 0x0);

    zdi_cpu_instruction_out (UART0_IER, 0x0);
    zdi_cpu_instruction_out (UART1_IER, 0x0);

    zdi_cpu_instruction_out (I2C_CTL, 0x0);
    zdi_cpu_instruction_out (FLASH_IRQ, 0x0);

    zdi_cpu_instruction_out (SPI_CTL, 0x4);

    zdi_cpu_instruction_out (RTC_CTRL, 0x0);
    
    // configure internal flash
    zdi_cpu_instruction_out (FLASH_ADDR_U,0x00);
    zdi_cpu_instruction_out (FLASH_CTRL,0b00101000);   // flash enabled, 1 wait state
    
    // configure internal RAM chip-select range
    zdi_cpu_instruction_out (RAM_ADDR_U,0xb7);         // configure internal RAM chip-select range
    zdi_cpu_instruction_out (RAM_CTL,0b10000000);      // enable
    // configure external RAM chip-select range
    zdi_cpu_instruction_out (CS0_LBR,0x04);            // lower boundary
    zdi_cpu_instruction_out (CS0_UBR,0x0b);            // upper boundary
    zdi_cpu_instruction_out (CS0_BMC,0b00000001);      // 1 wait-state, ez80 mode
    zdi_cpu_instruction_out (CS0_CTL,0b00001000);      // memory chip select, cs0 enabled

    // configure external RAM chip-select range
    zdi_cpu_instruction_out (CS1_CTL,0x00);            // memory chip select, cs1 disabled
    // configure external RAM chip-select range
    zdi_cpu_instruction_out (CS2_CTL,0x00);            // memory chip select, cs2 disabled
    // configure external RAM chip-select range
    zdi_cpu_instruction_out (CS3_CTL,0x00);            // memory chip select, cs3 disabled

    // set stack pointer
    zdi_write_cpu (REG_SP,0x0A0000);
}

void ZDI_upload(void) {
    uint32_t address,filesize;
    unsigned char *buffer;

    address = USERLOAD;
    buffer = &flashloader_bin[0];
    filesize = flashloader_bin_len;

    while(filesize > PAGESIZE) {
        zdi_write_memory(address, PAGESIZE, buffer);
        terminal.write(".");
        address += PAGESIZE;
        buffer += PAGESIZE;
        filesize -= PAGESIZE;
        sys_delay_ms(1);
    }
    zdi_write_memory(address, filesize, buffer);
}

void loop() {
    uint32_t page,pages,filesize;
    uint16_t productid;
    uint8_t memval;

    boot_screen();
    ask_initial();

    productid = zdi_get_productid();
    if((productid == 0) || (productid == 65535)) {
        term_printf("ZDI Interface DOWN - check cabling and reset\r\n");
        term_printf("--------------------------------------------\r\n");
        while(1);
    }
    
    boot_screen();
    term_printf("Action                         Status\r\n");
    term_printf("-------------------------------------\r\n");
    term_printf("Checking ZDI interface       - UP (ID %X.%02X)\r\n",productid, zdi_get_revision());
    term_printf("ZDI uploading flash utility  - ");

    init_ez80();
    //zdi_write_memory(address, flashloader_bin_len, &flashloader_bin[0]);
    ZDI_upload();
 
    term_printf(" Done\r\n");
    zdi_debug_breakpoint_enable (0, BREAKPOINT);    
    zdi_write_cpu (REG_PC, USERLOAD);
    zdi_debug_continue();   // start loaded program
    
    term_printf("Starting bootloader          - ");
    waitcontinueLoader();
    term_printf("Done\r\n");   
    term_printf("Opening file from SD card    - ");
    filesize = waitcontinueLoader();
    if(filesize == 0) {
        term_printf("Error opening \"MOS.bin\"");
        while(1);
    }
    if(filesize == 0xFFFFFF) {
        term_printf("Invalid file size for \"MOS.bin\"");
        while(1);
    }
    term_printf("Done");
    term_printf(" (%d bytes)\r\n", filesize);
    term_printf("Reading file to ez80 memory  - ");
    waitcontinueLoader();
    term_printf("Done\r\n");

    ask_proceed();
    waitcontinueLoader();

    term_printf("Erasing flash                - ");
    pages = waitcontinueLoader();
    term_printf("Done\r\n");

    term_printf("Programming ");

    page = 0;
    do {
        page++;
        terminal.write(".");
        waitcontinueLoader();
    }
    while(page < pages);
    
    terminal.write(" Done\r\n\r\n");
    terminal.write("MOS has been programmed to ez80 flash\r\n");
    terminal.write("Please (re)program ESP32 with matching VDP");

    while(1);
}
