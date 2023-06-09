/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Ha Thach (thach@tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * This file is part of the TinyUSB stack.
 */

#include "tusb_option.h"

#if CFG_TUC_ENABLED

#include "tcd.h"
#include "utcd.h"

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

// Debug level of USBD
#define UTCD_DEBUG   2
#define TU_LOG_UTCD(...)   TU_LOG(UTCD_DEBUG, __VA_ARGS__)

// Event queue
// utcd_int_set() is used as mutex in OS NONE config
void utcd_int_set(bool enabled);
OSAL_QUEUE_DEF(utcd_int_set, _utcd_qdef, CFG_TUC_TASK_QUEUE_SZ, tcd_event_t);
tu_static osal_queue_t _utcd_q;

// if stack is initialized
static bool _utcd_inited = false;

// if port is initialized
static bool _port_inited[TUP_TYPEC_RHPORTS_NUM];

// Max possible PD size is 262 bytes
static uint8_t _rx_buf[262] TU_ATTR_ALIGNED(4);

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+
bool tuc_inited(uint8_t rhport) {
  return _utcd_inited && _port_inited[rhport];
}

bool tuc_init(uint8_t rhport, tusb_typec_port_type_t port_type) {
  // Initialize stack
  if (!_utcd_inited) {
    tu_memclr(_port_inited, sizeof(_port_inited));

    _utcd_q = osal_queue_create(&_utcd_qdef);
    TU_ASSERT(_utcd_q != NULL);

    _utcd_inited = true;
  }

  // skip if port already initialized
  if ( _port_inited[rhport] ) {
    return true;
  }

  TU_LOG_UTCD("UTCD init on port %u\r\n", rhport);

  TU_ASSERT(tcd_init(rhport, port_type));
  tcd_int_enable(rhport);

  _port_inited[rhport] = true;
  return true;
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+

bool parse_message(uint8_t rhport, uint8_t const* buf, uint16_t len) {
  (void) rhport;
  uint8_t const* p_end = buf + len;
  tusb_pd_header_t const* header = (tusb_pd_header_t const*) buf;
  uint8_t const * ptr = buf + sizeof(tusb_pd_header_t);

  if (header->n_data_obj == 0) {
    // control message
  } else {
    // data message
    switch (header->msg_type) {
      case TUSB_PD_DATA_SOURCE_CAP: {
        for(size_t i=0; i<header->n_data_obj; i++) {
          TU_VERIFY(ptr < p_end);
          uint32_t const pdo = tu_le32toh(tu_unaligned_read32(ptr));

          switch ((pdo >> 30) & 0x03ul) {
            case PD_PDO_TYPE_FIXED: {
              pd_pdo_fixed_t const* fixed = (pd_pdo_fixed_t const*) &pdo;
              TU_LOG3("[Fixed] %u mV %u mA\r\n", fixed->voltage_50mv*50, fixed->current_max_10ma*10);
              break;
            }

            case PD_PDO_TYPE_BATTERY:
              break;

            case PD_PDO_TYPE_VARIABLE:
              break;

            case PD_PDO_TYPE_APDO:
              break;
          }

          ptr += 4;
        }
        break;
      }

      default: break;
    }
  }

  return true;
}

void tcd_event_handler(tcd_event_t const * event, bool in_isr) {
  (void) in_isr;
  switch(event->event_id) {
    case TCD_EVENT_CC_CHANGED:
      if (event->cc_changed.cc_state[0] || event->cc_changed.cc_state[1]) {
        // Attach
        tcd_rx_start(event->rhport, _rx_buf, sizeof(_rx_buf));
      }else {
        // Detach
      }
      break;

    case TCD_EVENT_RX_COMPLETE:
      // TODO process message here in ISR, move to thread later
      if (event->rx_complete.result == XFER_RESULT_SUCCESS) {
        parse_message(event->rhport, _rx_buf, event->rx_complete.xferred_bytes);
      }

      // start new rx
      tcd_rx_start(event->rhport, _rx_buf, sizeof(_rx_buf));
      break;

    default: break;
  }
}

//--------------------------------------------------------------------+
//
//--------------------------------------------------------------------+
void utcd_int_set(bool enabled) {
  // Disable all controllers since they shared the same event queue
  for (uint8_t p = 0; p < TUP_TYPEC_RHPORTS_NUM; p++) {
    if ( _port_inited[p] ) {
      if (enabled) {
        tcd_int_enable(p);
      }else {
        tcd_int_disable(p);
      }
    }
  }
}

#endif
