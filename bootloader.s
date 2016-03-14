;------------------------------------------------------------------------------
; Objectives and notes             
;------------------------------------------------------------------------------

;Bootloader located at address 0x400

;Target uC:       33FJ128MC802
;Clock Rate:      40 MHz
;Devices used:    UART, LEDs
;Configuration:   JTAG disabled

;Hardware description:
;Red LED
;  anode connected through 100ohm resistor to RB5 (pin 14), cathode grounded
;Green LED
;  anode connected through 100ohm resistor to RB4 (pin 11), cathode grounded
;Push Button
; Push button connected between RB15 (pin 26) and ground
;SP3232E RS-232 Interface
; T1IN connected to RP10 (pin 21)
; R1OUT connected to RP11 (pin 22)

;------------------------------------------------------------------------------
; Device includes and assembler directives             
;------------------------------------------------------------------------------

           .include "p33FJ128MC802.inc"
           .global __reset

;------------------------------------------------------------------------------
; Uninitialized variable space (in data memory)               
;------------------------------------------------------------------------------

           .bss
__SP:      .space   4*16                      ;allocate 16-level deep stack
__SP_LIM:  .space   4*2                       ;add pad after limit for debug
PAGE_DATA: .space   512*4                     ;allocate space for page data

;------------------------------------------------------------------------------
; Code starts here               
;------------------------------------------------------------------------------

           .text
__reset:   goto     main

;------------------------------------------------------------------------------
; Subroutines                
;------------------------------------------------------------------------------

;Serial putc
;  Parameters: character to TX in W0
;  Returns:    nothing
serial_putc: 
           btsc     U1STA, #UTXBF             ;wait until tx char needed
           bra      serial_putc
           mov      W0, U1TXREG               ;tx char
           return

;Serial getc
;  Parameters: nothing
;  Returns:    character RX in W0
serial_getc: 
           btsc     U1STA, #OERR              ;clear overflow condition
           bclr     U1STA, #OERR
sgc_loop:  btss     U1STA, #URXDA             ;wait until rx char ready
           bra      sgc_loop
           mov      U1RXREG, W0               ;read char
           return 

;Wait for PB Press (change as needed for hardware in use)
;  Parameters: nothing
;  Returns:    1 in W0 if bootload requested, 0 otherwise
boot_req:  bset     CNPU1, #CN11PUE           ;enable pull-up for push button
           nop                                ;wait for inputs to settle
           nop
           nop
           nop
           nop
           btsc     PORTB, #15                ;is pb pressed?
           retlw    #0, W0                    ;no
           retlw    #1, W0                    ;yes           

;------------------------------------------------------------------------------
; Main                
;------------------------------------------------------------------------------

main:      mov      #__SP, W15                ;setup stack
           mov      #__SP_LIM, W0
           mov      W0, SPLIM

           call     boot_req                  ;was boot requested?
           cp0      W0
           bra      Z, no_boot                ;no, run normal code at 0x800

           bclr     LATB, #4                  ;write 0 into output latches
           bclr     LATB, #5                  
           bclr     TRISB, #4                 ;make green led pin an output
           bclr     TRISB, #5                 ;make red led pin an output

           mov      #0x0003, W0               ;assign U1TX to RP10
           mov      W0, RPOR5
           mov      #0x1F0B, W0               ;assign U1RX to RP11
           mov      W0, RPINR18

                                              ;with 8 MHz input, set Fosc=80MHz
           mov      #38, W0                   ;set plldiv=40
           mov      W0, PLLFBD                ;Fosc = 8 / 2 * 40 / 4 = 40 MHz
           mov      #0x3000, W0               ;set pllpostdiv=2, pllprediv=2
           mov      W0, CLKDIV                ;Fosc = 8 / 2 * 40 / 2 = 80 MHz

           mov      #64, W0                   ;initialize uart1 to 38400 baud
           mov      W0, U1BRG                 ;set low-speed baud rate
           mov      #0x8000, W0               ;enable 8 bit, no parity, 1 stop bit  
           mov      W0, U1MODE                ;(8N1), low-speed brg
           mov      #0x0400, W0               ;enable rx and rx, set int flag 
           mov      W0, U1STA                 ; after each char rx or tx

find_seq:  call     serial_getc               ;look for "33F" sequence
           mov.b    #'3', W1
           cp.b     W0, W1                    
           bra      NZ, find_seq
           call     serial_getc
           mov.b    #'3', W1
           cp.b     W0, W1                    
           bra      NZ, find_seq
           call     serial_getc
           mov.b    #'F', W1
           cp.b     W0, W1                    
           bra      NZ, find_seq
           mov      #'k', W0                  ;sequence received, send ack
           call     serial_putc
           bset     LATB, #4                  ;set status LED
           call     serial_getc               ;get page count
           mov      W0, W1                    ;compute checksum in W1
           mov      W0, W2                    ;and page count in W2
           call     serial_getc
           add      W0, W1, W1
           sl       W0, #8, W0
           ior      W0, W2, W2
           mov      W2, W0                    ;transmit computed checksum
           call     serial_putc
           call     serial_getc               ;receive checksum
           cp       W0, W1                    ;does checksum match?
           bra      NZ, error                 ;no, indicate error

rx_page:   mov      #0, W1                    ;set W1 checksum to 0

           call     serial_getc               ;rx 24b address into W7:W6
           add.b    W0, W1, W1                
           mov      W0, W6
           call     serial_getc
           add.b    W0, W1, W1                
           sl       W0, #8, W0
           add      W0, W6, W6
           call     serial_getc
           add.b    W0, W1, W1                
           mov      W0, W7

           mov      #PAGE_DATA, W3            ;set W3 pointer to page data
           mov      #512, W4                  ;set W4 counter to 512
page_loop: call     serial_getc               ;get lsb of lsw
           add.b    W0, W1, W1                ;increment checksum
           mov.b    W0, [W3++]                ;store data
           call     serial_getc               ;get msb of lsw
           add.b    W0, W1, W1                ;increment checksum
           mov.b    W0, [W3++]                ;store data
           call     serial_getc               ;get lsb of msw
           add.b    W0, W1, W1                ;increment checksum
           mov.b    W0, [W3++]                ;store data
           mov      #0xFF, W0                 ;write FFh into msb of msw
           mov.b    W0, [W3++]                    
           dec      W4, W4                    ;decrement loop counter
           bra      NZ, page_loop             
           mov      W1, W0                    ;send checksum
           call     serial_putc
           call     serial_getc               ;rx checksum
           cp       W0, W1                    ;does checksum match?
           bra      NZ, error                 ;no, indicate error

           mov      #0x4042, W0               ;prepare for page erase
           mov      W0, NVMCON
           mov      W7, TBLPAG                ;set starting address of page
           tblwtl   W6, [W6]                  ;write garbage to set address
           disi     #5                        ;disable interrupts for key sequence
           mov      #0x55, W0                 ;send key sequence to unlock
           mov      W0, NVMKEY
           mov      #0xAA, W0
           mov      W0, NVMKEY
           bset     NVMCON, #WR               ;start erase
           nop                                ;manditory nop cycles
           nop
  
           mov      #PAGE_DATA, W3            ;set W3 pointer to page data
           sub      W6, #2, W6                ;adjust W6 to use pre-increments
           mov      #8, W5                    ;set loop counter (W5) to 8 rows
           
row_loop:  mov      #0x4001, W0               ;prepare for row write
           mov      W0, NVMCON
           mov      W7, TBLPAG                ;prepare for row write
           mov      #64, W8                   ;set instruction cntr (W8) to 64
copy_row:  tblwtl   [W3++], [++W6]            ;copy row to holding registers
           tblwth   [W3++], [W6]
           dec      W8, W8
           bra      NZ, copy_row
           disi     #5                        ;disable interrupts for key sequence
           mov      #0x55, W0                 ;send key sequence to unlock
           mov      W0, NVMKEY
           mov      #0xAA, W0
           mov      W0, NVMKEY
           bset     NVMCON, #WR               ;start write
           nop                                ;manditory nop cycles
           nop
           btsc     NVMCON, #15               ;wait for completion of write
           bra      $-2
           dec      W5, W5
           bra      NZ, row_loop

           mov.b    #'d', W0                  ;send done flag
           call     serial_putc

           dec      W2, W2                    ;decrement page count
           bra      NZ, rx_page

           btss     U1STA, #TRMT              ;wait until tx shift reg empty
           bra      $-2
           
           reset

error:     bset     LATB, #5                  ;indicate an error occured
           bclr     TRISB, #5                 
           bra      error
           
           .org     0x400                     ;address is 0x400 beyond start of code at 0x400
                                              ;this places the code at 0x800
no_boot:   bclr     TRISB, #4                 ;placeholder code until the bootloader overwrites later
           btg      LATB, #4                  ;led toggle
           mov      #100, W0                  
nb_loop:   repeat   #16383
           nop
           dec      W0, W0      
           bra      NZ, nb_loop
           bra      no_boot                   
           
           .end

           