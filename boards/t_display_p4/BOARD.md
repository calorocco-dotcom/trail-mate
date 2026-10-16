# T-Display P4 Board Facts

Sources:

- `boards/t_display_p4/include/boards/t_display_p4/board_profile.h`
- `boards/t_display_p4/src/t_display_p4_board.cpp`

This record describes hardware facts only.

## Identity

- board package: `boards/t_display_p4`
- final facts directory: `boards/t_display_p4`
- board id: `t_display_p4`
- platform family: `esp32`
- product name: `TrailMate P4`

## Confirmed Facts

- HI8561 panel size: 540 x 1168
- RM69A10 panel size: 568 x 1232
- touch present: yes
- audio present: yes
- SD card present: yes
- GPS UART present: yes
- LoRa present: yes
- keyboard module: optional T-Display-P4 Keyboard accessory
- keyboard default state: absent unless runtime TCA8418 probe succeeds
- keyboard module HP I2C0: `SDA=46`, `SCL=45`, `INT=48`, `BL=47`; SYS uses LP I2C0 on GPIO7/8
- keyboard controller: TCA8418 at `0x34`, 10 x 7 scan window
- ESP32-C6 companion present: yes
- ESP32-C6 SDIO link: CLK=18, CMD=19, D0=14, D1=15, D2=16, D3=17
- ESP32-C6 boot/download strap control: XL9535 IO13, assert low / release high
- ESP32-C6 reset/release control: XL9535 IO14, release high / assert low
- motion sensor present: yes, ICM20948 at 0x68
