/*
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
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <os/os.h>
#include <hal/hal_spi.h>
#include <hal/hal_gpio.h>
#include "bsp/bsp.h"

#include <dw1000/dw1000_regs.h>
#include <dw1000/dw1000_dev.h>
#include <dw1000/dw1000_hal.h>
#include <dw1000/dw1000_mac.h>
#include <dw1000/dw1000_phy.h>
#include <dw1000/dw1000_ftypes.h>

#if MYNEWT_VAL(N_RANGES_NPLUS_TWO_MSGS)
#include <nranges/dw1000_nranges.h>

static bool nranges_rx_complete_cb(dw1000_dev_instance_t * inst);
static bool nranges_rx_error_cb(dw1000_dev_instance_t * inst);
static bool nranges_rx_timeout_cb(dw1000_dev_instance_t* inst);
static bool nranges_tx_complete_cb(dw1000_dev_instance_t* inst);
static bool nranges_tx_error_cb(dw1000_dev_instance_t * inst);
static void nrng_tx_final_cb(dw1000_dev_instance_t * inst);
static dw1000_nranges_instance_t * nranges_instance = NULL;

dw1000_nranges_instance_t *
dw1000_nranges_init(dw1000_dev_instance_t * inst, dw1000_nranges_device_type_t type, uint16_t nframes, uint16_t nnodes){
    assert(inst);

    if (nranges_instance == NULL ) {
        nranges_instance = (dw1000_nranges_instance_t*) malloc(sizeof(dw1000_nranges_instance_t) + (nframes) * sizeof(nrng_frame_t*)); // struct + flexible array member
        assert(nranges_instance);
        memset(nranges_instance, 0, sizeof(dw1000_nranges_instance_t));
    }

    dw1000_nranges_instance_t *nranges = nranges_instance; // Updating the Global Instance of nranges
    nranges->nframes = nframes;
    nranges->device_type = type;
    nranges->nnodes = nnodes;
    nranges->idx = 0xFFFF;
    nranges->resp_count = nranges->timeout_count = nranges->t1_final_flag = 0;
 
    dw1000_extension_callbacks_t nranges_cbs;
    nranges_cbs.id = DW1000_N_RANGES;
    os_error_t err = os_sem_init(&nranges->sem, 0x1);
    assert(err == OS_OK);
    dw1000_set_dblrxbuff(inst, true);
    nranges_cbs.tx_complete_cb = nranges_tx_complete_cb;
    nranges_cbs.rx_complete_cb = nranges_rx_complete_cb;
    nranges_cbs.rx_timeout_cb = nranges_rx_timeout_cb;
    nranges_cbs.rx_error_cb = nranges_rx_error_cb;
    nranges_cbs.tx_error_cb = nranges_tx_error_cb;
    nranges_cbs.reset_cb = NULL;
    dw1000_nranges_set_ext_callbacks(inst, nranges_cbs);
    if(type == DWT_NRNG_RESPONDER)
        dw1000_rng_set_tx_final_cb(inst, nrng_tx_final_cb);
    else
        dw1000_rng_set_tx_final_cb(inst, NULL);

    return nranges;
}

inline void
dw1000_nrng_set_frames(dw1000_dev_instance_t * inst, nrng_frame_t twr[], uint16_t nframes){
        assert(nframes <= nranges_instance->nframes);
        for (uint16_t i = 0; i < nframes/FRAMES_PER_RANGE; i++){
            for(uint16_t j =0; j < FRAMES_PER_RANGE; j++)
                nranges_instance->frames[i][j] = &twr[(i*FRAMES_PER_RANGE)+j];
        }
}

dw1000_dev_status_t 
dw1000_nranges_request_delay_start(dw1000_dev_instance_t * inst, uint16_t dst_address, uint64_t delay, dw1000_rng_modes_t code, uint16_t start_slot_id, uint16_t end_slot_id){

    dw1000_rng_instance_t * rng = inst->rng;    

    rng->control.delay_start_enabled = 1;
    rng->delay = delay;
    dw1000_nranges_request(inst, dst_address, code, start_slot_id, end_slot_id);
    rng->control.delay_start_enabled = 0;
    dw1000_phy_forcetrxoff(inst);
    return inst->status;
}

dw1000_dev_status_t
dw1000_nranges_request(dw1000_dev_instance_t * inst, uint16_t dst_address, dw1000_nranges_modes_t code, uint16_t start_slot_id, uint16_t end_slot_id){

    // This function executes on the device that initiates a request
    assert(nranges_instance);
    dw1000_nranges_instance_t * nranges = nranges_instance;
    os_error_t err = os_sem_pend(&nranges->sem,  OS_TIMEOUT_NEVER);
    assert(err == OS_OK);

    dw1000_rng_instance_t * rng = inst->rng;
    nrng_frame_t * frame  = nranges->frames[0][FIRST_FRAME_IDX];
    dw1000_rng_config_t * config = inst->rng->config;

    frame->seq_num++;
    frame->code = code;
    frame->src_address = inst->my_short_address;
    frame->dst_address = dst_address;
    frame->start_slot_id = start_slot_id;
    frame->end_slot_id = end_slot_id;
    dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_request_frame_t));
    dw1000_write_tx_fctrl(inst, sizeof(nrng_request_frame_t), 0, true);
    dw1000_set_wait4resp(inst, true);
    uint16_t timeout = (frame->end_slot_id - inst->slot_id + 1)*(dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_request_frame_t))
                    + config->tx_guard_delay
                    + config->rx_timeout_period         // 2 * ToF, 1us ~= 300m
                    + config->tx_holdoff_delay);         // Remote side turn arroud time.
    dw1000_set_rx_timeout(inst, timeout);
    if (rng->control.delay_start_enabled)
        dw1000_set_delay_start(inst, rng->delay);
    if (dw1000_start_tx(inst).start_tx_error){
        if(!(SLIST_EMPTY(&inst->extension_cbs))){
            dw1000_extension_callbacks_t *temp = NULL;
            SLIST_FOREACH(temp, &inst->extension_cbs, cbs_next){
                if(temp != NULL)
                    if(temp->tx_error_cb != NULL)
                        if(temp->tx_error_cb(inst) == true)
                            break;
            }
        }
        os_sem_release(&nranges->sem);
    }
    err = os_sem_pend(&nranges->sem, OS_TIMEOUT_NEVER); // Wait for completion of transactions
    os_sem_release(&nranges->sem);
    return inst->status;
}

static bool
nranges_rx_timeout_cb(dw1000_dev_instance_t * inst){
    if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    assert(nranges_instance);
    dw1000_nranges_instance_t * nranges = nranges_instance;
    dw1000_rng_config_t * config = inst->rng->config;
    if(nranges->device_type == DWT_NRNG_INITIATOR)// only if the device is an initiator
    {
        nranges->timeout_count++;
        if(nranges->resp_count == 0 && nranges->timeout_count == nranges->nnodes)
        {
            os_error_t err = os_sem_release(&nranges->sem);
            assert(err == OS_OK);
            nranges->timeout_count = 0;
        }
        else if(nranges->resp_count + nranges->timeout_count == nranges->nnodes && nranges->t1_final_flag == 1)
        {
            nrng_frame_t * frame = nranges->frames[nranges->idx][SECOND_FRAME_IDX];
            send_final_msg(inst,frame);
        }
        else if(nranges->resp_count + nranges->timeout_count < nranges->nnodes)
        {
           uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                  + config->tx_guard_delay
                  + config->rx_timeout_period         // 2 * ToF, 1us ~= 300m
                  + config->tx_holdoff_delay;         // Remote side turn arroud time.
            dw1000_set_rx_timeout(inst, timeout);
            dw1000_start_rx(inst);
        }
        else
        {
            nranges->timeout_count = 0;
            os_error_t err = os_sem_release(&nranges->sem);
            assert(err == OS_OK);
        }
    }
    else
    {
        os_error_t err = os_sem_release(&nranges->sem);
        assert(err == OS_OK);
    }
    return true;
}

static bool
nranges_rx_error_cb(dw1000_dev_instance_t * inst){
    /* Place holder */
    if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    assert(nranges_instance);
    dw1000_nranges_instance_t * nranges = nranges_instance;
    os_error_t err = os_sem_release(&nranges->sem);
    assert(err == OS_OK);
    return true;
}

static bool
nranges_tx_complete_cb(dw1000_dev_instance_t * inst){
    /* Place holder */
   if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    dw1000_nranges_instance_t * nranges = nranges_instance;
    if(nranges->device_type == DWT_NRNG_RESPONDER){
        if (inst->rng_complete_cb) {
            inst->rng_complete_cb(inst);
        }
    }

    return true;
}

static bool
nranges_tx_error_cb(dw1000_dev_instance_t * inst){
    /* Place holder */
    if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    return true;
}

static bool
nranges_rx_complete_cb(dw1000_dev_instance_t * inst){
    /* Place holder */
     if(inst->fctrl != FCNTL_IEEE_N_RANGES_16){
        return false;
    }
    assert(nranges_instance);
    dw1000_nranges_instance_t * nranges = nranges_instance;
    uint16_t code, dst_address;
    dw1000_rng_config_t * config = inst->rng->config;
    dw1000_dev_control_t control = inst->control_rx_context;
    dw1000_read_rx(inst, (uint8_t *) &code, offsetof(nrng_request_frame_t,code), sizeof(uint16_t));
    dw1000_read_rx(inst, (uint8_t *) &dst_address, offsetof(nrng_request_frame_t,dst_address), sizeof(uint16_t));
       // For initiator: Only Allow the packets with dst_address matching with device my_short_address.
       // For responder: Only Allow the packets with dst_address matching with device my_short_address/Broadcast address.
    if (dst_address != inst->my_short_address && (dst_address != BROADCAST_ADDRESS || nranges->device_type == DWT_NRNG_INITIATOR) ){ 
        inst->control = inst->control_rx_context;
        dw1000_restart_rx(inst, control);
        return true;
    }

    switch (code){
#if MYNEWT_VAL(N_RANGES_NPLUS_TWO_MSGS) // n_ranges
        case DWT_DS_TWR_NRNG ... DWT_DS_TWR_NRNG_FINAL:
            switch(code){
                case DWT_DS_TWR_NRNG:
                    {
                        // This code executes on the device that is responding to a original request
                        // printf("nrng\n");
                        nrng_frame_t * frame = nranges->frames[(++nranges->idx)%(nranges->nframes/FRAMES_PER_RANGE)][FIRST_FRAME_IDX];
                        uint16_t slot_id = inst->slot_id;
                        if (inst->frame_len >= sizeof(nrng_request_frame_t))
                            dw1000_read_rx(inst, frame->array, 0, sizeof(nrng_request_frame_t));
                        else
                            break;
                        if(!(slot_id >= frame->start_slot_id && slot_id <= frame->end_slot_id))
                            break;

                        uint64_t request_timestamp = dw1000_read_rxtime(inst);
                        uint64_t response_tx_delay = request_timestamp + (((uint64_t)config->tx_holdoff_delay + ((uint64_t)(slot_id-1) * ((uint64_t)config->tx_guard_delay +  dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_response_frame_t)))))<< 16);
                        uint64_t response_timestamp = (response_tx_delay & 0xFFFFFFFE00UL) + inst->tx_antenna_delay;

                        frame->reception_timestamp =  request_timestamp;
                        frame->transmission_timestamp =  response_timestamp;

                        frame->dst_address = frame->src_address;
                        frame->src_address = inst->my_short_address;
                        frame->code = DWT_DS_TWR_NRNG_T1;
                        frame->slot_id = inst->slot_id;

                        dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_response_frame_t));
                        dw1000_write_tx_fctrl(inst, sizeof(nrng_response_frame_t), 0, true);
                        dw1000_set_wait4resp(inst, true);
                        uint16_t timeout =(frame->end_slot_id - slot_id + 1) * (dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_response_frame_t))
                            + config->rx_timeout_period
                            + config->tx_guard_delay
                            + config->tx_holdoff_delay);         // Remote side turn arroud time.
                        dw1000_set_rx_timeout(inst, timeout);
                        dw1000_set_delay_start(inst, response_tx_delay);
                        if (dw1000_start_tx(inst).start_tx_error)
                            os_sem_release(&nranges->sem);
                        break;
                    }
                case DWT_DS_TWR_NRNG_T1:
                    {
                        // This code executes on the device that initiated the original request, and is now preparing the next series of timestamps
                        // The 1st frame now contains a local copy of the initial first side of the double sided scheme.
                        // printf("T1 \n");
                        uint16_t nnodes = nranges->nnodes;
                        nrng_frame_t temp_frame;
                        if (inst->frame_len >= sizeof(nrng_response_frame_t))
                            dw1000_read_rx(inst, temp_frame.array, 0, sizeof(nrng_response_frame_t));
                        else
                            break;

                        nrng_frame_t * frame = nranges->frames[temp_frame.slot_id - temp_frame.start_slot_id][FIRST_FRAME_IDX];
                        nrng_frame_t * next_frame = nranges->frames[temp_frame.slot_id - temp_frame.start_slot_id][SECOND_FRAME_IDX];
                        memcpy(frame, &temp_frame, sizeof(nrng_response_frame_t));

                        frame->request_timestamp = next_frame->request_timestamp = dw1000_read_txtime_lo(inst);    // This corresponds to when the original request was actually sent
                        frame->response_timestamp = next_frame->response_timestamp = dw1000_read_rxtime_lo(inst);  // This corresponds to the response just received

                        uint8_t seq_num = frame->seq_num;
                        frame->dst_address = frame->src_address;
                        frame->src_address = inst->my_short_address;
                        frame->code = DWT_DS_TWR_NRNG_T2;
                        frame->seq_num = seq_num + 1;
                        // Note:: Advance to next frame
                        frame = next_frame;
                        frame->dst_address = 0xffff;
                        frame->src_address = inst->my_short_address;
                        frame->seq_num = seq_num + 1;
                        frame->code = DWT_DS_TWR_NRNG_T2;
                        frame->start_slot_id = temp_frame.start_slot_id;
                        frame->end_slot_id = temp_frame.end_slot_id;

                        uint64_t request_timestamp = dw1000_read_rxtime(inst);
                        uint64_t response_tx_delay = request_timestamp + (((uint64_t)(config->tx_holdoff_delay)) << 16);
                        uint64_t response_timestamp = (response_tx_delay & 0xFFFFFFFE00UL) + inst->tx_antenna_delay;

                        frame->reception_timestamp = request_timestamp;
                        frame->transmission_timestamp = response_timestamp;

                        nranges->resp_count++;
                        nranges->idx = (temp_frame.slot_id - temp_frame.start_slot_id);
                        nranges->t1_final_flag = 1;
                        uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                            + config->tx_guard_delay
                            + config->rx_timeout_period
                            + config->tx_holdoff_delay;         // Remote side turn arroud time.
                        dw1000_set_rx_timeout(inst, timeout);

                        if(nranges->resp_count + nranges->timeout_count == nnodes)
                        {
                            dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_final_frame_t));
                            dw1000_write_tx_fctrl(inst, sizeof(nrng_final_frame_t), 0, true);
                            dw1000_set_wait4resp(inst, true);

                            dw1000_set_delay_start(inst, response_tx_delay);
                            nranges->resp_count = 0;
                            nranges->timeout_count = 0;
                            nranges->t1_final_flag = 0;
                            if (dw1000_start_tx(inst).start_tx_error)
                                os_sem_release(&nranges->sem);
                        }else{
                            dw1000_start_rx(inst);
                        }
                        break;
                    }
                case DWT_DS_TWR_NRNG_T2:
                    {
                        // This code executes on the device that responded to the original request, and is now preparing the final timestamps
                        // printf("T2\n");
                        uint16_t slot_id = inst->slot_id;
                        nrng_frame_t * previous_frame = nranges->frames[nranges->idx%(nranges->nframes/FRAMES_PER_RANGE)][FIRST_FRAME_IDX];
                        nrng_frame_t * frame = nranges->frames[(++nranges->idx)%(nranges->nframes/FRAMES_PER_RANGE)][SECOND_FRAME_IDX];

                        if (inst->frame_len >= sizeof(nrng_final_frame_t))
                            dw1000_read_rx(inst,  frame->array, 0, sizeof(nrng_final_frame_t));
                        else
                            break;
                        if(!(slot_id >= frame->start_slot_id && slot_id <= frame->end_slot_id))
                            break;
                         
                        previous_frame->request_timestamp = frame->request_timestamp;
                        previous_frame->response_timestamp = frame->response_timestamp;

                        uint64_t request_timestamp = dw1000_read_rxtime(inst);
                        uint64_t response_tx_delay = request_timestamp + (((uint64_t)config->tx_holdoff_delay + ((uint64_t)(inst->slot_id-1) * ((uint64_t)config->tx_guard_delay +  dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t)))))<< 16);
                        frame->request_timestamp = dw1000_read_txtime_lo(inst); // This corresponds to when the original request was actually sent
                        frame->response_timestamp = dw1000_read_rxtime_lo(inst);  // This corresponds to the response just received
                        frame->dst_address = frame->src_address;
                        frame->src_address = inst->my_short_address;
                        frame->code = DWT_DS_TWR_NRNG_FINAL;
                        frame->slot_id = slot_id;
                        dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_final_frame_t));
                        dw1000_write_tx_fctrl(inst, sizeof(nrng_final_frame_t), 0, true);
                        uint16_t timeout =(frame->end_slot_id - slot_id + 1)* (dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                                + config->tx_guard_delay
                                + config->rx_timeout_period
                                + config->tx_holdoff_delay);         // Remote side turn arroud time.
                        dw1000_set_rx_timeout(inst, timeout);
                        dw1000_set_delay_start(inst, response_tx_delay);
                        if (dw1000_start_tx(inst).start_tx_error)
                            os_sem_release(&nranges->sem);
                        break;
                    }
                case  DWT_DS_TWR_NRNG_FINAL:
                    {
                        // This code executes on the device that initialed the original request, and has now receive the final response timestamp.
                        // This marks the completion of the double-single-two-way request.
                        // printf("Final\n");
                        uint16_t nnodes = nranges->nnodes;
                        nrng_frame_t temp;
                        if (inst->frame_len >= sizeof(nrng_final_frame_t))
                            dw1000_read_rx(inst, (uint8_t *)&temp, 0, sizeof(nrng_final_frame_t));

                        nrng_frame_t * frame = nranges->frames[temp.slot_id - temp.start_slot_id][SECOND_FRAME_IDX];

                        nranges->t1_final_flag = 0;
                        nranges->resp_count++;
                        frame->request_timestamp = temp.request_timestamp;
                        frame->response_timestamp = temp.response_timestamp;
                        frame->code = temp.code;
                        frame->dst_address = temp.src_address;
                        frame->transmission_timestamp = dw1000_read_txtime_lo(inst);
                        if(nranges->resp_count + nranges->timeout_count < nnodes)
                        {
                            uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                                + config->tx_guard_delay
                                + config->rx_timeout_period         // 2 * ToF, 1us ~= 300m
                                + config->tx_holdoff_delay;         // Remote side turn arroud time.
                            dw1000_set_rx_timeout(inst, timeout);
                            dw1000_start_rx(inst);
                        }
                        else if(nranges->resp_count + nranges->timeout_count == nnodes)
                        {
                            os_sem_release(&nranges->sem);
                        }
                        break;
                    }
                default:
                    break;
            }
            break;
#endif // n_ranges
#if MYNEWT_VAL(N_RANGES_NPLUS_TWO_MSGS) && MYNEWT_VAL(DW1000_DS_TWR_EXT_ENABLED) // n_ranges_ext
        case DWT_DS_TWR_NRNG_EXT ... DWT_DS_TWR_NRNG_EXT_FINAL:
            switch(code){
                case DWT_DS_TWR_NRNG_EXT:
                    {
                        // This code executes on the device that is responding to a original request
                        // printf("DWT_DS_TWR\n");
                        nrng_frame_t * frame = nranges->frames[(++nranges->idx)%(nranges->nframes/FRAMES_PER_RANGE)][FIRST_FRAME_IDX];
                        uint16_t slot_id = inst->slot_id;
                        if (inst->frame_len >= sizeof(nrng_request_frame_t))
                            dw1000_read_rx(inst, frame->array, 0, sizeof(nrng_request_frame_t));
                        else
                            break;
                        if(!(slot_id >= frame->start_slot_id && slot_id <= frame->end_slot_id))
                            break;

                        uint64_t request_timestamp = dw1000_read_rxtime(inst);
                        uint64_t response_tx_delay = request_timestamp + (((uint64_t)config->tx_holdoff_delay + ((uint64_t)(slot_id-1) * ((uint64_t)config->tx_guard_delay +  dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_response_frame_t)))))<< 16);

                        uint64_t response_timestamp = (response_tx_delay & 0xFFFFFFFE00UL) + inst->tx_antenna_delay;

                        frame->reception_timestamp =  request_timestamp;
                        frame->transmission_timestamp =  response_timestamp;

                        frame->dst_address = frame->src_address;
                        frame->src_address = inst->my_short_address;
                        frame->code = DWT_DS_TWR_NRNG_EXT_T1;
                        frame->slot_id = inst->slot_id;

                        dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_response_frame_t));
                        dw1000_write_tx_fctrl(inst, sizeof(nrng_response_frame_t), 0, true);
                        dw1000_set_wait4resp(inst, true);
                        uint16_t timeout =(frame->end_slot_id - slot_id + 1) * (dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_response_frame_t))
                            + config->rx_timeout_period
                            + config->tx_guard_delay
                            + config->tx_holdoff_delay);         // Remote side turn arroud time.
                        dw1000_set_rx_timeout(inst, timeout);
                        dw1000_set_delay_start(inst, response_tx_delay);
                        if (dw1000_start_tx(inst).start_tx_error)
                            os_sem_release(&nranges->sem);
                        break;
                    }
                case DWT_DS_TWR_NRNG_EXT_T1:
                    {
                        //printf("T1\n");
                        // This code executes on the device that initiated the original request, and is now preparing the next series of timestamps
                        // The 1st frame now contains a local copy of the initial first side of the double sided scheme.
                        // printf("DWT_DS_TWR_T1\n");
                        uint16_t nnodes = nranges->nnodes;
                        nrng_frame_t temp_frame;
                        if (inst->frame_len >= sizeof(nrng_response_frame_t))
                            dw1000_read_rx(inst, temp_frame.array, 0, sizeof(nrng_response_frame_t));
                        else
                            break;

                        nrng_frame_t * frame = nranges->frames[temp_frame.slot_id - temp_frame.start_slot_id][FIRST_FRAME_IDX];
                        nrng_frame_t * next_frame = nranges->frames[temp_frame.slot_id - temp_frame.start_slot_id][SECOND_FRAME_IDX];
                        memcpy(frame, &temp_frame, sizeof(nrng_response_frame_t));

                        frame->request_timestamp = next_frame->request_timestamp = dw1000_read_txtime_lo(inst);    // This corresponds to when the original request was actually sent
                        frame->response_timestamp = next_frame->response_timestamp = dw1000_read_rxtime_lo(inst);  // This corresponds to the response just received

                        uint8_t seq_num = frame->seq_num;
                        frame->dst_address = frame->src_address;
                        frame->src_address = inst->my_short_address;
                        frame->code = DWT_DS_TWR_NRNG_EXT_T2;
                        frame->seq_num = seq_num + 1;
                        // Note:: Advance to next frame
                        frame = next_frame;
                        frame->dst_address = 0xffff;
                        frame->src_address = inst->my_short_address;
                        frame->seq_num = seq_num + 1;
                        frame->code = DWT_DS_TWR_NRNG_EXT_T2;
                        frame->start_slot_id = temp_frame.start_slot_id;
                        frame->end_slot_id = temp_frame.end_slot_id;

                        uint64_t request_timestamp = dw1000_read_rxtime(inst);
                        uint64_t response_tx_delay = request_timestamp + ((uint64_t)config->tx_holdoff_delay << 16);
                        uint64_t response_timestamp = (response_tx_delay & 0xFFFFFFFE00UL) + inst->tx_antenna_delay;

                        frame->reception_timestamp = request_timestamp;
                        frame->transmission_timestamp = response_timestamp;

                        nranges->resp_count++;
                        nranges->idx = (temp_frame.slot_id - temp_frame.start_slot_id);
                        nranges->t1_final_flag = 1;
                        uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                            + config->tx_guard_delay
                            + config->rx_timeout_period
                            + config->tx_holdoff_delay;         // Remote side turn arroud time.
                        dw1000_set_rx_timeout(inst, timeout);

                        if(nranges->resp_count + nranges->timeout_count == nnodes)
                        {
                            dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_final_frame_t));
                            dw1000_write_tx_fctrl(inst, sizeof(nrng_final_frame_t), 0, true);
                            dw1000_set_wait4resp(inst, true);
                            dw1000_set_delay_start(inst, response_tx_delay);
                            nranges->resp_count = 0;
                            nranges->timeout_count = 0;
                            nranges->t1_final_flag = 0;

                            if (dw1000_start_tx(inst).start_tx_error)
                                os_sem_release(&nranges->sem);
                        }else{
                            dw1000_start_rx(inst);
                        }
                     break;

                    }
                case DWT_DS_TWR_NRNG_EXT_T2:
                    {
                        // This code executes on the device that responded to the original request, and is now preparing the final timestamps
                        // printf("DWT_SDS_TWR_T2\n");
                        uint16_t slot_id = inst->slot_id;
                        nrng_frame_t * previous_frame = nranges->frames[nranges->idx%(nranges->nframes/FRAMES_PER_RANGE)][FIRST_FRAME_IDX];
                        nrng_frame_t * frame = nranges->frames[(++nranges->idx)%(nranges->nframes/FRAMES_PER_RANGE)][SECOND_FRAME_IDX];

                        if (inst->frame_len >= sizeof(nrng_final_frame_t))
                            dw1000_read_rx(inst,  frame->array, 0, sizeof(nrng_final_frame_t));
                        else
                            break;
                        if(!(slot_id >= frame->start_slot_id && slot_id <= frame->end_slot_id))
                            break;

                        previous_frame->request_timestamp = frame->request_timestamp;
                        previous_frame->response_timestamp = frame->response_timestamp;

                        uint64_t request_timestamp = dw1000_read_rxtime(inst);
                        uint64_t response_tx_delay = request_timestamp + (((uint64_t)config->tx_holdoff_delay + ((uint64_t)(inst->slot_id-1) * ((uint64_t)config->tx_guard_delay +  dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t)))))<< 16);

                        dw1000_set_delay_start(inst, response_tx_delay);
                        frame->request_timestamp = dw1000_read_txtime_lo(inst); // This corresponds to when the original request was actually sent
                        frame->response_timestamp = dw1000_read_rxtime_lo(inst);  // This corresponds to the response just received
                        frame->dst_address = frame->src_address;
                        frame->src_address = inst->my_short_address;
                        frame->code = DWT_DS_TWR_NRNG_EXT_FINAL;
                        frame->slot_id = slot_id;
                        if (inst->rng_tx_final_cb != NULL)
                           inst->rng_tx_final_cb(inst);
                        dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_frame_t));
                        dw1000_write_tx_fctrl(inst, sizeof(nrng_frame_t), 0, true);
                        uint16_t timeout =(frame->end_slot_id - slot_id + 1)* (dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                                + config->tx_guard_delay
                                + config->rx_timeout_period
                                + config->tx_holdoff_delay);         // Remote side turn arroud time.
                        dw1000_set_rx_timeout(inst, timeout);

                        if (dw1000_start_tx(inst).start_tx_error)
                            os_sem_release(&nranges->sem);
                        break;
                    }
                case  DWT_DS_TWR_NRNG_EXT_FINAL:
                    {
                        // This code executes on the device that initialed the original request, and has now receive the final response timestamp.
                        // This marks the completion of the double-single-two-way request.
                        // printf("DWT_SDS_TWR_FINAL\n");
                        uint16_t nnodes = nranges->nnodes;
                        nrng_frame_t temp;
                        if (inst->frame_len >= sizeof(nrng_final_frame_t))
                            dw1000_read_rx(inst, (uint8_t *)&temp, 0, sizeof(nrng_final_frame_t));

                        nrng_frame_t * frame = nranges->frames[temp.slot_id - temp.start_slot_id][SECOND_FRAME_IDX];

                        nranges->t1_final_flag = 0;
                        nranges->resp_count++;
                        frame->request_timestamp = temp.request_timestamp;
                        frame->response_timestamp = temp.response_timestamp;
                        frame->code = temp.code;
                        frame->dst_address = temp.src_address;
                        memcpy(frame->payload,temp.payload,sizeof(twr_data_t));
                        frame->transmission_timestamp = dw1000_read_txtime_lo(inst);
                        if(nranges->resp_count + nranges->timeout_count < nnodes)
                        {
                            uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                                + config->tx_guard_delay
                                + config->rx_timeout_period         // 2 * ToF, 1us ~= 300m
                                + config->tx_holdoff_delay;         // Remote side turn arroud time.
                            dw1000_set_rx_timeout(inst, timeout);
                            dw1000_start_rx(inst);
                        }
                        else if(nranges->resp_count + nranges->timeout_count == nnodes)
                        {
                            os_sem_release(&nranges->sem);
                        }
                        break;
                    }
                default:
                    break;
            }
#endif // n_ranges_ext
        default:
             break;
    }
    return true;
}

void dw1000_nranges_set_ext_callbacks(dw1000_dev_instance_t * inst, dw1000_extension_callbacks_t nranges_cbs){
    assert(inst);
    nranges_cbs.id = DW1000_N_RANGES;
    dw1000_add_extension_callbacks(inst, nranges_cbs);
}

void send_final_msg(dw1000_dev_instance_t * inst , nrng_frame_t * frame)
{
    //printf("final_cb\n");
    assert(nranges_instance);
    dw1000_nranges_instance_t * nranges = nranges_instance;
    dw1000_rng_config_t * config = inst->rng->config;
    dw1000_write_tx(inst, frame->array, 0, sizeof(nrng_final_frame_t));
    dw1000_write_tx_fctrl(inst, sizeof(nrng_final_frame_t), 0, true);
    dw1000_set_wait4resp(inst, true);
    uint16_t timeout = dw1000_phy_frame_duration(&inst->attrib, sizeof(nrng_final_frame_t))
                    + config->tx_guard_delay
                    + config->rx_timeout_period         // 2 * ToF, 1us ~= 300m
                    + config->tx_holdoff_delay;         // Remote side turn arroud time.
    dw1000_set_rx_timeout(inst, timeout);

    nranges->resp_count = 0;
    nranges->timeout_count = 0;
    nranges->t1_final_flag = 0;

    if (dw1000_start_tx(inst).start_tx_error)
        os_sem_release(&nranges->sem);
}

float
dw1000_nranges_twr_to_tof_frames(nrng_frame_t *first_frame, nrng_frame_t *final_frame){
    float ToF = 0;
    uint64_t T1R, T1r, T2R, T2r;
    int64_t nom,denom;

    assert(first_frame != NULL);
    assert(final_frame != NULL);

    switch(final_frame->code){
        case DWT_DS_TWR_NRNG ... DWT_DS_TWR_NRNG_END:
        case DWT_DS_TWR_NRNG_EXT ... DWT_DS_TWR_NRNG_EXT_END:
            T1R = (first_frame->response_timestamp - first_frame->request_timestamp);
            T1r = (first_frame->transmission_timestamp  - first_frame->reception_timestamp);
            T2R = (final_frame->response_timestamp - final_frame->request_timestamp);
            T2r = (final_frame->transmission_timestamp - final_frame->reception_timestamp);
            nom = T1R * T2R  - T1r * T2r;
            denom = T1R + T2R  + T1r + T2r;
            ToF = (float) (nom) / denom;
            break;
        default: break;
    }
    return ToF;
}

static void
nrng_tx_final_cb(dw1000_dev_instance_t * inst){

#ifdef DS_TWR_EXT_ENABLE
    dw1000_nranges_instance_t * nranges = nranges_instance;
    nrng_frame_t * frame = nranges->frames[nranges->idx%(nranges->nframes/FRAMES_PER_RANGE)][SECOND_FRAME_IDX];

    frame->cartesian.x = MYNEWT_VAL(LOCAL_COORDINATE_X);
    frame->cartesian.y = MYNEWT_VAL(LOCAL_COORDINATE_Y);
    frame->cartesian.z = MYNEWT_VAL(LOCAL_COORDINATE_Z);

    frame->spherical_variance.range = MYNEWT_VAL(RANGE_VARIANCE);
    frame->spherical_variance.azimuth = -1;
    frame->spherical_variance.zenith = -1;
    frame->utime = os_cputime_ticks_to_usecs(os_cputime_get32());//dw1000_read_systime(inst)/128;
#endif
}

#endif //MYNEWT_VAL(N_RANGES_NPLUS_TWO_MSGS)
