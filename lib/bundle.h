/************************************************************************
 * File: bundle.h
 *
 *  Copyright 2019 United States Government as represented by the 
 *  Administrator of the National Aeronautics and Space Administration. 
 *  All Other Rights Reserved.  
 *
 *  This software was created at NASA's Goddard Space Flight Center.
 *  This software is governed by the NASA Open Source Agreement and may be 
 *  used, distributed and modified only pursuant to the terms of that 
 *  agreement.
 *
 * Maintainer(s):
 *  Joe-Paul Swinski, Code 582 NASA GSFC
 *
 *************************************************************************/

#ifndef __BPLIB_BUNDLE_H__
#define __BPLIB_BUNDLE_H__

/******************************************************************************
 INCLUDES
 ******************************************************************************/

#include "bplib.h"
#include "bplib_os.h"
#include "v6.h"

/******************************************************************************
 DEFINES
 ******************************************************************************/

#define BP_NUM_EXCLUDE_REGIONS          8
#define BP_BUNDLE_HDR_BUF_SIZE          128

/******************************************************************************
 TYPEDEFS
 ******************************************************************************/

/* Generate Call-Back */
typedef int (*bp_generate_t) (void* parm, bool is_record, uint8_t* payload, int size, int timeout);

/* Active Bundle */
typedef struct {
    bp_sid_t            sid;    /* storage id */
    bp_val_t            retx;   /* retransmit time */
    bp_val_t            cid;    /* custody id */
} bp_active_bundle_t;

/* Bundle Data */
typedef struct {
    bp_val_t            exprtime;               /* absolute time when bundle expires */
    bp_field_t          cidsdnv;                /* SDNV of custody id field of bundle */
    int                 cteboffset;             /* offset of the CTEB block of bundle */
    int                 biboffset;              /* offset of the BIB block of bundle */
    int                 payoffset;              /* offset of the payload block of bundle */
    int                 headersize;             /* size of the header (portion of buffer below used) */
    int                 bundlesize;             /* total size of the bundle (header and payload) */
    uint8_t             header[BP_BUNDLE_HDR_BUF_SIZE]; /* header portion of bundle */
} bp_bundle_data_t;

/* Custodian */
typedef struct {
    uint8_t*            rec;                    /* payload | aggregate custody signal */
    int                 rec_size;               /* size of payload | size of aggregate custody signal */
    bp_ipn_t            node;                   /* custody node of bundle */
    bp_ipn_t            service;                /* custody service of bundle */
    bp_val_t            cid;                    /* custody id of bundle */
} bp_custodian_t;

/* Bundle Control Structure */
typedef struct {
    bp_route_t          route;                  /* addressing information */
    bp_attr_t*          attributes;             /* -pointer- to the channel attributes */
    bp_bundle_data_t    data;                   /* serialized and stored bundle data */
    bool                prebuilt;               /* does pre-built bundle header need initialization */
    bp_v6blocks_t       v6blocks;               /* populated in initialization function */
} bp_bundle_t;

/******************************************************************************
 PROTOTYPES
 ******************************************************************************/

int     bundle_initialize   (bp_bundle_t* bundle, bp_route_t route, bp_attr_t* attributes, uint16_t* flags);
int     bundle_send         (bp_bundle_t* bundle, bool set_time, uint8_t* pay, int pay_size, bp_generate_t gen, void* parm, int timeout, uint16_t* flags);
int     bundle_receive      (bp_bundle_t* bundle, uint8_t* block, int block_size, bp_val_t sysnow, bp_custodian_t* custodian, uint16_t* flags);
int     bundle_update       (bp_bundle_data_t* data, bp_val_t cid, uint16_t* flags);

/* v6 bundle functions */
int     v6_build            (bp_bundle_t* bundle, bp_blk_pri_t* pri, uint8_t* hdr_buf, int hdr_len, uint16_t* flags);
int     v6_write            (bp_bundle_t* bundle, bool set_time, uint8_t* pay_buf, int pay_len, bp_generate_t gen, void* parm, int timeout, uint16_t* flags);
int     v6_read             (bp_bundle_t* bundle, uint8_t* block, int block_size, bp_val_t sysnow, bp_custodian_t* custodian, uint16_t* flags);
int     v6_update           (bp_bundle_data_t* data, bp_val_t cid, uint16_t* flags);



#endif  /* __BPLIB_BUNDLE_H__ */
