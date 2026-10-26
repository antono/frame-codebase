/*
 * This file is a part of: https://github.com/brilliantlabsAR/frame-codebase
 *
 * Authored by: Raj Nakarja / Brilliant Labs Ltd. (raj@brilliant.xyz)
 *              Rohit Rathnam / Silicon Witchery AB (rohit@siliconwitchery.com)
 *              Uma S. Gupta / Techno Exponent (umasankar@technoexponent.com)
 *
 * ISC Licence
 *
 * Copyright © 2023 Brilliant Labs Ltd.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <math.h>
#include <string.h>
#include "error_logging.h"
#include "frame_lua_libraries.h"
#include "i2c.h"
#include "lauxlib.h"
#include "lua.h"
#include "main.h"
#include "nrfx_gpiote.h"
#include "nrfx_systick.h"
#include "pinout.h"
#include "watchdog.h"

#define PI 3.14159265

typedef struct imu_values_y
{
    struct accelerometer_t
    {
        int16_t x;
        int16_t y;
        int16_t z;
    } accelerometer;
    struct magnetometer_t
    {
        int16_t x;
        int16_t y;
        int16_t z;
    } magnetometer;
} imu_values_t;

static int lua_imu_callback_function = 0;

static void lua_imu_tap_callback_handler(lua_State *L, lua_Debug *ar)
{
    sethook_watchdog(L);

    // Reading the status register clears the interrupt. Its low six bits
    // say which way the tap pushed the part, in the same order as the
    // enables in 0x06 and TAPEN: X+ X- Y+ Y- Z+ Z- (datasheet table 17; bit
    // 7 is ACQ_INT). The callback gets the whole byte; one that takes no
    // argument is unaffected.
    i2c_response_t status = i2c_read(ACCELEROMETER, 0x03, 0xFF);
    check_error(status.fail);

    if (lua_imu_callback_function != 0)
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, lua_imu_callback_function);
        lua_pushinteger(L, status.value);

        if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        {
            luaL_error(L, "%s", lua_tostring(L, -1));
        }
    }
}

void imu_tap_interrupt_handler(nrfx_gpiote_pin_t unused_gptiote_pin,
                               nrfx_gpiote_trigger_t unused_gptiote_trigger,
                               void *unused_gptiote_context_pointer)
{
    lua_sethook(L_global,
                lua_imu_tap_callback_handler,
                LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE | LUA_MASKCOUNT,
                1);
}

static int lua_imu_tap_callback(lua_State *L)
{
    if (lua_isnil(L, 1))
    {
        lua_imu_callback_function = 0;
        return 0;
    }

    if (lua_isfunction(L, 1))
    {
        lua_imu_callback_function = luaL_ref(L, LUA_REGISTRYINDEX);
        return 0;
    }

    luaL_error(L, "expected nil or function");

    return 0;
}

static imu_values_t get_imu_data(void)
{
    imu_values_t values;

    // Set PC to wake up magnetometer, and set FORCE to start a conversion
    check_error(i2c_write(MAGNETOMETER, 0x1B, 0x80, 0x80).fail);
    check_error(i2c_write(MAGNETOMETER, 0x1D, 0x40, 0x40).fail);

    // Wait until data is ready
    while (true)
    {
        if (not_real_hardware)
        {
            break;
        }

        if (i2c_read(MAGNETOMETER, 0x18, 0x40).value)
        {
            break;
        }

        nrfx_systick_delay_ms(1);
    }

    // Read magnetometer (14 bit signed integers)
    int16_t x_mag_lsb = i2c_read(MAGNETOMETER, 0x10, 0xFF).value;
    int16_t x_mag_msb = i2c_read(MAGNETOMETER, 0x11, 0xFF).value;
    int16_t y_mag_lsb = i2c_read(MAGNETOMETER, 0x12, 0xFF).value;
    int16_t y_mag_msb = i2c_read(MAGNETOMETER, 0x13, 0xFF).value;
    int16_t z_mag_lsb = i2c_read(MAGNETOMETER, 0x14, 0xFF).value;
    int16_t z_mag_msb = i2c_read(MAGNETOMETER, 0x15, 0xFF).value;

    // Combine bytes and swap the axis to match the worn orientation
    values.magnetometer.x = y_mag_msb << 8 | y_mag_lsb;
    values.magnetometer.y = z_mag_msb << 8 | z_mag_lsb;
    values.magnetometer.z = x_mag_msb << 8 | x_mag_lsb;

    // Clear PC to put magnetometer back to sleep
    check_error(i2c_write(MAGNETOMETER, 0x1B, 0x80, 0x00).fail);

    // Accelerometer data is always available, so just read it
    int16_t x_accel_lsb = i2c_read(ACCELEROMETER, 0x0D, 0xFF).value;
    int16_t x_accel_msb = i2c_read(ACCELEROMETER, 0x0E, 0xFF).value;
    int16_t y_accel_lsb = i2c_read(ACCELEROMETER, 0x0F, 0xFF).value;
    int16_t y_accel_msb = i2c_read(ACCELEROMETER, 0x10, 0xFF).value;
    int16_t z_accel_lsb = i2c_read(ACCELEROMETER, 0x11, 0xFF).value;
    int16_t z_accel_msb = i2c_read(ACCELEROMETER, 0x12, 0xFF).value;

    // Combine bytes and swap the axis to match the worn orientation
    values.accelerometer.x = y_accel_msb << 8 | y_accel_lsb;
    values.accelerometer.y = z_accel_msb << 8 | z_accel_lsb;
    values.accelerometer.z = x_accel_msb << 8 | x_accel_lsb;

    return values;
}

static int lua_imu_direction(lua_State *L)
{
    imu_values_t values = get_imu_data();

    double roll = atan2((double)values.accelerometer.x,
                        (double)values.accelerometer.z);

    double pitch = atan2((double)values.accelerometer.y,
                         (double)values.accelerometer.z);

    double heading = 0.0; // TODO

    lua_newtable(L);

    lua_pushnumber(L, pitch * (180.0 / PI));
    lua_setfield(L, -2, "pitch");

    lua_pushnumber(L, roll * (180.0 / PI));
    lua_setfield(L, -2, "roll");

    lua_pushnumber(L, heading);
    lua_setfield(L, -2, "heading");

    return 1;
}

static int lua_imu_raw(lua_State *L)
{
    imu_values_t values = get_imu_data();

    lua_newtable(L);

    lua_newtable(L);

    lua_pushnumber(L, values.magnetometer.x);
    lua_setfield(L, -2, "x");

    lua_pushnumber(L, values.magnetometer.y);
    lua_setfield(L, -2, "y");

    lua_pushnumber(L, values.magnetometer.z);
    lua_setfield(L, -2, "z");

    lua_setfield(L, -2, "compass");

    lua_newtable(L);

    lua_pushnumber(L, values.accelerometer.x);
    lua_setfield(L, -2, "x");

    lua_pushnumber(L, values.accelerometer.y);
    lua_setfield(L, -2, "y");

    lua_pushnumber(L, values.accelerometer.z);
    lua_setfield(L, -2, "z");

    lua_setfield(L, -2, "accelerometer");

    return 1;
}

// MC6470 tap configuration. The accelerometer detects taps in hardware; these
// registers decide how forgiving it is. Datasheet sections 9.6 to 9.8.
//
//   0x07 MODE   IAH IPP ... OPCON[1:0]    0xC0 standby, 0xC1 wake
//   0x08 SRTFR  TAP_LATCH | FLIP_* | RATE[3:0]
//   0x09 TAPEN  TAP_EN | THRDUR | per-axis enables
//   0x0A-0x0C   THRDUR=1 -> threshold; THRDUR=0 -> QUIET[7:4] | DUR[3:0]
//               one register per axis: 0x0A X, 0x0B Y, 0x0C Z
//
// Each of 0x0A to 0x0C is two registers in one, selected by THRDUR. Stock
// firmware only ever wrote the threshold, leaving QUIET and DUR at their
// power-on zero: no enforced silence after a tap, so the frame ringing after
// a knock registers as further taps. Writing both halves is the point here.
typedef struct
{
    uint8_t rate;       // RATE[3:0] field value; 0x0A is 256 Hz
    bool latch;         // TAP_LATCH: ignore further taps until 0x03 is read
    uint8_t axes;       // TAPEN bits 5:0, also mirrored into INTEN
    uint8_t threshold[3]; // X, Y, Z: 0x0A to 0x0C while THRDUR = 1
    uint8_t quiet;      // 0..15, high nibble while THRDUR = 0
    uint8_t duration;   // 0..15, low nibble while THRDUR = 0
} tap_settings_t;

static const tap_settings_t tap_defaults = {
    .rate = 0x0A,       // 256 Hz
    .latch = true,
    .axes = 0x08,       // -Y only, which is how the part is mounted
    .threshold = {0x07, 0x07, 0x07},
    .quiet = 0,         // stock values: no quiet period, no minimum duration
    .duration = 0,
};

static tap_settings_t tap_settings = tap_defaults;

static void imu_apply_tap_settings(void)
{
    // Tap configuration only takes effect from standby. The original code
    // did this once at boot and documented it as "IMU must be repowered
    // after changing these settings"; doing the full cycle here is what
    // makes the settings changeable at runtime.
    check_error(i2c_write(ACCELEROMETER, 0x07, 0xFF, 0xC0).fail);

    // Interrupt enables must track the detection axes
    check_error(i2c_write(ACCELEROMETER, 0x06, 0xFF, tap_settings.axes).fail);

    uint8_t srtfr = (tap_settings.latch ? 0x80 : 0x00) |
                    (tap_settings.rate & 0x0F);
    check_error(i2c_write(ACCELEROMETER, 0x08, 0xFF, srtfr).fail);

    // THRDUR = 0: 0x0A-0x0C are quiet (high nibble) and duration (low)
    uint8_t quiet_dur = ((tap_settings.quiet & 0x0F) << 4) |
                        (tap_settings.duration & 0x0F);
    check_error(i2c_write(ACCELEROMETER, 0x09, 0xFF,
                          0x80 | (tap_settings.axes & 0x3F))
                    .fail);
    check_error(i2c_write(ACCELEROMETER, 0x0A, 0xFF, quiet_dur).fail);
    check_error(i2c_write(ACCELEROMETER, 0x0B, 0xFF, quiet_dur).fail);
    check_error(i2c_write(ACCELEROMETER, 0x0C, 0xFF, quiet_dur).fail);

    // THRDUR = 1: the same addresses now hold each axis's threshold
    check_error(i2c_write(ACCELEROMETER, 0x09, 0xFF,
                          0xC0 | (tap_settings.axes & 0x3F))
                    .fail);
    for (uint8_t axis = 0; axis < 3; axis++)
    {
        check_error(i2c_write(ACCELEROMETER, 0x0A + axis, 0xFF,
                              tap_settings.threshold[axis])
                        .fail);
    }

    // Set INTA as push-pull active-high and come out of standby
    check_error(i2c_write(ACCELEROMETER, 0x07, 0xFF, 0xC1).fail);

    // Clear interrupts
    check_error(i2c_read(ACCELEROMETER, 0x03, 0xFF).fail);
}

// Called by shutdown() before System OFF, when a tap is what wakes the
// glasses. The accelerometer sleeps as it was last set, so a harder
// threshold or a long duration from tap_config took many taps to wake them,
// or none did; and a tap still latched (TAP_LATCH holds INTA until 0x03 is
// read) keeps the wake-up line where no new tap can move it. Seen: with the
// stock IMU code the glasses would not wake at all. So the boot defaults go
// back, which clears the latch, and it is cleared once more after the frame
// has stopped ringing from the tap that sent the glasses to sleep.
void imu_prepare_wakeup(void)
{
    nrfx_gpiote_trigger_disable(IMU_INTERRUPT_PIN);

    tap_settings = tap_defaults;
    imu_apply_tap_settings();

    nrfx_systick_delay_ms(300);
    check_error(i2c_read(ACCELEROMETER, 0x03, 0xFF).fail);
}

// Waking takes two taps. The accelerometer has no double-tap detection, and
// a sleeping nRF cannot count, so the first tap wakes the glasses and this
// waits for the second one early in boot, before the FPGA, display or
// Bluetooth are powered. Without it the glasses go straight back to sleep:
// one knock -- a bag, a table, putting them on -- woke them far too often.
//
// The frame rings after a tap, and the wake-up settings have no quiet
// period, so hits in the first WAKE_TAP_MUTE_MS are the same tap ringing.
// Times are from the moment this starts, a few tens of ms after the first
// tap (the bootloader skips its CRC check when waking from System OFF).
#define WAKE_TAP_MUTE_MS 150
#define WAKE_TAP_WINDOW_MS 900

bool imu_woke_by_tap = false;

bool imu_wait_for_second_tap(void)
{
    // The waking tap is still latched; reading 0x03 drops it and re-arms
    check_error(i2c_read(ACCELEROMETER, 0x03, 0xFF).fail);

    for (uint32_t ms = 0; ms < WAKE_TAP_WINDOW_MS; ms += 10)
    {
        nrfx_systick_delay_ms(10);

        i2c_response_t status = i2c_read(ACCELEROMETER, 0x03, 0xFF);
        check_error(status.fail);

        if (status.value && ms >= WAKE_TAP_MUTE_MS)
        {
            return true;
        }
    }

    return false;
}

// Indexed by the RATE[3:0] field. Zero marks a code with no whole-number Hz
// value (0.5 and 0.25 Hz) or a reserved one, and is never selectable.
static const uint16_t tap_rate_hz[16] = {32, 16, 8, 4, 2, 1, 0, 0,
                                         64, 128, 256, 0, 0, 0, 0, 0};

// Whether this boot is the glasses waking to two taps, so the app can carry
// on rather than greet them with a banner. A cold boot, a reset or waking
// in the charging case all return false.
static int lua_imu_woke_by_tap(lua_State *L)
{
    lua_pushboolean(L, imu_woke_by_tap);
    return 1;
}

static const char *const tap_axis_names[3] = {"x", "y", "z"};

static uint8_t tap_check_threshold(lua_State *L, int index)
{
    lua_Integer value = luaL_checkinteger(L, index);
    if (value < 0 || value > 255)
    {
        luaL_error(L, "threshold must be between 0 and 255");
    }
    return (uint8_t)value;
}

// Per-axis table at the top of the stack, {x =, y =, z =}: sets the axes it
// names. Anything else in it, a list or {X = 7}, is an error rather than a
// silent no-op, and so is a table that names no axis.
static void tap_set_thresholds(lua_State *L)
{
    uint8_t threshold[3];
    memcpy(threshold, tap_settings.threshold, sizeof(threshold));
    bool named = false;

    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        uint8_t axis = 0;
        while (axis < 3 && !(lua_type(L, -2) == LUA_TSTRING &&
                             strcmp(lua_tostring(L, -2),
                                    tap_axis_names[axis]) == 0))
        {
            axis++;
        }
        if (axis == 3)
        {
            luaL_error(L, "threshold table takes only x, y and z");
        }
        threshold[axis] = tap_check_threshold(L, -1);
        named = true;
        lua_pop(L, 1);
    }

    if (named == false)
    {
        luaL_error(L, "threshold table names no axis; use x, y or z");
    }
    memcpy(tap_settings.threshold, threshold, sizeof(threshold));
}

static int lua_imu_tap_config(lua_State *L)
{
    // No argument: report what is programmed, so a calibration app can show
    // the current values and round-trip them.
    if (lua_gettop(L) == 0)
    {
        lua_newtable(L);

        // "threshold" stays a plain integer while the three axes agree, so
        // callers that predate per-axis thresholds still round-trip it
        if (tap_settings.threshold[0] == tap_settings.threshold[1] &&
            tap_settings.threshold[1] == tap_settings.threshold[2])
        {
            lua_pushinteger(L, tap_settings.threshold[0]);
            lua_setfield(L, -2, "threshold");
        }

        lua_newtable(L);
        for (uint8_t axis = 0; axis < 3; axis++)
        {
            lua_pushinteger(L, tap_settings.threshold[axis]);
            lua_setfield(L, -2, tap_axis_names[axis]);
        }
        lua_setfield(L, -2, "thresholds");

        lua_pushinteger(L, tap_settings.quiet);
        lua_setfield(L, -2, "quiet");

        lua_pushinteger(L, tap_settings.duration);
        lua_setfield(L, -2, "duration");

        lua_pushboolean(L, tap_settings.latch);
        lua_setfield(L, -2, "latch");

        lua_pushinteger(L, tap_settings.axes);
        lua_setfield(L, -2, "axes");

        lua_pushinteger(L, tap_rate_hz[tap_settings.rate & 0x0F]);
        lua_setfield(L, -2, "rate");

        return 1;
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    // "thresholds" is what the getter returns, so its table round-trips.
    // It goes first: a caller that edits "threshold" in that table and
    // passes it back gets the edit, not the stale per-axis values.
    int thresholds_type = lua_getfield(L, 1, "thresholds");
    if (thresholds_type == LUA_TTABLE)
    {
        tap_set_thresholds(L);
    }
    else if (thresholds_type != LUA_TNIL)
    {
        luaL_error(L, "thresholds must be a table {x =, y =, z =}");
    }
    lua_pop(L, 1);

    // threshold = 7 sets all three axes; threshold = {x = 7, z = 9} sets
    // only the axes it names
    int threshold_type = lua_getfield(L, 1, "threshold");
    if (threshold_type == LUA_TTABLE)
    {
        tap_set_thresholds(L);
    }
    else if (threshold_type != LUA_TNIL)
    {
        uint8_t value = tap_check_threshold(L, -1);
        for (uint8_t axis = 0; axis < 3; axis++)
        {
            tap_settings.threshold[axis] = value;
        }
    }
    lua_pop(L, 1);

    if (lua_getfield(L, 1, "quiet") != LUA_TNIL)
    {
        lua_Integer value = luaL_checkinteger(L, -1);
        if (value < 0 || value > 15)
        {
            luaL_error(L, "quiet must be between 0 and 15");
        }
        tap_settings.quiet = (uint8_t)value;
    }
    lua_pop(L, 1);

    if (lua_getfield(L, 1, "duration") != LUA_TNIL)
    {
        lua_Integer value = luaL_checkinteger(L, -1);
        if (value < 0 || value > 15)
        {
            luaL_error(L, "duration must be between 0 and 15");
        }
        tap_settings.duration = (uint8_t)value;
    }
    lua_pop(L, 1);

    if (lua_getfield(L, 1, "axes") != LUA_TNIL)
    {
        lua_Integer value = luaL_checkinteger(L, -1);
        // Zero would disable tap detection outright. Taps are the only input
        // the glasses have besides orientation, so that is a lockout until
        // the next reset; refuse it rather than offer the foot-gun.
        if (value < 1 || value > 0x3F)
        {
            luaL_error(L, "axes must be between 1 and 0x3F");
        }
        tap_settings.axes = (uint8_t)value;
    }
    lua_pop(L, 1);

    if (lua_getfield(L, 1, "latch") != LUA_TNIL)
    {
        tap_settings.latch = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);

    if (lua_getfield(L, 1, "rate") != LUA_TNIL)
    {
        lua_Integer value = luaL_checkinteger(L, -1);
        bool found = false;

        for (uint8_t code = 0; code < sizeof(tap_rate_hz) / sizeof(uint16_t);
             code++)
        {
            if (tap_rate_hz[code] != 0 && tap_rate_hz[code] == value)
            {
                tap_settings.rate = code;
                found = true;
                break;
            }
        }

        if (found == false)
        {
            luaL_error(L, "rate must be 1, 2, 4, 8, 16, 32, 64, 128 or 256");
        }
    }
    lua_pop(L, 1);

    imu_apply_tap_settings();

    return 0;
}

void lua_open_imu_library(lua_State *L)
{
    // Configure 14bit mode
    check_error(i2c_write(ACCELEROMETER, 0x20, 0xFF, 0x05).fail);

    imu_apply_tap_settings();

    // Configure tap pin interrupt
    nrfx_gpiote_input_config_t input_config = {
        .pull = NRF_GPIO_PIN_NOPULL,
    };

    nrfx_gpiote_trigger_config_t trigger_config = {
        .trigger = NRFX_GPIOTE_TRIGGER_LOTOHI,
        .p_in_channel = NULL,
    };

    nrfx_gpiote_handler_config_t handler_config = {
        .handler = imu_tap_interrupt_handler,
        .p_context = NULL,
    };

    check_error(nrfx_gpiote_input_configure(IMU_INTERRUPT_PIN,
                                            &input_config,
                                            &trigger_config,
                                            &handler_config));

    nrfx_gpiote_trigger_enable(IMU_INTERRUPT_PIN, true);

    // Add functions to the frame lua library
    lua_getglobal(L, "frame");

    lua_newtable(L);

    lua_pushcfunction(L, lua_imu_tap_callback);
    lua_setfield(L, -2, "tap_callback");

    lua_pushcfunction(L, lua_imu_tap_config);
    lua_setfield(L, -2, "tap_config");

    lua_pushcfunction(L, lua_imu_woke_by_tap);
    lua_setfield(L, -2, "woke_by_tap");

    lua_pushcfunction(L, lua_imu_direction);
    lua_setfield(L, -2, "direction");

    lua_pushcfunction(L, lua_imu_raw);
    lua_setfield(L, -2, "raw");

    lua_setfield(L, -2, "imu");

    lua_pop(L, 1);
}