/**
 * \file            ow.c
 * \brief           OneWire protocol implementation
 */

/*
 * Copyright (c) 2020 Tilen MAJERLE
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * This file is part of OneWire-UART library.
 *
 * Author:          Tilen MAJERLE <tilen@majerle.eu>
 * Version:         v2.0.0
 */
#include <string.h>
#include "ow/ow.h"

#if !__DOXYGEN__

/* Internal macros */
#define OW_FIRST_DEV                    0xFF
#define OW_LAST_DEV                     0x00

#define OW_RESET_BYTE                   0xF0

#endif /* !__DOXYGEN__ */

/* Set value if not NULL */
#define SET_NOT_NULL(p, v)          if ((p) != NULL) { *(p) = (v); }

/**
 * \brief           Send single bit to OneWire port
 * \param[in]       ow: OneWire instance
 * \param[in]       btw: Bit to send, either `1` or `0`
 * \param[out]      btr: Pointer to output variable to write read bit
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
static owr_t
send_bit(ow_t* const ow, uint8_t btw, uint8_t* btr) {
    uint8_t b;

    SET_NOT_NULL(btr, 0);

    /*
     * To send logical 1 over 1-wire, send 0xFF over UART
     * To send logical 0 over 1-wire, send 0x00 over UART
     */
    btw = btw > 0 ? 0xFF : 0x00;                /* Convert to 0 or 1 */
    if (!ow->ll_drv->tx_rx(&btw, &b, 1, ow->arg)) {
        return owERRTXRX;                       /* Transmit error */
    }
    b = b == 0xFF ? 1 : 0;                      /* Go to bit values */
    SET_NOT_NULL(btr, b);                       /* Set new byte */
    return owOK;
}

/**
 * \brief           Initialize OneWire instance
 * \param[in]       ow: OneWire instance
 * \param[in]       ll_drv: Low-level driver
 * \param[in]       arg: Custom argument
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_init(ow_t* const ow, const ow_ll_drv_t* const ll_drv, void* arg) {
    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("ll_drv != NULL", ll_drv != NULL);
    OW_ASSERT("ll_drv->init != NULL", ll_drv->init != NULL);
    OW_ASSERT("ll_drv->deinit != NULL", ll_drv->deinit != NULL);
    OW_ASSERT("ll_drv->set_baudrate != NULL", ll_drv->set_baudrate != NULL);
    OW_ASSERT("ll_drv->tx_rx != NULL", ll_drv->tx_rx != NULL);

    ow->arg = arg;
    ow->ll_drv = ll_drv;                        /* Assign low-level driver */
    if (!ow->ll_drv->init(ow->arg)) {           /* Init low-level directly */
        return owERR;
    }
#if OW_CFG_OS
    if (!ow_sys_mutex_create(&ow->mutex, arg)) {
        ow->ll_drv->deinit(ow->arg);            /* Deinit low-level */
        return owERR;
    }
#endif /* OW_CFG_OS */
    return owOK;
}

/**
 * \brief           Deinitialize OneWire instance
 * \param[in]       ow: OneWire instance
 */
void
ow_deinit(ow_t* const ow) {
    if (ow == NULL || ow->ll_drv == NULL) {
        return;
    }

#if OW_CFG_OS
    ow_sys_mutex_delete(&ow->mutex, ow->arg);
#endif /* OW_CFG_OS */
    ow->ll_drv->deinit(ow->arg);
}

/**
 * \brief           Protect 1-wire from concurrent access
 * \note            Used only for OS systems
 * \param[in,out]   ow: 1-Wire handle
 * \param[in]       protect: Set to `1` to protect core, `0` otherwise
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_protect(ow_t* const ow, const uint8_t protect) {
    OW_ASSERT("ow != NULL", ow != NULL);

#if OW_CFG_OS
    if (protect && !ow_sys_mutex_wait(&ow->mutex, ow->arg)) {
        return owERR;
    }
#else
    (void)ow;
    (void)protect;
#endif /* OW_CFG_OS */
    return owOK;
}

/**
 * \brief           Unprotect 1-wire from concurrent access
 * \note            Used only for OS systems
 * \param[in,out]   ow: 1-Wire handle
 * \param[in]       protect: Set to `1` to protect core, `0` otherwise
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_unprotect(ow_t* const ow, const uint8_t protect) {
    OW_ASSERT("ow != NULL", ow != NULL);

#if OW_CFG_OS
    if (protect && !ow_sys_mutex_release(&ow->mutex, ow->arg)) {
        return owERR;
    }
#else
    (void)ow;
    (void)protect;
#endif /* OW_CFG_OS */
    return owOK;
}

/**
 * \brief           Reset 1-Wire bus and set connected devices to idle state
 * \param[in,out]   ow: 1-Wire handle
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_reset_raw(ow_t* const ow) {
    uint8_t b;

    OW_ASSERT("ow != NULL", ow != NULL);

    /* First send reset pulse */
    b = OW_RESET_BYTE;                          /* Set reset sequence byte = 0xF0 */
    if (!ow->ll_drv->set_baudrate(9600, ow->arg)) {
        return owERRBAUD;                       /* Error setting baudrate */
    }
    if (!ow->ll_drv->tx_rx(&b, &b, 1, ow->arg)) {
        return owERRTXRX;                       /* Error with data exchange */
    }
    if (!ow->ll_drv->set_baudrate(115200, ow->arg)) {
        return owERRBAUD;                       /* Error setting baudrate */
    }

    /* Check if there is reply from any device */
    if (b == 0 || b == OW_RESET_BYTE) {
        return owERRPRESENCE;
    }
    return owOK;
}

/**
 * \copydoc         ow_reset_raw
 * \note            This function is thread-safe
 */
owr_t
ow_reset(ow_t* const ow) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);

    ow_protect(ow, 1);
    res = ow_reset_raw(ow);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Write byte over OW and read its response
 * \param[in,out]   ow: 1-Wire handle
 * \param[in]       btw: Byte to write
 * \param[out]      br: Pointer to read value. Set to `NULL` if not used
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_write_byte_ex_raw(ow_t* const ow, const uint8_t btw, uint8_t* const br) {
    uint8_t tr[8];

    OW_ASSERT("ow != NULL", ow != NULL);
    SET_NOT_NULL(br, 0);

    /* Prepare output data */
    for (uint8_t i = 0; i < 8; ++i) {
        /*
         * If we have to send high bit, set byte as 0xFF,
         * otherwise set it as low bit, 0x00
         */
        tr[i] = (btw & (1 << i)) ? 0xFF : 0x00;
    }

    /*
     * Exchange data on UART level,
     * send single byte for each bit = 8 bytes
     */
    if (!ow->ll_drv->tx_rx(tr, tr, 8, ow->arg)) {
        return owERRTXRX;
    }

    /* Update output value */
    if (br != NULL) {
        uint8_t r = 0;
        /*
         * Check received data. If we read 0xFF,
         * our logical write 1 was successful, otherwise it was 0.
         */
        for (uint8_t i = 0; i < 8; ++i) {
            if (tr[i] == 0xFF) {
                r |= 0x01 << i;
            }
        }
        *br = r;
    }
    return owOK;
}

/**
 * \copydoc         ow_write_byte_ex_raw
 * \note            This function is thread-safe
 */
owr_t
ow_write_byte_ex(ow_t* const ow, const uint8_t btw, uint8_t* const br) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);

    ow_protect(ow, 1);
    res = ow_write_byte_ex_raw(ow, btw, br);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Read byte from OW device
 * \param[in,out]   ow: 1-Wire handle
 * \param[out]      br: Pointer to save read value
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_read_byte_ex_raw(ow_t* const ow, uint8_t* const br) {
    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("br != NULL", br != NULL);

    /*
     * When we want to read byte over 1-Wire,
     * we have to send all bits as 1 and check if slave pulls line down.
     *
     * According to slave reactions, we can later construct received bytes
     */
    return ow_write_byte_ex_raw(ow, 0xFF, br);
}

/**
 * \copydoc         ow_read_byte_ex_raw
 * \note            This function is thread-safe
 */
owr_t
ow_read_byte_ex(ow_t* const ow, uint8_t* const br) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("br != NULL", br != NULL);

    ow_protect(ow, 1);
    res = ow_read_byte_ex_raw(ow, br);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Read sinle bit from OW device
 * \param[in,out]   ow: 1-Wire handle
 * \param[out]      br: Pointer to save read value, either `1` or `0`
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_read_bit_ex_raw(ow_t* const ow, uint8_t* const br) {
    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("br != NULL", br != NULL);

    return send_bit(ow, 1, br);                 /* Send bit as `1` and read the response */
}

/**
 * \copydoc         ow_write_byte_ex_raw
 * \note            This function is thread-safe
 */
owr_t
ow_read_bit_ex(ow_t* const ow, uint8_t* const br) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("br != NULL", br != NULL);

    ow_protect(ow, 1);
    res = ow_read_bit_ex_raw(ow, br);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Reset search
 * \param[in,out]   ow: 1-Wire handle
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_search_reset_raw(ow_t* const ow) {
    OW_ASSERT("ow != NULL", ow != NULL);

    ow->disrepancy = OW_FIRST_DEV;              /* Reset disrepancy to default value */
    return owOK;
}

/**
 * \copydoc         ow_search_reset_raw
 * \note            This function is thread-safe
 */
owr_t
ow_search_reset(ow_t* const ow) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);

    ow_protect(ow, 1);
    res = ow_search_reset_raw(ow);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Search for devices on 1-wire bus
 * \note            To reset search and to start over, use \ref ow_search_reset function
 * \param[in,out]   ow: 1-Wire handle
 * \param[out]      rom_id: Pointer to ROM structure to save ROM
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_search_raw(ow_t* const ow, ow_rom_t* const rom_id) {
    OW_ASSERT("ow != NULL", ow != NULL);
    return ow_search_with_command_raw(ow, OW_CMD_SEARCHROM, rom_id);
}

/**
 * \copydoc         ow_search_raw
 * \note            This function is thread-safe
 */
owr_t
ow_search(ow_t* const ow, ow_rom_t* const rom_id) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);

    ow_protect(ow, 1);
    res = ow_search_raw(ow, rom_id);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Search for devices on 1-wire bus with custom search command
 * \note            To reset search and to start over, use \ref ow_search_reset function
 * \param[in,out]   ow: 1-Wire handle
 * \param[in]       cmd: command to use for search operation
 * \param[out]      rom_id: Pointer to ROM structure to store address
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_search_with_command_raw(ow_t* const ow, const uint8_t cmd, ow_rom_t* const rom_id) {
    owr_t res;
    uint8_t id_bit_number, next_disrepancy, *id;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id != NULL", rom_id != NULL);

    id = ow->rom.rom;

    /* Check for last device */
    if (ow->disrepancy == 0) {
        ow_search_reset_raw(ow);                /* Reset search for next search */
        return owERRNODEV;                      /* No devices anymore */
    }

    /* Step 1: Reset all devices on 1-Wire line to be able to listen for new command */
    if ((res = ow_reset_raw(ow)) != owOK) {
        return res;
    }

    /* Step 2: Send search rom command for all devices on 1-Wire */
    ow_write_byte_ex_raw(ow, cmd, NULL);        /* Start with search ROM command */
    next_disrepancy = OW_LAST_DEV;              /* This is currently last device */

    for (id_bit_number = 64; id_bit_number > 0;) {
        uint8_t b, b_cpl;
        for (uint8_t j = 8; j > 0; --j, --id_bit_number) {
            /* Read first bit and its complimentary one */
            if (send_bit(ow, 1, &b) != owOK || send_bit(ow, 1, &b_cpl) != owOK) {
                return owERRTXRX;
            }

            /*
             * If we have connected many devices on 1-Wire port, b and b_cpl are ANDed between all devices.
             *
             * We have to react if b and b_cpl are the same:
             *
             *  - Both 1: No devices on 1-Wire line responded
             *      - No device connected at all
             *      - All devices were put to block state due to search
             *  - Both 0: We have "collision" as device with bit 0 and bit 1 are connected
             *
             * If b and b_cpl are different, it means we have:
             *
             *  - Single device connected on 1-Wire or
             *  - All devices on 1-Wire have the same bit value at current position
             *      - In this case, we move to direction of b value
             */
            if (b && b_cpl) {
                goto out;                       /* We do not have device connected */
            } else if (!b && !b_cpl) {
                /*
                 * Decide which way to go for next scan
                 *
                 * Force move to "1" in case of:
                 *
                 *  - known diff position is larger than current bit reading
                 *  - Previous ROM address bit 0 was 1 and known diff is different than reading
                 *
                 * Because we shift *id variable down by 1 bit every iteration,
                 * *id & 0x01 always returns 1 if bit on previous ROM is the same as current bit
                 */
                if (id_bit_number < ow->disrepancy || ((*id & 0x01) && ow->disrepancy != id_bit_number)) {
                    b = 1;
                    next_disrepancy = id_bit_number;
                }
            }

            /*
             * Devices are expecting master will send bit value back.
             * All devices which do not have this bit value
             * will go to blocked state and will wait for next reset sequence
             *
             * In case of "collision", we decide here which devices we will
             * continue to scan (binary tree)
             */
            send_bit(ow, b, NULL);              /* Send bit you want to continue with */

            /*
             * Because we shift down *id each iteration, we have to position bit value to the MSB position
             * and it will be automatically positioned correct way.
             */
            *id = (*id >> 0x01) | (b << 0x07);  /* Shift ROM byte down and add next, protocol is LSB first */
        }
        ++id;                                   /* Go to next byte */
    }
out:
    ow->disrepancy = next_disrepancy;           /* Save disrepancy value */
    memcpy(rom_id->rom, ow->rom.rom, sizeof(ow->rom.rom));  /* Copy ROM to user memory */
    return id_bit_number == 0 ? owOK : owERRNODEV;  /* Return search result status */
}

/**
 * \copydoc         ow_search_with_command_raw
 * \note            This function is thread-safe
 */
owr_t
ow_search_with_command(ow_t* const ow, const uint8_t cmd, ow_rom_t* const rom_id) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id != NULL", rom_id != NULL);

    ow_protect(ow, 1);
    res = ow_search_with_command_raw(ow, cmd, rom_id);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Select device on 1-wire network with exact ROM number
 * \param[in]       ow: 1-Wire handle
 * \param[in]       rom_id: 1-Wire device address to match device
 * \return          \ref owrOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_match_rom_raw(ow_t* const ow, const ow_rom_t* const rom_id) {
    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id != NULL", rom_id != NULL);

    /* Write byte to match rom exactly */
    if (ow_write_byte_ex_raw(ow, OW_CMD_MATCHROM, NULL) != owOK) {
        return owERR;
    }
    for (uint8_t i = 0; i < 8; ++i) {           /* Send 8 bytes representing ROM address */
        if (ow_write_byte_ex_raw(ow, rom_id->rom[i], NULL) != owOK) {   /* Send ROM bytes */
            return owERR;
        }
    }

    return owOK;
}

/**
 * \copydoc         ow_match_rom_raw
 * \note            This function is thread-safe
 */
owr_t
ow_match_rom(ow_t* const ow, const ow_rom_t* const rom_id) {
    uint8_t res;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id != NULL", rom_id != NULL);

    ow_protect(ow, 1);
    res = ow_match_rom_raw(ow, rom_id);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Skip ROM address and select all devices on the network
 * \param[in]       ow: 1-Wire handle
 * \return          \ref owrOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_skip_rom_raw(ow_t* const ow) {
    OW_ASSERT("ow != NULL", ow != NULL);

    return ow_write_byte_ex_raw(ow, OW_CMD_SKIPROM, NULL);  /* Write byte to match rom exactly */
}

/**
 * \copydoc         ow_skip_rom_raw
 * \note            This function is thread-safe
 */
owr_t
ow_skip_rom(ow_t* const ow) {
    uint8_t res;

    OW_ASSERT("ow != NULL", ow != NULL);

    ow_protect(ow, 1);
    res = ow_skip_rom_raw(ow);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Calculate CRC-8 of input data
 * \param[in]       in: Input data
 * \param[in]       len: Number of bytes
 * \return          Calculated CRC
 * \note            This function is reentrant
 */
uint8_t
ow_crc(const void* in, const size_t len) {
    size_t i;
    uint8_t crc = 0, inbyte, mix;
    const uint8_t* d = in;

    if (in == NULL || len == 0) {
        return 0;
    }

    for (i = 0; i < len; ++i, ++d) {
        inbyte = *d;
        for (uint8_t i = 8; i > 0; --i) {
            mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix > 0) {
                crc ^= 0x8C;
            }
            inbyte >>= 0x01;
        }
    }
    return crc;
}

/**
 * \brief           Search devices on 1-wire network by using callback function and custom search command
 *
 * When new device is detected, callback function `func` is called to notify user
 *
 * \param[in]       ow: 1-Wire handle
 * \param[in]       cmd: 1-Wire search command
 * \param[out]      roms_found: Output variable to save number of found devices. Set to `NULL` if not used
 * \param[in]       func: Callback function to call for each device
 * \param[in]       arg: Custom user argument, used in callback function
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 * \note            This function is thread-safe
 */
owr_t
ow_search_with_command_callback(ow_t* const ow, const uint8_t cmd, size_t* const roms_found,
                                    const ow_search_cb_fn func, void* arg) {
    owr_t res;
    ow_rom_t rom_id;
    size_t i;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("func != NULL", func != NULL);

    ow_protect(ow, 1);
    /* Search device-by-device until all found */
    for (i = 0, res = ow_search_reset_raw(ow);
        res == owOK && (res = ow_search_with_command_raw(ow, cmd, &rom_id)) == owOK; ++i) {
        if ((res = func(ow, &rom_id, i, arg)) != owOK) {
            break;
        }
    }
    func(ow, NULL, i, arg);                     /* Call with NULL rom_id parameter */
    ow_unprotect(ow, 1);

    if (roms_found != NULL) {
        *roms_found = i;
    }
    if (res == owERRNODEV) {                    /* `No device` might not be an error, but simply no devices on bus */
        res = owOK;
    }
    return res;
}

/**
 * \brief           Search devices on 1-wire network by using callback function and `SEARCH_ROM` 1-Wire command
 *
 * When new device is detected, callback function `func` is called to notify user
 *
 * \param[in]       ow: 1-Wire handle
 * \param[out]      roms_found: Output variable to save number of found devices. Set to `NULL` if not used
 * \param[in]       func: Callback function to call for each device
 * \param[in]       arg: Custom user argument, used in callback function
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 * \note            This function is thread-safe
 */
owr_t
ow_search_with_callback(ow_t* const ow, size_t* const roms_found, const ow_search_cb_fn func, void* arg) {
    return ow_search_with_command_callback(ow, OW_CMD_SEARCHROM, roms_found, func, arg);
}

/**
 * \brief           Search for devices on 1-Wire network with command and store ROM IDs to input array
 * \param[in]       ow: 1-Wire handle
 * \param[in]       cmd: 1-Wire search command
 * \param[in]       rom_id_arr: Pointer to output array to store found ROM IDs into
 * \param[in]       rom_len: Length of input ROM array
 * \param[out]      roms_found: Output variable to save number of found devices. Set to `NULL` if not used
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_search_devices_with_command_raw(ow_t* const ow, const uint8_t cmd, ow_rom_t* const rom_id_arr,
                                    const size_t rom_len, size_t* const roms_found) {
    owr_t res;
    size_t cnt = 0;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id_arr != NULL", rom_id_arr != NULL);
    OW_ASSERT("rom_len > 0", rom_len > 0);

    for (cnt = 0, res = ow_search_reset_raw(ow); cnt < rom_len; ++cnt) {
        if ((res = ow_search_with_command_raw(ow, cmd, &rom_id_arr[cnt])) != owOK) {
            break;
        }
    }
    if (roms_found != NULL) {
        *roms_found = cnt;
    }
    if (res == owERRNODEV && cnt > 0) {
        res = owOK;
    }
    return res;
}

/**
 * \copydoc         ow_search_devices_with_command_raw
 * \note            This function is thread-safe
 */
owr_t
ow_search_devices_with_command(ow_t* const ow, const uint8_t cmd, ow_rom_t* const rom_id_arr,
                                const size_t rom_len, size_t* const roms_found) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id_arr != NULL", rom_id_arr != NULL);
    OW_ASSERT("rom_len > 0", rom_len > 0);

    ow_protect(ow, 1);
    res = ow_search_devices_with_command_raw(ow, cmd, rom_id_arr, rom_len, roms_found);
    ow_unprotect(ow, 1);
    return res;
}

/**
 * \brief           Search for devices on 1-Wire network with default command and store ROM IDs to input array
 * \param[in]       ow: 1-Wire handle
 * \param[in]       rom_id_arr: Pointer to output array to store found ROM IDs into
 * \param[in]       rom_len: Length of input ROM array
 * \param[out]      roms_found: Output variable to save number of found devices. Set to `NULL` if not used
 * \return          \ref owOK on success, member of \ref owr_t otherwise
 */
owr_t
ow_search_devices_raw(ow_t* const ow, ow_rom_t* const rom_id_arr, const size_t rom_len, size_t* const roms_found) {
    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id_arr != NULL", rom_id_arr != NULL);
    OW_ASSERT("rom_len > 0", rom_len > 0);

    return ow_search_devices_with_command_raw(ow, OW_CMD_SEARCHROM, rom_id_arr, rom_len, roms_found);
}

/**
 * \copydoc         ow_search_devices_raw
 * \note            This function is thread-safe
 */
owr_t
ow_search_devices(ow_t* const ow, ow_rom_t* const rom_id_arr, const size_t rom_len, size_t* const roms_found) {
    owr_t res;

    OW_ASSERT("ow != NULL", ow != NULL);
    OW_ASSERT("rom_id_arr != NULL", rom_id_arr != NULL);
    OW_ASSERT("rom_len > 0", rom_len > 0);

    ow_protect(ow, 1);
    res = ow_search_devices_raw(ow, rom_id_arr, rom_len, roms_found);
    ow_unprotect(ow, 1);
    return res;
}

/* Deprecated functions list */

/**
 * \brief           Write byte over 1-wire protocol
 * \note            This function is deprecated. Use \ref ow_write_byte_ex_raw instead
 * \deprecated      This function is deprecated. Use \ref ow_write_byte_ex_raw instead
 * \param[in,out]   ow: 1-Wire handle
 * \param[in]       b: Byte to write
 * \return          Received byte over 1-wire protocol
 */
uint8_t
ow_write_byte_raw(ow_t* const ow, const uint8_t b) {
    uint8_t r;
    return ow_write_byte_ex_raw(ow, b, &r) == owOK ? r : 0x00;
}

/**
 * \copydoc         ow_write_byte_raw
 * \note            This function is deprecated. Use \ref ow_write_byte_ex instead
 * \deprecated      This function is deprecated. Use \ref ow_write_byte_ex instead
 * \note            This function is thread-safe
 */
uint8_t
ow_write_byte(ow_t* const ow, const uint8_t b) {
    uint8_t r;
    return ow_write_byte_ex(ow, b, &r) == owOK ? r : 0x00;
}

/**
 * \brief           Read next byte on 1-Wire
 * \note            This function is deprecated. Use \ref ow_read_byte_ex_raw instead
 * \deprecated      This function is deprecated. Use \ref ow_read_byte_ex_raw instead
 * \param[in,out]   ow: 1-Wire handle
 * \return          Byte read over 1-Wire
 */
uint8_t
ow_read_byte_raw(ow_t* const ow) {
    uint8_t br;
    return ow_read_byte_ex_raw(ow, &br) == owOK ? br : 0x00;
}

/**
 * \copydoc         ow_read_byte_raw
 * \note            This function is deprecated. Use \ref ow_read_byte_ex instead
 * \deprecated      This function is deprecated. Use \ref ow_read_byte_ex instead
 * \note            This function is thread-safe
 */
uint8_t
ow_read_byte(ow_t* const ow) {
    uint8_t br;
    return ow_read_byte_ex(ow, &br) == owOK ? br : 0x00;
}

/**
 * \brief           Read single bit on 1-Wire network
 * \note            This function is deprecated. Use \ref ow_read_bit_ex_raw instead
 * \deprecated      This function is deprecated. Use \ref ow_read_bit_ex_raw instead
 * \param[in,out]   ow: 1-Wire handle
 * \return          Bit value
 */
uint8_t
ow_read_bit_raw(ow_t* const ow) {
    uint8_t br;
    return ow_read_bit_ex_raw(ow, &br) == owOK ? br : 0x00;
}

/**
 * \copydoc         ow_read_bit_raw
 * \note            This function is deprecated. Use \ref ow_read_bit_ex instead
 * \deprecated      This function is deprecated. Use \ref ow_read_bit_ex instead
 * \note            This function is thread-safe
 */
uint8_t
ow_read_bit(ow_t* const ow) {
    uint8_t br;
    return ow_read_bit_ex(ow, &br) == owOK ? br : 0x00;
}
