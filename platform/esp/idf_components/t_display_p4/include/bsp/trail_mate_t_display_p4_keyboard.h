#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Controls the T-Display-P4 K270 keyboard adapter.
     *
     * The adapter owns only the P2 (GPIO46/GPIO45) XL9555/TCA8418 keyboard
     * chain. It preserves FIFO press/release edges in an LVGL keypad device,
     * selects the active Trail focus group explicitly, and uses a private proxy
     * for text, page and menu shortcuts rather than the mutable default group.
     *
     * A missing expansion keyboard is optional and returns false.  Call
     * trail_mate_t_display_p4_keyboard_initialize() before LVGL starts rendering
     * to bring up the P2 controller chain.  It intentionally does not create an
     * LVGL timer; call trail_mate_t_display_p4_keyboard_start() afterwards to
     * attach Trail's UI polling adapter. Failed probes retry with a cooldown on
     * that same LVGL task; persistent I2C failures also restart controller setup.
     */
    bool trail_mate_t_display_p4_keyboard_initialize(void);

    /** Attach keypad/recovery to LVGL, returning the current controller readiness. */
    bool trail_mate_t_display_p4_keyboard_start(void);

    /** True only after both P2 controllers have been initialized. */
    bool trail_mate_t_display_p4_keyboard_is_ready(void);

#ifdef __cplusplus
}
#endif
