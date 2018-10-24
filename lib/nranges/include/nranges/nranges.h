/**
 * Copyright 2018, Decawave Limited, All Rights Reserved
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _DW1000_N_RANGES_H_
#define _DW1000_N_RANGES_H_

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <hal/hal_spi.h>
#include <dw1000/dw1000_regs.h>
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_ftypes.h>
#include <dw1000/triad.h>

#define FCNTL_IEEE_N_RANGES_16 0x88C1
#define FRAMES_PER_RANGE       2
#define FIRST_FRAME_IDX        0
#define SECOND_FRAME_IDX       1

typedef enum _dw1000_nrng_modes_t{
    DWT_DS_TWR_NRNG_INVALID = 0xFFFF,
    DWT_DS_TWR_NRNG = 17,
    DWT_DS_TWR_NRNG_T1,
    DWT_DS_TWR_NRNG_T2,
    DWT_DS_TWR_NRNG_FINAL,
    DWT_DS_TWR_NRNG_END,
    DWT_DS_TWR_NRNG_EXT,
    DWT_DS_TWR_NRNG_EXT_T1,
    DWT_DS_TWR_NRNG_EXT_T2,
    DWT_DS_TWR_NRNG_EXT_FINAL,
    DWT_DS_TWR_NRNG_EXT_END
}dw1000_nrng_modes_t;

typedef enum _dw1000_nrng_device_type_t{
    DWT_NRNG_INITIATOR,
    DWT_NRNG_RESPONDER
}dw1000_nrng_device_type_t;

//! nRange configuration parameters.
typedef struct _dw1000_nrng_config_t{
   uint32_t rx_holdoff_delay;        //!< Delay between frames, in UWB usec.
   uint32_t tx_guard_delay;
   uint32_t tx_holdoff_delay;        //!< Delay between frames, in UWB usec.
   uint16_t rx_timeout_period;       //!< Receive response timeout, in UWB usec.
   uint16_t bias_correction:1;       //!< Enable range bias correction polynomial
}dw1000_nrng_config_t;

//! nRange control parameters.
typedef struct _dw1000_nrng_control_t{
    uint16_t delay_start_enabled:1;  //!< Set for enabling delayed start
}dw1000_nrng_control_t;

//! Range status parameters
typedef struct _dw1000_nrng_status_t{
    uint16_t selfmalloc:1;           //!< Internal flag for memory garbage collection
    uint16_t initialized:1;          //!< Instance allocated
    uint16_t mac_error:1;            //!< Error caused due to frame filtering
    uint16_t invalid_code_error:1;   //!< Error due to invalid code
}dw1000_nrng_status_t;

//! N-Ranges request frame
typedef union {
    struct  _nrng_request_frame_t{
       struct _ieee_rng_request_frame_t;
       uint8_t start_slot_id;    //!< First anchor slot_id allowed
       uint8_t end_slot_id;      //!< Last anchor slot_id allowed
    }__attribute__((__packed__,aligned(1)));
    uint8_t array[sizeof(struct _nrng_request_frame_t)]; //!< Array of size nrng request frame
} nrng_request_frame_t;

//! N-Ranges response frame
typedef union {
    struct  _nrng_response_frame_t{
       struct _nrng_request_frame_t;
       uint8_t slot_id;  //!< slot_id of transmitting anchor
       uint32_t reception_timestamp;//!< Request reception timestamp
       uint32_t transmission_timestamp; //!< Response tx timestamp
    }__attribute__((__packed__,aligned(1)));
    uint8_t array[sizeof(struct _nrng_response_frame_t)]; //!< Array of size nrng response frame
} nrng_response_frame_t;

//! N-Ranges response frame
typedef union {
    struct  _nrng_final_frame_t{
       struct _nrng_response_frame_t;
       uint32_t request_timestamp;
       uint32_t response_timestamp;
    }__attribute__((__packed__,aligned(1)));
    uint8_t array[sizeof(struct _nrng_final_frame_t)]; //!< Array of size range final frame
} nrng_final_frame_t;

//! TWR data format
typedef struct _nrng_twr_data_t{
                triad_t spherical;                  //!< Measurement triad spherical coordinates
                triad_t spherical_variance;         //!< Measurement variance triad 
                triad_t cartesian;                  //!< Position triad local coordinates
          //      triad_t cartesian_variance;       //!< Position estimated variance triad 
}nrng_twr_data_t;

//! N-Ranges ext response frame format
typedef union {
    struct _nrng_frame_t{
        struct _nrng_final_frame_t;
#if MYNEWT_VAL(TWR_DS_EXT_NRNG_ENABLED)
        union {
            struct _nrng_twr_data_t;                            //!< Structure of twr_data
            uint8_t payload[sizeof(struct _nrng_twr_data_t)];   //!< Payload of size twr_data 
        };
#endif
    } __attribute__((__packed__, aligned(1)));
    uint8_t array[sizeof(struct _nrng_frame_t)];        //!< Array of size twr_frame
} nrng_frame_t;


typedef struct _dw1000_nrng_instance_t{
    uint16_t nframes;
    uint16_t nnodes;
    uint16_t resp_count;
    uint16_t t1_final_flag;
    uint64_t delay;
    dw1000_nrng_device_type_t device_type;
    dw1000_nrng_status_t status;
    dw1000_nrng_control_t control;
    dw1000_nrng_config_t config;
    struct os_sem sem;
    uint16_t idx;
    struct _dw1000_dev_instance_t * parent;
    nrng_frame_t *frames[][FRAMES_PER_RANGE];
}dw1000_nrng_instance_t;

dw1000_nrng_instance_t * dw1000_nrng_init(dw1000_dev_instance_t * inst, dw1000_nrng_config_t * config, dw1000_nrng_device_type_t type, uint16_t nframes, uint16_t nnodes);
dw1000_dev_status_t dw1000_nrng_request_delay_start(dw1000_dev_instance_t * inst, uint16_t dst_address, uint64_t delay, dw1000_nrng_modes_t code, uint16_t start_slot_id, uint16_t end_slot_id);
dw1000_dev_status_t dw1000_nrng_request(dw1000_dev_instance_t * inst, uint16_t dst_address, dw1000_nrng_modes_t code, uint16_t start_slot_id, uint16_t end_slot_id);
float dw1000_nrng_twr_to_tof_frames(nrng_frame_t *first_frame, nrng_frame_t *final_frame);
void dw1000_nrng_set_frames(dw1000_dev_instance_t* inst, uint16_t nframes);
dw1000_dev_status_t dw1000_nrng_config(struct _dw1000_dev_instance_t* inst, dw1000_nrng_config_t * config);
dw1000_nrng_config_t * dw1000_nrng_get_config(dw1000_dev_instance_t * inst, dw1000_nrng_modes_t code);
#define dw1000_nrng_tof_to_meters(ToF) (float)(ToF * 299792458 * (1.0/499.2e6/128.0)) //!< Converts time of flight to meters.

#ifdef __cplusplus
}
#endif
#endif /* _DW1000_N_RANGES_H_ */
