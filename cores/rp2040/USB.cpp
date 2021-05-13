/*
 * Shared USB for the Raspberry Pi Pico RP2040
 * Allows for multiple endpoints to share the USB controller
 *
 * Copyright (c) 2021 Earle F. Philhower, III <earlephilhower@yahoo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <Arduino.h>
#include "CoreMutex.h"

#include "tusb.h"
#include "class/hid/hid_device.h"
#include "class/audio/audio.h"
#include "class/midi/midi.h"
#include "pico/time.h"
#include "pico/binary_info.h"
#include "pico/bootrom.h"
#include "hardware/irq.h"
#include "pico/mutex.h"
#include "hardware/watchdog.h"
#include "pico/unique_id.h"

// Weak function definitions for each type of endpoint
extern void __USBInstallSerial() __attribute__((weak));
extern void __USBInstallKeyboard() __attribute__((weak));
extern void __USBInstallMouse() __attribute__((weak));
extern void __USBInstallMIDI() __attribute__((weak));

#define PICO_STDIO_USB_TASK_INTERVAL_US 1000
#define PICO_STDIO_USB_LOW_PRIORITY_IRQ 31

#define USBD_VID (0x2E8A) // Raspberry Pi

#ifdef SERIALUSB_PID
    #define USBD_PID (SERIALUSB_PID)
#else
    #define USBD_PID (0x000a) // Raspberry Pi Pico SDK CDC
#endif

#define USBD_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)
#define USBD_MAX_POWER_MA (250)

#define USBD_ITF_CDC (0) // needs 2 interfaces
#define USBD_ITF_MAX (2)

#define USBD_CDC_EP_CMD (0x81)
#define USBD_CDC_EP_OUT (0x02)
#define USBD_CDC_EP_IN (0x82)
#define USBD_CDC_CMD_MAX_SIZE (8)
#define USBD_CDC_IN_OUT_MAX_SIZE (64)

#define USBD_STR_0 (0x00)
#define USBD_STR_MANUF (0x01)
#define USBD_STR_PRODUCT (0x02)
#define USBD_STR_SERIAL (0x03)
#define USBD_STR_CDC (0x04)


#define EPNUM_HID   0x83


  #define EPNUM_MIDI   0x01


static char _idString[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const usbd_desc_str[] = {
    [USBD_STR_0] = "",
    [USBD_STR_MANUF] = "Raspberry Pi",
    [USBD_STR_PRODUCT] = "PicoArduino",
    [USBD_STR_SERIAL] = _idString,
    [USBD_STR_CDC] = "Board CDC",
};

extern "C" const uint8_t *tud_descriptor_device_cb(void) {
    static tusb_desc_device_t usbd_desc_device = {
        .bLength = sizeof(tusb_desc_device_t),
        .bDescriptorType = TUSB_DESC_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = TUSB_CLASS_CDC,
        .bDeviceSubClass = MISC_SUBCLASS_COMMON,
        .bDeviceProtocol = MISC_PROTOCOL_IAD,
        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .idVendor = USBD_VID,
        .idProduct = USBD_PID,
        .bcdDevice = 0x0100,
        .iManufacturer = USBD_STR_MANUF,
        .iProduct = USBD_STR_PRODUCT,
        .iSerialNumber = USBD_STR_SERIAL,
        .bNumConfigurations = 1
    };
    if (__USBInstallSerial && !__USBInstallKeyboard && !__USBInstallMouse && !__USBInstallMIDI) {
        // Can use as-is, this is the default USB case
        return (const uint8_t *)&usbd_desc_device;
    }
    // Need a multi-endpoint config which will require changing the PID to help Windows not barf
    if (__USBInstallKeyboard) {
        usbd_desc_device.idProduct |= 0x8000;
    }
    if (__USBInstallMouse) {
        usbd_desc_device.idProduct |= 0x4000;
    }
    if (__USBInstallMIDI) {
        usbd_desc_device.idProduct |= 0x2000;
    }
    // Set the device class to 0 to indicate multiple device classes
    usbd_desc_device.bDeviceClass = 0;
    usbd_desc_device.bDeviceSubClass = 0;
    usbd_desc_device.bDeviceProtocol = 0;
    return (const uint8_t *)&usbd_desc_device;
}

int __GetMouseReportID() {
    return __USBInstallKeyboard ? 2 : 1;
}

static uint8_t *GetDescHIDReport(int *len) {
    if (__USBInstallKeyboard && __USBInstallMouse) {
        static uint8_t desc_hid_report[] = {
            TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(1) ),
            TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(2) )
        };
        if (len) {
            *len = sizeof(desc_hid_report);
        }
        return desc_hid_report;
    } else if (__USBInstallKeyboard && ! __USBInstallMouse) {
        static uint8_t desc_hid_report[] = {
            TUD_HID_REPORT_DESC_KEYBOARD( HID_REPORT_ID(1) )
        };
        if (len) {
            *len = sizeof(desc_hid_report);
        }
        return desc_hid_report;
    } else { // if (!__USBInstallKeyboard && __USBInstallMouse) {
        static uint8_t desc_hid_report[] = {
            TUD_HID_REPORT_DESC_MOUSE( HID_REPORT_ID(1) )
        };
        if (len) {
            *len = sizeof(desc_hid_report);
        }
        return desc_hid_report;
    }
}

// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
extern "C" uint8_t const * tud_hid_descriptor_report_cb(void)
{
    return GetDescHIDReport(nullptr);
}   


const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    int len = 0;
    static uint8_t *usbd_desc_cfg;
    
    if (!usbd_desc_cfg) {
        bool hasHID = __USBInstallKeyboard || __USBInstallMouse;

        uint8_t interface_count = (__USBInstallSerial ? 2 : 0) + (hasHID ? 1 : 0) + (__USBInstallMIDI ? 2 : 0);

        static uint8_t cdc_desc[TUD_CDC_DESC_LEN] = {
            // Interface number, string index, protocol, report descriptor len, EP In & Out address, size & polling interval
            TUD_CDC_DESCRIPTOR(USBD_ITF_CDC, USBD_STR_CDC, USBD_CDC_EP_CMD, USBD_CDC_CMD_MAX_SIZE, USBD_CDC_EP_OUT, USBD_CDC_EP_IN, USBD_CDC_IN_OUT_MAX_SIZE)
        };

        int hid_report_len;
        GetDescHIDReport(&hid_report_len);
        uint8_t hid_itf = __USBInstallSerial ? 2 : 0;
        static uint8_t hid_desc[TUD_HID_DESC_LEN] = {
            // Interface number, string index, protocol, report descriptor len, EP In & Out address, size & polling interval
            TUD_HID_DESCRIPTOR(hid_itf, 0, HID_PROTOCOL_NONE, hid_report_len, EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10)
        };

        uint8_t midi_itf = hid_itf + (hasHID ? 1 : 0);
        static uint8_t midi_desc[TUD_MIDI_DESC_LEN] = {
            // Interface number, string index, EP Out & EP In address, EP size
            TUD_MIDI_DESCRIPTOR(midi_itf, 0, EPNUM_MIDI, 0x80 | EPNUM_MIDI, 64)
        };

        int usbd_desc_len = TUD_CONFIG_DESC_LEN + (__USBInstallSerial ? sizeof(cdc_desc) : 0) + (hasHID ? sizeof(hid_desc) : 0) + (__USBInstallMIDI ? sizeof(midi_desc) : 0);

        static uint8_t tud_cfg_desc[TUD_CONFIG_DESC_LEN] = {
            // Config number, interface count, string index, total length, attribute, power in mA
            TUD_CONFIG_DESCRIPTOR(1, interface_count, USBD_STR_0, usbd_desc_len, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, USBD_MAX_POWER_MA)
        };

        // Combine to one descriptor
        usbd_desc_cfg = (uint8_t *)malloc(usbd_desc_len);
        bzero(usbd_desc_cfg, usbd_desc_len);
        uint8_t *ptr = usbd_desc_cfg;
        memcpy(ptr, tud_cfg_desc, sizeof(tud_cfg_desc));
        ptr += sizeof(tud_cfg_desc);
        if (__USBInstallSerial) {
            memcpy(ptr, cdc_desc, sizeof(cdc_desc));
            ptr += sizeof(cdc_desc);
        }
        if (hasHID) {
            memcpy(ptr, hid_desc, sizeof(hid_desc));
            ptr += sizeof(hid_desc);
        }
        if (__USBInstallMIDI) {
            memcpy(ptr, midi_desc, sizeof(midi_desc));
        }
    }
    return usbd_desc_cfg;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    #define DESC_STR_MAX (20)
    static uint16_t desc_str[DESC_STR_MAX];

    uint8_t len;
    if (index == 0) {
        desc_str[1] = 0x0409; // supported language is English
        len = 1;
    } else {
        if (index >= sizeof(usbd_desc_str) / sizeof(usbd_desc_str[0])) {
            return NULL;
        }
        const char *str = usbd_desc_str[index];
        for (len = 0; len < DESC_STR_MAX - 1 && str[len]; ++len) {
            desc_str[1 + len] = str[len];
        }
    }

    // first byte is length (including header), second byte is string type
    desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * len + 2);

    return desc_str;
}

mutex_t __usb_mutex;

static void low_priority_worker_irq() {
    // if the mutex is already owned, then we are in user code
    // in this file which will do a tud_task itself, so we'll just do nothing
    // until the next tick; we won't starve
    if (mutex_try_enter(&__usb_mutex, NULL)) {
        tud_task();
        mutex_exit(&__usb_mutex);
    }
}

static int64_t timer_task(__unused alarm_id_t id, __unused void *user_data) {
    irq_set_pending(PICO_STDIO_USB_LOW_PRIORITY_IRQ);
    return PICO_STDIO_USB_TASK_INTERVAL_US;
}

void __StartUSB() {
    if (tusb_inited()) {
        // Already called
        return;
    }

    // Get ID string into human readable serial number
    pico_unique_board_id_t id;
    pico_get_unique_board_id(&id);
    _idString[0] = 0;
    for (auto i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
        char hx[3];
        sprintf(hx, "%02X", id.id[i]);
        strcat(_idString, hx);
    }

    mutex_init(&__usb_mutex);
    tusb_init();

    irq_set_exclusive_handler(PICO_STDIO_USB_LOW_PRIORITY_IRQ, low_priority_worker_irq);
    irq_set_enabled(PICO_STDIO_USB_LOW_PRIORITY_IRQ, true);

    add_alarm_in_us(PICO_STDIO_USB_TASK_INTERVAL_US, timer_task, NULL, true);
}


// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  // TODO set LED based on CAPLOCK, NUMLOCK etc...
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) bufsize;
}

