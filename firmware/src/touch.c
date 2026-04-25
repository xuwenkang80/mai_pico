/*
 * Mai Pico Touch Keys
 * WHowe <github.com/whowechina>
 * 
 */

#include "touch.h"

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "bsp/board.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "board_defs.h"

#include "config.h"
#include "mpr121.h"

static uint16_t touch[MPR121_NUM];
static unsigned touch_counts[TOUCH_KEY_NUM];

static uint8_t touch_map[TOUCH_CH_TOTAL] = TOUCH_MAP;

void touch_sensor_init()
{
    for (int m = 0; m < MPR121_NUM; m++) {
        mpr121_init(MPR121_BASE_ADDR + m);
    }
    touch_update_config();
}

void touch_init()
{
    i2c_init(I2C_PORT, I2C_FREQ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    memcpy(touch_map, mai_cfg->alt.touch, sizeof(touch_map));
    touch_sensor_init();
}

const char *touch_key_name(unsigned key)
{
    static char name[3] = { 0 };
    if (key < 18) {
        name[0] = "ABC"[key / 8];
        name[1] = '1' + key % 8;
    } else if (key < TOUCH_KEY_NUM) {
        name[0] = "DE"[(key - 18) / 8];
        name[1] = '1' + (key - 18) % 8;
    } else {
        return "XX";
    }
    return name;
}

int touch_key_by_name(const char *name)
{
    if (strlen(name) != 2) {
        return -1;
    }
    if (strcasecmp(name, "XX") == 0) {
        return 255;
    }

    int zone = toupper(name[0]) - 'A';
    int id = name[1] - '1';
    if ((zone < 0) || (zone > 4) || (id < 0) || (id > 7)) {
        return -1;
    }
    if ((zone == 2) && (id > 1)) {
        return -1; // C1 and C2 only
    }
    const int offsets[] = { 0, 8, 16, 18, 26 };
    return offsets[zone] + id;
}

int touch_key_channel(unsigned key)
{
    for (int i = 0; i < TOUCH_CH_TOTAL; i++) {
        if (touch_map[i] == key) {
            return i;
        }
    }
    return -1;
}

unsigned touch_key_from_channel(unsigned channel)
{
    if (channel < TOUCH_CH_TOTAL) {
        return touch_map[channel];
    }
    return 0xff;
}

void touch_set_map(unsigned channel, unsigned key)
{
    if (channel < TOUCH_CH_TOTAL) {
        touch_map[channel] = key;
        memcpy(mai_cfg->alt.touch, touch_map, sizeof(mai_cfg->alt.touch));
        config_changed();
    }
}

static uint64_t touch_reading;

static void remap_reading()
{
    uint64_t map = 0;

    for (int m = 0; m < MPR121_NUM; m++) {          // 第 m 颗 MPR121
        for (int i = 0; i < MPR121_CH_PER; i++) {  // 第 i 个电极 (0~11)
            if (touch[m] & (1 << i)) {              // 这个电极被判定为 touched
                uint8_t key = touch_map[m * MPR121_CH_PER + i];
                if (key < TOUCH_KEY_NUM) {          // 不是 XX
                    map |= 1ULL << key;
                }
            }
        }
    }

    touch_reading = map;
}


static void touch_stat()
{
    static uint64_t last_reading;

    uint64_t just_touched = touch_reading & ~last_reading;
    last_reading = touch_reading;

    for (int i = 0; i < TOUCH_KEY_NUM; i++) {
        if (just_touched & (1ULL << i)) {
            touch_counts[i]++;
        }
    }
}

void touch_update()
{
    //touch[0] = mpr121_touched(MPR121_BASE_ADDR) & 0x0fff;
    //touch[1] = mpr121_touched(MPR121_BASE_ADDR + 1) & 0x0fff;
    //touch[2] = mpr121_touched(MPR121_BASE_ADDR + 2) & 0x0fff;
    for (int m = 0; m < MPR121_NUM; m++) {
        touch[m] = mpr121_touched(MPR121_BASE_ADDR + m) & 0x0fff;
    }

    remap_reading();

    touch_stat();
}

static bool sensor_ok[MPR121_NUM];
bool touch_sensor_ok(unsigned i)
{
    if (i < MPR121_NUM) {
        return sensor_ok[i];
    }
    return false;
}

const uint16_t *touch_raw()
{
    static uint16_t readout[TOUCH_CH_TOTAL] = {0};
    // Do not use readout as buffer directly, update readout with buffer when operation finishes
    uint16_t buf[TOUCH_CH_TOTAL] = {0};

    for (int i = 0; i < MPR121_NUM; i++) {
        sensor_ok[i] = mpr121_raw(MPR121_BASE_ADDR + i,
                                  buf + i * MPR121_CH_PER,
                                  MPR121_CH_PER);
    }
    memcpy(readout, buf, sizeof(readout));

    return readout;
}

const uint16_t *map_raw_to_zones(const uint16_t* raw)
{
    static uint16_t zones[TOUCH_KEY_NUM];

    memset(zones, 0, sizeof(zones));
    for (int i = 0; i < TOUCH_CH_TOTAL; i++) {
        uint8_t key = touch_map[i];
        if (key < TOUCH_KEY_NUM) {
            zones[key] = raw[i];
        }
    }
    
    return zones;
}

const int8_t *unmap_sense_to_raw(const int8_t* memsense)
{
    static int8_t outsense[TOUCH_CH_TOTAL];
    for(int i = 0; i < TOUCH_CH_TOTAL; i++) {
        uint8_t key = touch_map[i];
        outsense[i] = key < TOUCH_KEY_NUM ? memsense[key] : 0;
    }
    return outsense;
}

bool touch_touched(unsigned key)
{
    if (key >= TOUCH_KEY_NUM) {
        return 0;
    }
    return touch_reading & (1ULL << key);
}

uint64_t touch_touchmap()
{
    return touch_reading;
}

unsigned touch_count(unsigned key)
{
    if (key >= TOUCH_KEY_NUM) {
        return 0;
    }
    return touch_counts[key];
}

void touch_reset_stat()
{
    memset(touch_counts, 0, sizeof(touch_counts));
}

void touch_update_config()
{
    const int8_t* outsense = unmap_sense_to_raw((const int8_t*)mai_cfg->sense.zones);
    for (int m = 0; m < MPR121_NUM; m++) {
        mpr121_debounce(MPR121_BASE_ADDR + m,
                        mai_cfg->sense.debounce_touch,
                        mai_cfg->sense.debounce_release);
        mpr121_sense(MPR121_BASE_ADDR + m,
                     mai_cfg->sense.global,
                     (int8_t*)outsense + m * MPR121_CH_PER,
                     MPR121_CH_PER);
        mpr121_filter(MPR121_BASE_ADDR + m,
                      mai_cfg->sense.filter >> 6,
                      (mai_cfg->sense.filter >> 4) & 0x03,
                      mai_cfg->sense.filter & 0x07);
    }
}
