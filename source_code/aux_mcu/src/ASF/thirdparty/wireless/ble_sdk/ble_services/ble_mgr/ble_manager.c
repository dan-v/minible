/**
* \file
*
* \brief BLE Manager
*
* Copyright (c) 2017-2018 Microchip Technology Inc. and its subsidiaries.
*
* \asf_license_start
*
* \page License
*
* Subject to your compliance with these terms, you may use Microchip
* software and any derivatives exclusively with Microchip products.
* It is your responsibility to comply with third party license terms applicable
* to your use of third party software (including open source software) that
* may accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES,
* WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
* INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
* AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
* LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
* LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
* SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
* POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
* ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
* RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*
* \asf_license_stop
*
*/
/*
* Support and FAQ: visit <a href="https://www.microchip.com/support/">Microchip Support</a>
*/

#include <asf.h>
#include <string.h>
#include "at_ble_api.h"
#include "at_ble_trace.h"
#include "driver_timer.h"
#include "logic_bluetooth.h"
#include "ble_manager.h"
#include "ble_utils.h"
#include "logic_rng.h"
#include "platform.h"

#if BLE_DEVICE_ROLE == BLE_ROLE_ALL
#ifndef ATT_DB_MEMORY
#define ATT_DB_MEMORY
#endif
#endif

#ifndef HOST_UART_BAUDRATE_CONFIG_VALUE
#define HOST_UART_BAUDRATE_CONFIG_VALUE 115200
#endif

#if defined ATT_DB_MEMORY
uint32_t att_db_data[BLE_ATT_DB_MEMORY_SIZE/4] = {0};
#endif

/* Refer Bluetooth Core Specification-4.1, Volume-3, Part-H, Section-2.4.6 */
bool volatile send_slave_security_flag = true;

bool volatile resolve_addr_flag = false;

ble_connected_dev_info_t ble_device_info;

static at_ble_addr_t ble_peripheral_dev_address;

volatile ble_device_ll_state_t ble_device_current_state = BLE_DEVICE_IDLE_STATE;

at_ble_connected_t connected_state_info;

at_ble_encryption_request_t temp_param[sizeof(at_ble_encryption_request_t)];

const ble_gap_event_cb_t *ble_mgr_gap_event_cb[MAX_GAP_EVENT_SUBSCRIBERS] = {NULL, };
const ble_gatt_client_event_cb_t *ble_mgr_gatt_client_event_cb[MAX_GATT_CLIENT_SUBSCRIBERS] = {NULL, };
const ble_gatt_server_event_cb_t *ble_mgr_gatt_server_event_cb[MAX_GATT_SERVER_SUBSCRIBERS] = {NULL, };
const ble_l2cap_event_cb_t *ble_mgr_l2cap_event_cb[MAX_L2CAP_EVENT_SUBSCRIBERS] = {NULL, };
const ble_htpt_event_cb_t *ble_mgr_htpt_event_cb[MAX_HTPT_EVENT_SUBSCRIBERS] = {NULL, };
const ble_dtm_event_cb_t *ble_mgr_dtm_event_cb[MAX_DTM_EVENT_SUBSCRIBERS] = {NULL, };
const ble_custom_event_cb_t *ble_mgr_custom_event_cb[MAX_CUSTOM_EVENT_SUBSCRIBERS] = {NULL, };

/* All BLE Manager GAP Event callback */
static const ble_gap_event_cb_t ble_mgr_gap_handle = {
    .undefined = ble_undefined_event_handler,
    .scan_info = ble_scan_info_handler,
    .scan_report = ble_scan_report_handler,
    .advt_cmp = NULL,
    .adv_report = NULL, /* This event not handled in BLE Manager */
    .rand_addr_changed = NULL, /* This event not handled in BLE Manager */
    .connected = ble_connected_state_handler,
    .disconnected = ble_disconnected_state_handler,
    .conn_parameter_update_done = ble_conn_param_update,
    .conn_param_update_request = ble_conn_param_update_req,
    .pair_done = ble_pair_done_handler,
    .pair_request = ble_pair_request_handler,
    .slave_sec_request = ble_slave_security_request_handler,
    .pair_key_request = ble_pair_key_request_handler,
    .encryption_request = ble_encryption_request_handler,
    .encryption_status_changed = ble_encryption_status_change_handler,
    .resolv_rand_addr_status = ble_resolv_rand_addr_handler,
    .sign_counters_ind = NULL,  /* This event not handled in BLE Manager */
    .peer_att_info_ind = NULL,  /* This event not handled in BLE Manager */
    .con_channel_map_ind = NULL /* This event not handled in BLE Manager */
};

/* All BLE Manager GATT Server Event callback */
static const ble_gatt_server_event_cb_t ble_mgr_gatt_server_handle = {  
    .notification_confirmed = NULL, /* This event not handled in BLE Manager */
    .indication_confirmed = NULL, /* This event not handled in BLE Manager */
    .characteristic_changed = NULL, /* This event not handled in BLE Manager */
    .characteristic_configuration_changed = NULL, /* This event not handled in BLE Manager */
    .service_changed_indication_sent = NULL, /* This event not handled in BLE Manager */
    .write_authorize_request = NULL, /* This event not handled in BLE Manager */
    .mtu_changed_indication = ble_mtu_changed_indication_handler,
    .mtu_changed_cmd_complete = ble_mtu_changed_cmd_complete_handler,
    .characteristic_write_cmd_cmp = ble_characteristic_write_cmd_complete_handler,
    .read_authorize_request = NULL /* This event not handled in BLE Manager */
};

/* All BLE Manager Custom Event callback */
static const ble_custom_event_cb_t ble_mgr_custom_event_handle = {
    .custom_event = NULL, /* This event not handled in BLE Manager */
    .device_dead = ble_device_dead_handler, 
    .device_out_of_sync = ble_device_out_of_sync_handler,  
    .device_ready = NULL,  /* This event not handled in BLE Manager */
    .event_max = NULL,   /* This event not handled in BLE Manager */    
};

volatile uint32_t scan_response_count = 0;
at_ble_scan_info_t scan_info[MAX_SCAN_DEVICE];

at_ble_events_t event;

/* allocate in words to avoid alignment issues */
uint32_t ble_event_params[BLE_EVENT_PARAM_MAX_SIZE/sizeof(uint32_t)];

#if (BLE_DEVICE_ROLE == BLE_ROLE_OBSERVER) && (SAMG55 || SAM4S)
uint32_t event_pool_memory[4] = {0};
#else
uint32_t event_pool_memory[256] = {0};
#endif
uint32_t event_params_memory[512] = {0};
uint32_t rx_fifo_memory[256] = {0};
    
/** @brief initializes the platform */
static void ble_init(at_ble_init_config_t * args);

/** @brief Set BLE Address, If address is NULL then it will use BD public address */
static void ble_set_dev_config(at_ble_addr_t *addr);

/** @brief function to get event from stack */
at_ble_status_t ble_event_task(void)
{
    if (at_ble_event_get(&event, ble_event_params, BLE_EVENT_TIMEOUT) == AT_BLE_SUCCESS) 
    {
            ble_event_manager(event, ble_event_params);
            return AT_BLE_SUCCESS;
    }
    
    return AT_BLE_FAILURE;
}

uint32_t ble_sdk_version(void)
{
    uint32_t fw_ver, rf_ver;
    if(at_ble_firmware_version_get(&fw_ver) == AT_BLE_SUCCESS)
    {
        /* Check the SDK and Library version compatibility */
        if ( (BLE_SDK_MAJOR_NO(fw_ver) == BLE_SDK_MAJOR_NO(BLE_SDK_VERSION)) && \
             (BLE_SDK_MINOR_NO(fw_ver) == BLE_SDK_MINOR_NO(BLE_SDK_VERSION)) )
        {
            DBG_LOG("BluSDK Firmware Version:%X.%X.%X", (uint8_t)BLE_SDK_MAJOR_NO(fw_ver), \
                                        ((uint8_t)BLE_SDK_MINOR_NO(fw_ver) + BLE_SDK_MINOR_NO_INC), (uint16_t)BLE_SDK_BUILD_NO(fw_ver));
        }
        else
        {
            DBG_LOG("Error:Library version doesn't match with SDK version. Please use %X.%X version of library", \
                    BLE_SDK_MAJOR_NO(BLE_SDK_VERSION), BLE_SDK_MINOR_NO(BLE_SDK_VERSION));
            fw_ver = 0;
        }
        
        if(at_ble_rf_version_get(&rf_ver) == AT_BLE_SUCCESS)
        {
            DBG_LOG_DEV("BTLC1000 RF Version:0x%8X", (unsigned int)rf_ver);
        }
        else
        {
            DBG_LOG_DEV("Error: Failed to get BTLC1000 RF Version");
            rf_ver = 0;
        }       
    }
    else
    {
        DBG_LOG("Error: Failed to get BluSDK Firmware Version");
        fw_ver = 0;
    }

    return fw_ver;
}

at_ble_init_config_t pf_cfg = {
    /* Register Platform callback API's */
    .platform_api_list.at_ble_create_timer = timer_create_callback_timer,
    .platform_api_list.at_ble_delete_timer = timer_remove_callback_timer,
    .platform_api_list.at_ble_start_timer = timer_start_callback_timer,
    .platform_api_list.at_ble_stop_timer = timer_stop_callback_timer,
    .platform_api_list.at_ble_sleep = platform_sleep,
    .platform_api_list.at_ble_gpio_set = logic_bluetooth_gpio_set,
    .platform_api_list.at_ble_send_sync = platform_send_sync,
    .platform_api_list.at_ble_recv_async = platform_recv_async,
    .platform_api_list.at_ble_reconfigure_usart = platform_configure_hw_fc_uart,
    .platform_api_list.at_ble_wait_for_signal = platform_wait_for_signal,
    .platform_api_list.at_ble_create_signal = platform_create_signal,
    .platform_api_list.at_ble_delete_signal = platform_delete_signal,
    .platform_api_list.at_ble_trigger_signal = platform_trigger_signal,
    .platform_api_list.at_ble_reset_signal = platform_reset_signal
};

/** @brief BLE device initialization */
void ble_device_init(at_ble_addr_t *addr, at_ble_gap_deviceinfo_t* device_info, dis_device_information_t* dis_device_info)
{
#ifdef BTLC_REINIT_SUPPORT
    static bool btlc1000_initialized = false;
#endif
   
    /* Reset bond info data */
    ble_clear_bond_info();  
    
#if defined ATT_DB_MEMORY
    pf_cfg.memPool.memSize = BLE_ATT_DB_MEMORY_SIZE;
    pf_cfg.memPool.memStartAdd = (uint8_t *)&att_db_data;
#else
    pf_cfg.memPool.memSize = 0;
    pf_cfg.memPool.memStartAdd = NULL;
#endif

    pf_cfg.event_mem_pool.memStartAdd        = (uint8_t *)event_pool_memory;
    pf_cfg.event_mem_pool.memSize            = sizeof(event_pool_memory);
    pf_cfg.event_params_mem_pool.memStartAdd = (uint8_t *)event_params_memory;
    pf_cfg.event_params_mem_pool.memSize     = sizeof(event_params_memory);
    pf_cfg.rx_fifo_mem_pool.memStartAdd      = (uint8_t *)rx_fifo_memory;
    pf_cfg.rx_fifo_mem_pool.memSize          = sizeof(rx_fifo_memory);

/* BTLC1000 XPro Module ZR/MR */
#if (BLE_MODULE == BTLC1000_ZR)
    pf_cfg.btlc1000_module_version           = AT_BTLC1000_ZR;

    DBG_LOG("BTLC1000 XPro Module: BTLC1000-ZR");
    #if ((UART_FLOWCONTROL_6WIRE_MODE == false) && (UART_FLOWCONTROL_4WIRE_MODE == true))
        DBG_LOG("BTLC1000 Host Interface UART Mode:4-Wire, Baudrate:%d", (unsigned int)HOST_UART_BAUDRATE_CONFIG_VALUE);
    #else
        DBG_LOG("Error: Invalid BTLC1000 Host Interface UART Mode, BTLC1000-ZR supports only 4-WIRE UART mode");
        return;
    #endif
#elif (BLE_MODULE == BTLC1000_MR)
    pf_cfg.btlc1000_module_version           = AT_BTLC1000_MR;

    DBG_LOG("BTLC1000 XPro Module: BTLC1000-MR");
    #if ((UART_FLOWCONTROL_6WIRE_MODE == false) && (UART_FLOWCONTROL_4WIRE_MODE == true))
        DBG_LOG("BTLC1000 Host Interface UART Mode:4-Wire(works only when eFuse enabled), Baudrate:%d", \
                                                            (unsigned int)HOST_UART_BAUDRATE_CONFIG_VALUE);
    #elif ((UART_FLOWCONTROL_6WIRE_MODE == true) && (UART_FLOWCONTROL_4WIRE_MODE == false))
        DBG_LOG("BTLC1000 Host Interface UART Mode:6-Wire(without Efuse Enabled), Baudrate:%d", \
                                                            (unsigned int)HOST_UART_BAUDRATE_CONFIG_VALUE);
    #else
        DBG_LOG("Error: Invalid BTLC1000 Host Interface UART Mode, BTLC1000-MR supports only 4-Wire or 6-Wire UART mode");
        return;
    #endif
#else
    DBG_LOG("Error: Select a Valid BTLC1000 XPro Module");
    return;
#endif
    
    /*Bus configuration*/
    pf_cfg.bus_info.bus_type = AT_BLE_UART;
    /// Select BTLC1000 GPIO for host wakeup, for Wing board select BTLC1000_EXT_PIN9 only, for SoC use the enum values
    pf_cfg.bus_info.host_external_wakeup_gpio = BTLC1000_EXT_PIN9;

#if ((UART_FLOWCONTROL_6WIRE_MODE == true) && (BLE_MODULE == BTLC1000_MR))
    /* Enable Hardware Flow-control on BTLC1000  using patch */
   pf_cfg.bus_info.btlc1000_uart_pinout_switch = true;
#else
    /* Use default Flowcontrol option on BTLC1000 */
   pf_cfg.bus_info.btlc1000_uart_pinout_switch = false;
#endif
   
    /// UART baudrate value one of @ref at_ble_uart_baudrate_tag values
    pf_cfg.bus_info.bus_baudrate = HOST_UART_BAUDRATE_CONFIG_VALUE;

    pf_cfg.platform_api_list.at_ble_reconfigure_usart = pf_cfg.bus_info.btlc1000_uart_pinout_switch ? platform_configure_hw_fc_uart : platform_configure_primary_uart;
    
    platform_init(pf_cfg.bus_info.bus_type, pf_cfg.bus_info.btlc1000_uart_pinout_switch);
    
    trace_set_level(TRACE_LVL_DISABLE);
    
    ble_init(&pf_cfg);
    
#ifdef BTLC_REINIT_SUPPORT
    if(!btlc1000_initialized)
    {
#endif
        /* Register it in first index of callback handler */
        ble_mgr_events_callback_handler(REGISTER_CALL_BACK, 
                                        BLE_GAP_EVENT_TYPE, 
                                        &ble_mgr_gap_handle);
        ble_mgr_events_callback_handler(REGISTER_CALL_BACK,
                                        BLE_GATT_SERVER_EVENT_TYPE,
                                        &ble_mgr_gatt_server_handle);
        ble_mgr_events_callback_handler(REGISTER_CALL_BACK,
                                        BLE_CUSTOM_EVENT_TYPE,
                                        &ble_mgr_custom_event_handle);
#ifdef BTLC_REINIT_SUPPORT
        btlc1000_initialized = true;
    }
#endif

    /* Set advanced info */
    if(at_ble_set_gap_deviceinfo(device_info) != AT_BLE_SUCCESS)
    {
        DBG_LOG("ERROR: Couldn't set advanced info");
    }
                                    
    char* dev_name = (char *)dis_device_info->custom_device_name;
    if (ble_set_device_name((uint8_t *)dev_name, strlen(dev_name)) != AT_BLE_SUCCESS)
    {
        DBG_LOG("Device name set failed");
    }
    
    ble_set_dev_config(addr);

    ble_sdk_version();                      
}

/** @brief set device name to BLE Device*/
at_ble_status_t ble_set_device_name(uint8_t *name, uint8_t name_len)
{
    if ((name == NULL) || (name_len < 1))
    {
        return AT_BLE_INVALID_PARAM;
    }
    return at_ble_device_name_set(name, name_len);
}

/* Initialize the BLE */
static void ble_init(at_ble_init_config_t * args)
{
    /* Initialize the platform */
    DBG_LOG("Initializing BTLC1000");

    /* Init BLE device */
    if(at_ble_init(args) != AT_BLE_SUCCESS)
    {
        DBG_LOG("BTLC1000 Initialization failed");
        DBG_LOG("Please check the configuration and connection / hardware connector");  
        while(1);
    }
    else
    {
        uint32_t chip_id = 0xFFFFFFFF;
        if (at_ble_chip_id_get(&chip_id) == AT_BLE_SUCCESS)
        {
            DBG_LOG("BTLC1000 Chip ID: 0x%6X", (unsigned int)chip_id);
        }
        else
        {
            DBG_LOG("BTLC1000 Chip identification failed");
            while(1);
        }
    }
}


/* Set BLE Address and device configuration, If address is NULL then it will use BD public address */
static void ble_set_dev_config(at_ble_addr_t *addr)
{
    at_ble_dev_config_t stDevConfig;
    at_ble_addr_t address = {AT_BLE_ADDRESS_PUBLIC, {0xAB, 0xCD, 0xEF, 0xAB, 0xCD, 0xEF}};
    
    if (addr == NULL)
    {       
        /* get BD address from BLE device */
        if(at_ble_addr_get(&address) != AT_BLE_SUCCESS)
        {
            DBG_LOG("BD address get failed");
        }
        
        /* Copy the BD address into address pointer */
        addr = &address;
    }
    
    DBG_LOG("BD Address:0x%02X%02X%02X%02X%02X%02X, Address Type:%d",
    addr->addr[5],
    addr->addr[4],
    addr->addr[3],
    addr->addr[2],
    addr->addr[1],
    addr->addr[0], addr->type);
    
    /* Set device configuration */
    /* Device role */
    stDevConfig.role = (at_ble_dev_role_t)BLE_DEVICE_ROLE;
    /* device renew duration */
    stDevConfig.renew_dur = AT_RENEW_DUR_VAL_MIN;
    /* device address type */
    memcpy((uint8_t *)&stDevConfig.address, (uint8_t *)addr, sizeof(at_ble_addr_t));
    /* Attributes */
    stDevConfig.att_cfg.b2NamePerm = AT_BLE_WRITE_DISABLE;
    stDevConfig.att_cfg.b2AppearancePerm = AT_BLE_WRITE_DISABLE;
    stDevConfig.att_cfg.b1EnableSpcs = 1;
    stDevConfig.att_cfg.b1EnableServiceChanged = 1;
    stDevConfig.att_cfg.b2Rfu = AT_BLE_WRITE_DISABLE;
    /* Handles */
    stDevConfig.gap_start_hdl = AT_BLE_AUTO_ALLOC_HANDLE;
    stDevConfig.gatt_start_hdl = AT_BLE_AUTO_ALLOC_HANDLE;
    /* MTU */
    stDevConfig.max_mtu = AT_MTU_VAL_RECOMMENDED;
    
    if(at_ble_set_dev_config(&stDevConfig) != AT_BLE_SUCCESS)
    {
        DBG_LOG("Set BLE Device configuration failed");
    }
    
    if (at_ble_addr_set(addr) != AT_BLE_SUCCESS)
    {
        DBG_LOG("Set BLE Device Address failed");
    }
}

/**@ingroup ble_manager_group
 * @brief The structure of BLE Event callback pointer will be registered or unregistered dynamically.
 *
 * The callback pointers are registered according to the group type of BLE Events
 * request is received @ref AT_BLE_LECB_CONN_REQ
 *
 * @param[in] event_cb_type Register/Unregister the callback functions
 * @param[in] event_type type of BLE event
 * @param[in] ble_event_handler structure of function pointers for the event
 *
 * @return Upon successful completion the function shall return @ref true,
 * Otherwise the function shall return @ref false
 */
///@cond IGNORE_DOXYGEN
BLU_SDK_API
///@endcond
bool ble_mgr_events_callback_handler(ble_mgr_event_cb_t event_cb_type, 
                                    ble_mgr_event_t event_type,
                                    const void *ble_event_handler)
{
    uint32_t idx;
    bool status = false;
    if (ble_event_handler != NULL)
    {
        switch(event_type)
        {
            case BLE_GAP_EVENT_TYPE:
            {
                ble_gap_event_cb_t *ble_gap_event_cb;
                ble_gap_event_cb = (ble_gap_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_GAP_EVENT_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_gap_event_cb[idx] == ble_gap_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_gap_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }                                               
                    else if((ble_mgr_gap_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_gap_event_cb[idx] = ble_gap_event_cb;
                        status = true;
                        break;
                    }
                }
            }
            break;
            /* Callbacks for custom event type */
            case BLE_GATT_CLIENT_EVENT_TYPE:
            {
                ble_gatt_client_event_cb_t *ble_gatt_client_event_cb;
                ble_gatt_client_event_cb = (ble_gatt_client_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_GATT_CLIENT_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_gatt_client_event_cb[idx] == ble_gatt_client_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_gatt_client_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }
                    else if((ble_mgr_gatt_client_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_gatt_client_event_cb[idx] = ble_gatt_client_event_cb;
                        status = true;
                        break;
                    }
                }
            }
            break;
            /* Callbacks for GATT Server event type */
            case BLE_GATT_SERVER_EVENT_TYPE:
            {
                ble_gatt_server_event_cb_t *ble_gatt_server_event_cb;
                ble_gatt_server_event_cb = (ble_gatt_server_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_GATT_SERVER_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_gatt_server_event_cb[idx] == ble_gatt_server_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_gatt_server_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }
                    else if((ble_mgr_gatt_server_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_gatt_server_event_cb[idx] = ble_gatt_server_event_cb;
                        status = true;
                        break;
                    }
                }
            }
            break;
            /* Callbacks for L2CAP event type */
            case BLE_L2CAP_EVENT_TYPE:
            {
                ble_l2cap_event_cb_t *ble_l2cap_event_cb;
                ble_l2cap_event_cb = (ble_l2cap_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_L2CAP_EVENT_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_l2cap_event_cb[idx] == ble_l2cap_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_l2cap_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }
                    else if((ble_mgr_l2cap_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_l2cap_event_cb[idx] = ble_l2cap_event_cb;
                        status = true;
                        break;
                    }
                }
            }
            break;
            /* Callbacks for HTPT event type */
            case BLE_GATT_HTPT_EVENT_TYPE:
            {
                ble_htpt_event_cb_t *ble_htpt_event_cb;
                ble_htpt_event_cb = (ble_htpt_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_HTPT_EVENT_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_htpt_event_cb[idx] == ble_htpt_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_htpt_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }
                    else if((ble_mgr_htpt_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_htpt_event_cb[idx] = ble_htpt_event_cb;
                        status = true; 
                        break;
                    }
                }
            }
            break;
            /* Callbacks for DTM event type */
            case BLE_DTM_EVENT_TYPE:
            {
                ble_dtm_event_cb_t *ble_dtm_event_cb;
                ble_dtm_event_cb = (ble_dtm_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_DTM_EVENT_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_dtm_event_cb[idx] == ble_dtm_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_dtm_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }
                    else if((ble_mgr_dtm_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_dtm_event_cb[idx] = ble_dtm_event_cb;
                        status = true;
                        break;
                    }
                }
            }
            break;
            /* Callbacks for custom event type */   
            case BLE_CUSTOM_EVENT_TYPE:
            {
                ble_custom_event_cb_t *ble_custom_event_cb;
                ble_custom_event_cb = (ble_custom_event_cb_t *)ble_event_handler;
                
                for (idx = 0; idx < MAX_CUSTOM_EVENT_SUBSCRIBERS; idx++)
                {
                    if (ble_mgr_custom_event_cb[idx] == ble_custom_event_cb)
                    {
                        if (event_cb_type == REGISTER_CALL_BACK)
                        {
                            /* Do not add duplicate entries into the table */
                            status = true;
                            break;
                        }
                        else if (event_cb_type == UNREGISTER_CALL_BACK)
                        {
                            /* remove callback from table */
                            ble_mgr_custom_event_cb[idx] = NULL;
                            status = true;
                            break;
                        }
                    }
                    else if((ble_mgr_custom_event_cb[idx] == NULL) && (event_cb_type == REGISTER_CALL_BACK))
                    {
                        /* add callback to table */
                        ble_mgr_custom_event_cb[idx] = ble_custom_event_cb;
                        status = true;
                        break;
                    }
                }
            }
            break;
            
            default:            
            break;
        }
    }
    return status;
}

at_ble_status_t ble_undefined_event_handler(void *params)
{
    DBG_LOG_DEV("Undefined Event Received");
    ALL_UNUSED(params);
    return AT_BLE_SUCCESS;
}

/** @brief request the peer device for connection */
at_ble_status_t gap_dev_connect(at_ble_addr_t *dev_addr)
{
    at_ble_connection_params_t gap_conn_parameter;
    
    memcpy((uint8_t *)&ble_peripheral_dev_address, (uint8_t *)dev_addr, sizeof(at_ble_addr_t));

    gap_conn_parameter.con_intv_min = GAP_CONN_INTERVAL_MIN;
    gap_conn_parameter.con_intv_max = GAP_CONN_INTERVAL_MAX;
    gap_conn_parameter.con_latency = GAP_CONN_SLAVE_LATENCY;
    gap_conn_parameter.ce_len_min = GAP_CE_LEN_MIN;
    gap_conn_parameter.ce_len_max = GAP_CE_LEN_MAX;
    gap_conn_parameter.superv_to = GAP_SUPERVISION_TIMOUT;

    return (at_ble_connect(dev_addr, GAP_CONNECT_PEER_COUNT, SCAN_INTERVAL, SCAN_WINDOW, &gap_conn_parameter));
}

/** @brief instructs device to start scanning */
at_ble_status_t gap_dev_scan(void)
{
    ble_device_current_state = CENTRAL_SCANNING_STATE;
    /* Device Scan discover started*/
    DBG_LOG("Scanning...Please wait...");
    /* make service discover counter to zero*/
    scan_response_count = 0;
    #if BLE_DEVICE_ROLE == BLE_ROLE_OBSERVER
    return(at_ble_scan_start(SCAN_INTERVAL, SCAN_WINDOW, SCAN_TIMEOUT, SCAN_TYPE, AT_BLE_SCAN_OBSERVER_MODE, false,false)) ;
    #else
    return(at_ble_scan_start(SCAN_INTERVAL, SCAN_WINDOW, SCAN_TIMEOUT, SCAN_TYPE, AT_BLE_SCAN_GEN_DISCOVERY, false,true)) ;
    #endif
}

/** @brief function handling scaned information */
at_ble_status_t ble_scan_info_handler(void *params)
{
    at_ble_scan_info_t *scan_param;
    scan_param = (at_ble_scan_info_t *)params;
    #if BLE_DEVICE_ROLE == BLE_ROLE_OBSERVER
        // store the advertising report data into scan_info[]
        memcpy((uint8_t *)scan_info, scan_param, sizeof(at_ble_scan_info_t));
        DBG_LOG_DEV("Info:Device found address [%ld]  0x%02X%02X%02X%02X%02X%02X ",
        scan_response_count,
        scan_param->dev_addr.addr[5],
        scan_param->dev_addr.addr[4],
        scan_param->dev_addr.addr[3],
        scan_param->dev_addr.addr[2],
        scan_param->dev_addr.addr[1],
        scan_param->dev_addr.addr[0]);
        scan_response_count++;
        return AT_BLE_SUCCESS;
    #else
        if(scan_response_count < MAX_SCAN_DEVICE)
        {
            // store the advertising report data into scan_info[]
            memcpy((uint8_t *)&scan_info[scan_response_count], scan_param, sizeof(at_ble_scan_info_t));
            DBG_LOG_DEV("Info:Device found address [%d]  0x%02X%02X%02X%02X%02X%02X ",
            scan_response_count,
            scan_param->dev_addr.addr[5],
            scan_param->dev_addr.addr[4],
            scan_param->dev_addr.addr[3],
            scan_param->dev_addr.addr[2],
            scan_param->dev_addr.addr[1],
            scan_param->dev_addr.addr[0]);
            scan_response_count++;
            return AT_BLE_SUCCESS;
        }
        else
        {
            DBG_LOG("Info:maximum no.of scan device reached...Stopping Scan");
            if(at_ble_scan_stop() != AT_BLE_SUCCESS)
            {
                DBG_LOG("Failed to stop scanning");
            }
        
            return AT_BLE_FAILURE;
        }
    #endif
}

/** @brief function handles scan report */
at_ble_status_t ble_scan_report_handler(void *params)
{
    at_ble_scan_report_t *scan_report;
    ble_device_current_state = BLE_DEVICE_IDLE_STATE;
    scan_report = (at_ble_scan_report_t *)params;
    if (scan_report->status == AT_BLE_SUCCESS)
    {
        /* All scan data should be handled */
        return AT_BLE_SUCCESS;
    }
    else
    {
        DBG_LOG("Scanning  failed");
    }
    return AT_BLE_FAILURE;
}

/* Parse the received advertising data for service and local name */
at_ble_status_t scan_info_parse(at_ble_scan_info_t *scan_info_data,
                at_ble_uuid_t *ble_service_uuid, uint8_t adv_type)
{
    if (scan_info_data->adv_data_len) {
        uint8_t adv_data_size;
        uint8_t index = 0;
        volatile adv_element_t *adv_element_p;
        volatile adv_element_t adv_element_data;

        adv_data_size = scan_info_data->adv_data_len;
        adv_element_data.len = 0;
        while (adv_data_size) {
            adv_element_data.len = scan_info_data->adv_data[index];
            adv_element_data.type
            = scan_info_data->adv_data[index + 1];
            adv_element_data.data
            = &scan_info_data->adv_data[index + 2];
            adv_element_p = &adv_element_data;

            if (adv_element_p->type == adv_type) {
                /* passing the length of data type */
                uint8_t adv_type_size = adv_element_p->len;
                /* actual size of the data  */
                adv_type_size -= 1;
                while (adv_type_size) {
                    volatile int cmp_status = -1;
                    if (ble_service_uuid->type ==
                    AT_BLE_UUID_16) {
                        cmp_status = memcmp(
                        adv_element_p->data, ble_service_uuid->uuid,
                        AT_BLE_UUID_16_LEN);
                        adv_element_p->data
                        += AT_BLE_UUID_16_LEN;
                        adv_type_size
                        -= AT_BLE_UUID_16_LEN;
                    } else if (ble_service_uuid->type ==
                    AT_BLE_UUID_32) {
                        cmp_status = memcmp(
                        adv_element_p->data, ble_service_uuid->uuid,
                        AT_BLE_UUID_32_LEN);
                        adv_element_p->data
                        += AT_BLE_UUID_32_LEN;
                        adv_type_size
                        -= AT_BLE_UUID_32_LEN;
                    } else if (ble_service_uuid->type ==
                    AT_BLE_UUID_128) {
                        cmp_status = memcmp(
                        adv_element_p->data, ble_service_uuid->uuid,
                        AT_BLE_UUID_128_LEN);
                        adv_element_p->data
                        += AT_BLE_UUID_128_LEN;
                        adv_type_size
                        -= AT_BLE_UUID_32_LEN;
                    }

                    if (cmp_status == 0) {
                        return AT_BLE_SUCCESS;
                    }
                }
            }

            index += (adv_element_data.len + 1);
            adv_element_data.len += 1;
            adv_data_size -= adv_element_data.len;
        }
    }

    return AT_BLE_FAILURE;
}

/** @brief function to send slave security request */
at_ble_status_t ble_send_slave_sec_request(at_ble_handle_t conn_handle)
{
    #if BLE_PAIR_ENABLE
        if (at_ble_send_slave_sec_request(conn_handle, BLE_MITM_REQ, BLE_BOND_REQ) == AT_BLE_SUCCESS)
        {
            DBG_LOG_DEV("Slave security request successful");
            return AT_BLE_SUCCESS;
        }
        else
        {
            DBG_LOG("Slave security request failed");
        }
    #endif
        ALL_UNUSED(conn_handle);
    return AT_BLE_FAILURE;
}

bool ble_check_ispheripheral(at_ble_handle_t handle)
{
    at_ble_dev_role_t dev_role;
    
    if(ble_connected_device_role(handle, &dev_role) == AT_BLE_SUCCESS)
    {
        if (dev_role == AT_BLE_ROLE_PERIPHERAL)
        {
            return true;
        }
    }
    return false;
}

bool ble_check_iscentral(at_ble_handle_t handle)
{
    at_ble_dev_role_t dev_role = AT_BLE_ROLE_NONE;
    
    if(ble_connected_device_role(handle, &dev_role) == AT_BLE_SUCCESS)
    {
        if (dev_role == AT_BLE_ROLE_CENTRAL)
        {
            return true;
        }
    }
    return false;
}

bool ble_check_disconnected_iscentral(at_ble_handle_t handle)
{
    at_ble_dev_role_t dev_role;
    
    if(ble_disconnected_device_role(handle, &dev_role) == AT_BLE_SUCCESS)
    {
        if (dev_role == AT_BLE_ROLE_CENTRAL)
        {
            return true;
        }
    }
    return false;
}

bool ble_check_disconnected_isperipheral(at_ble_handle_t handle)
{
    at_ble_dev_role_t dev_role;
    
    if(ble_disconnected_device_role(handle, &dev_role) == AT_BLE_SUCCESS)
    {
        if (dev_role == AT_BLE_ROLE_PERIPHERAL)
        {
            return true;
        }
    }
    return false;
}

at_ble_status_t ble_connected_device_role(at_ble_handle_t conn_handle, at_ble_dev_role_t *dev_role)
{
    at_ble_status_t status = AT_BLE_FAILURE;
    *dev_role = AT_BLE_ROLE_NONE;
    if((ble_device_info.conn_state != BLE_DEVICE_DEFAULT_IDLE) && (ble_device_info.conn_state != BLE_DEVICE_DISCONNECTED) && (ble_device_info.conn_info.handle == conn_handle))
    {
        *dev_role = ble_device_info.dev_role;
        status = AT_BLE_SUCCESS;
    }
    return status;
}

at_ble_status_t ble_disconnected_device_role(at_ble_handle_t conn_handle, at_ble_dev_role_t *dev_role)
{
    at_ble_status_t status = AT_BLE_FAILURE;
    *dev_role = AT_BLE_ROLE_NONE;
    if(((ble_device_info.conn_state == BLE_DEVICE_DEFAULT_IDLE) || (ble_device_info.conn_state == BLE_DEVICE_DISCONNECTED)) && (ble_device_info.conn_info.handle == conn_handle))
    {
        *dev_role = ble_device_info.dev_role;
        status = AT_BLE_SUCCESS;
    }
    return status;
}

at_ble_status_t ble_check_device_state(at_ble_handle_t conn_handle, ble_device_state_t state)
{
    at_ble_status_t status = AT_BLE_FAILURE;
    if((ble_device_info.conn_state == state) && (ble_device_info.conn_info.handle == conn_handle))
    {
        status = AT_BLE_SUCCESS;
    }
    return status;
}

void ble_disconnect_all_devices(void)
{
    /* Disconnect from devices */
    if ((ble_device_info.conn_state != BLE_DEVICE_DEFAULT_IDLE) && (ble_device_info.conn_state != BLE_DEVICE_DISCONNECTED))
    {
        at_ble_disconnect(ble_device_info.conn_info.handle, AT_BLE_TERMINATED_BY_USER);
    }    
}

void ble_clear_bond_info(void)
{
    /* Delete all pairing data */
    memset(&ble_device_info, 0, sizeof(ble_device_info));
    ble_device_info.dev_role = AT_BLE_ROLE_NONE;
    ble_device_info.conn_state = BLE_DEVICE_DEFAULT_IDLE;
    ble_device_info.bond_info.status = AT_BLE_GAP_INVALID_PARAM;
    ble_device_info.conn_info.handle = BLE_INVALID_CONNECTION_HANDLE;
}

/** @brief function to handle connected event received from stack */
at_ble_status_t ble_connected_state_handler(void *params)
{
    nodemgmt_bluetooth_bonding_information_t recalled_bonding_info;
    at_ble_connected_t *conn_params;
    conn_params = (at_ble_connected_t *)params;
    bool peripheral_device_added = false;
    
    /* Only allow connections if we're not connected to anything else */
    if ((ble_device_info.conn_state != BLE_DEVICE_DEFAULT_IDLE) && (ble_device_info.conn_state != BLE_DEVICE_DISCONNECTED))
    {
        at_ble_disconnect(connected_state_info.handle, AT_BLE_TERMINATED_BY_USER);
        DBG_LOG("We're already connected to someone else... bye!");
        return AT_BLE_FAILURE;
    }
    
    if (conn_params->conn_status == AT_BLE_SUCCESS)
    {
        DBG_LOG("Connection Handle %d", conn_params->handle);
        DBG_LOG("Connected to peer device with address 0x%02x%02x%02x%02x%02x%02x",
        conn_params->peer_addr.addr[5],
        conn_params->peer_addr.addr[4],
        conn_params->peer_addr.addr[3],
        conn_params->peer_addr.addr[2],
        conn_params->peer_addr.addr[1],
        conn_params->peer_addr.addr[0]);        
        memcpy((uint8_t *)&connected_state_info, (uint8_t *)conn_params, sizeof(at_ble_connected_t));   
        
        if (conn_params->peer_addr.type == AT_BLE_ADDRESS_PUBLIC)
        {
            resolve_addr_flag = false;
            
            /* Check for temp ban */
            if (logic_bluetooth_is_device_temp_banned(conn_params->peer_addr.addr) != FALSE)
            {
                /* Device temporarily banned, disconnect it */
                at_ble_disconnect(connected_state_info.handle, AT_BLE_TERMINATED_BY_USER);
                DBG_LOG("Device temporarily banned... bye!");
                // Try AT_BLE_LL_CON_LIMIT_EXCEED ?
                return AT_BLE_FAILURE;
            }
                   
            /* Store address we could temp ban later */
            logic_bluetooth_store_temp_ban_connected_address(conn_params->peer_addr.addr);
            
            if (comms_main_mcu_fetch_bonding_info_for_mac(conn_params->peer_addr.type, conn_params->peer_addr.addr, &recalled_bonding_info) == RETURN_OK)
            {
                /* Our dear MCU knows that device */
                ble_device_info.conn_state = BLE_DEVICE_CONNECTED;
                ble_device_info.bond_info.status = AT_BLE_GAP_INVALID_PARAM;
                DBG_LOG("Successfully recalled bonding info from MAIN MCU");
                memcpy(&ble_device_info.conn_info, (uint8_t *)&connected_state_info, sizeof(at_ble_connected_t));
                
                /***********************/
                /* Bonding information */
                /***********************/
                
                /* General stuff */
                ble_device_info.bond_info.auth = recalled_bonding_info.auth_type;
                ble_device_info.conn_info.peer_addr.type = recalled_bonding_info.address_resolv_type;
                memcpy(ble_device_info.conn_info.peer_addr.addr, recalled_bonding_info.mac_address, sizeof(recalled_bonding_info.mac_address));
                
                /* Peer LTK */
                memcpy(ble_device_info.bond_info.peer_ltk.key, recalled_bonding_info.peer_ltk_key, sizeof(recalled_bonding_info.peer_ltk_key));
                ble_device_info.bond_info.peer_ltk.ediv = recalled_bonding_info.peer_ltk_ediv;
                memcpy(ble_device_info.bond_info.peer_ltk.nb, recalled_bonding_info.peer_ltk_random_nb, sizeof(recalled_bonding_info.peer_ltk_random_nb));
                ble_device_info.bond_info.peer_ltk.key_size = recalled_bonding_info.peer_ltk_key_size;
                
                /* CSRK */
                memcpy(ble_device_info.bond_info.peer_csrk.key, recalled_bonding_info.peer_csrk_key, sizeof(recalled_bonding_info.peer_csrk_key));
                
                /* IRK */
                memcpy(ble_device_info.bond_info.peer_irk.key, recalled_bonding_info.peer_irk_key, sizeof(recalled_bonding_info.peer_irk_key));
                ble_device_info.bond_info.peer_irk.addr.type = recalled_bonding_info.peer_irk_resolv_type;
                memcpy(ble_device_info.bond_info.peer_irk.addr.addr, recalled_bonding_info.peer_irk_address, sizeof(recalled_bonding_info.peer_irk_address));
                
                /* Host LTK & CSRK */
                memcpy(ble_device_info.host_ltk.key, recalled_bonding_info.host_ltk_key, sizeof(recalled_bonding_info.host_ltk_key));
                ble_device_info.host_ltk.ediv = recalled_bonding_info.host_ltk_ediv;
                memcpy(ble_device_info.host_ltk.nb, recalled_bonding_info.host_ltk_random_nb, sizeof(recalled_bonding_info.host_ltk_random_nb));
                ble_device_info.host_ltk.key_size = recalled_bonding_info.host_ltk_key_size;
                memcpy(ble_device_info.host_csrk.key, recalled_bonding_info.host_csrk_key, sizeof(recalled_bonding_info.host_csrk_key));
            }
            else
            {
                /* New connection ! */
                memcpy(&ble_device_info.conn_info, (uint8_t *)conn_params, sizeof(at_ble_connected_t));
                ble_device_info.conn_state = BLE_DEVICE_CONNECTED;
                DBG_LOG("Allowing new connection to unknown device");
            }
            
            if(!memcmp((uint8_t *)&ble_peripheral_dev_address, (uint8_t *)&conn_params->peer_addr, sizeof(at_ble_addr_t)))
            {
                ble_device_info.dev_role = AT_BLE_ROLE_CENTRAL;
                DBG_LOG("Central role");
            }
            else
            {
                ble_device_current_state = PERIPHERAL_CONNECTED_STATE;
                ble_device_info.dev_role = AT_BLE_ROLE_PERIPHERAL;
                peripheral_device_added = true;
                DBG_LOG("Peripheral role");
            }
        }
        else if((conn_params->peer_addr.type == AT_BLE_ADDRESS_RANDOM_PRIVATE_RESOLVABLE) && (memcmp((uint8_t *)&ble_peripheral_dev_address, (uint8_t *)&conn_params->peer_addr, sizeof(at_ble_addr_t))))
        {            
            uint8_t* irk_keys_buffer;
            uint16_t nb_irk_keys = comms_main_mcu_get_bonding_info_irks(&irk_keys_buffer);
            DBG_LOG_DEV("Received %d IRK keys from main MCU", nb_irk_keys);
            for (uint16_t i=0; i < nb_irk_keys; i++)
            {
                DBG_LOG("IRK: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",irk_keys_buffer[i*16+0],irk_keys_buffer[i*16+1],irk_keys_buffer[i*16+2],irk_keys_buffer[i*16+3],irk_keys_buffer[i*16+4],irk_keys_buffer[i*16+5],irk_keys_buffer[i*16+6],irk_keys_buffer[i*16+7],irk_keys_buffer[i*16+8],irk_keys_buffer[i*16+9],irk_keys_buffer[i*16+10],irk_keys_buffer[i*16+11],irk_keys_buffer[i*16+12],irk_keys_buffer[i*16+13],irk_keys_buffer[i*16+14],irk_keys_buffer[i*16+15]);
            }
            
            if (nb_irk_keys == 0)
            {
                DBG_LOG("No IRK keys gotten");
                resolve_addr_flag = false;
                return AT_BLE_FAILURE;
            }
            
            if(at_ble_random_address_resolve((uint8_t)nb_irk_keys, &conn_params->peer_addr, irk_keys_buffer) == AT_BLE_SUCCESS)
            {
                DBG_LOG_DEV("Resolving Random address success**");
                resolve_addr_flag = true;
                return AT_BLE_SUCCESS;
            }
            else
            {
                DBG_LOG("Resolving Random address failed**");
                resolve_addr_flag = false;
                return AT_BLE_FAILURE;
            }
        }       

        /* Reset the ble_peripheral_dev_address to identify the initiator */
        memset((uint8_t *)&ble_peripheral_dev_address, 0, sizeof(at_ble_addr_t));
        
#if ((BLE_DEVICE_ROLE == BLE_ROLE_PERIPHERAL) || (BLE_DEVICE_ROLE == BLE_ROLE_ALL))
        /* Do not send security request, let the host make one once it wants to connect to protected characteristics */
        /*if((ble_device_info.dev_role == AT_BLE_ROLE_PERIPHERAL) && (peripheral_device_added))
        {
            ble_send_slave_sec_request(conn_params->handle);
        }*/
#endif
    } 
    else
    {
        DBG_LOG("Device Connection Failed - Status: %d", conn_params->conn_status);
    }
    ALL_UNUSED(peripheral_device_added);
    return AT_BLE_SUCCESS;
}

at_ble_status_t ble_resolv_rand_addr_handler(void *params)
{
    at_ble_resolv_rand_addr_status_t* ble_resolv_rand_addr_status = (at_ble_resolv_rand_addr_status_t *)params;
    nodemgmt_bluetooth_bonding_information_t recalled_bonding_info;
    bool peripheral_device_added = false;
    bool device_found = false;
    
    if(ble_resolv_rand_addr_status->status == AT_BLE_SUCCESS)
    {
        DBG_LOG_DEV("ble_resolv_rand_addr_handler: success");
            
        /* Ask our dear MCU */
        if (comms_main_mcu_fetch_bonding_info_for_irk((uint8_t*)ble_resolv_rand_addr_status->irk, &recalled_bonding_info) == RETURN_OK)
        {
            DBG_LOG_DEV("Main MCU knows IRK key");
            
            ble_device_info.bond_info.status = AT_BLE_GAP_INVALID_PARAM;
            ble_device_info.conn_state = BLE_DEVICE_CONNECTED;
            device_found = true;
                
            /***********************/
            /* Bonding information */
            /***********************/
                
            /* General stuff */
            ble_device_info.bond_info.auth = recalled_bonding_info.auth_type;
            ble_device_info.conn_info.peer_addr.type = recalled_bonding_info.address_resolv_type;
            memcpy(ble_device_info.conn_info.peer_addr.addr, recalled_bonding_info.mac_address, sizeof(recalled_bonding_info.mac_address));
                
            /* Peer LTK */
            memcpy(ble_device_info.bond_info.peer_ltk.key, recalled_bonding_info.peer_ltk_key, sizeof(recalled_bonding_info.peer_ltk_key));
            ble_device_info.bond_info.peer_ltk.ediv = recalled_bonding_info.peer_ltk_ediv;
            memcpy(ble_device_info.bond_info.peer_ltk.nb, recalled_bonding_info.peer_ltk_random_nb, sizeof(recalled_bonding_info.peer_ltk_random_nb));
            ble_device_info.bond_info.peer_ltk.key_size = recalled_bonding_info.peer_ltk_key_size;
                
            /* CSRK */
            memcpy(ble_device_info.bond_info.peer_csrk.key, recalled_bonding_info.peer_csrk_key, sizeof(recalled_bonding_info.peer_csrk_key));
                
            /* IRK */
            memcpy(ble_device_info.bond_info.peer_irk.key, recalled_bonding_info.peer_irk_key, sizeof(recalled_bonding_info.peer_irk_key));
            ble_device_info.bond_info.peer_irk.addr.type = recalled_bonding_info.peer_irk_resolv_type;
            memcpy(ble_device_info.bond_info.peer_irk.addr.addr, recalled_bonding_info.peer_irk_address, sizeof(recalled_bonding_info.peer_irk_address));
                
            /* Host LTK & CSRK */
            memcpy(ble_device_info.host_ltk.key, recalled_bonding_info.host_ltk_key, sizeof(recalled_bonding_info.host_ltk_key));
            ble_device_info.host_ltk.ediv = recalled_bonding_info.host_ltk_ediv;
            memcpy(ble_device_info.host_ltk.nb, recalled_bonding_info.host_ltk_random_nb, sizeof(recalled_bonding_info.host_ltk_random_nb));
            ble_device_info.host_ltk.key_size = recalled_bonding_info.host_ltk_key_size;
            memcpy(ble_device_info.host_csrk.key, recalled_bonding_info.host_csrk_key, sizeof(recalled_bonding_info.host_csrk_key));
            
            /* Check for temp ban */
            if ((connected_state_info.peer_addr.type != AT_BLE_ADDRESS_PUBLIC) && (logic_bluetooth_is_device_temp_banned(ble_device_info.bond_info.peer_irk.addr.addr) != FALSE))
            {
                /* Device temporarily banned, disconnect it */
                at_ble_disconnect(connected_state_info.handle, AT_BLE_TERMINATED_BY_USER);
                DBG_LOG("Device temporarily banned... bye!");
                // Try AT_BLE_LL_CON_LIMIT_EXCEED ?
                return AT_BLE_FAILURE;
            }
            
            if (connected_state_info.peer_addr.type != AT_BLE_ADDRESS_PUBLIC)
            {
                /* Store address we could temp ban later */
                logic_bluetooth_store_temp_ban_connected_address(ble_device_info.bond_info.peer_irk.addr.addr);
            }
        }   
        else
        {
            DBG_LOG_DEV("Main MCU doesn't know IRK key");
        }               
    }
    else
    {
        DBG_LOG_DEV("ble_resolv_rand_addr_handler: no success");
    }        
    
    if (device_found)
    {
        DBG_LOG_DEV("##########Device Found");
        if((ble_device_info.conn_state == BLE_DEVICE_DEFAULT_IDLE) || (ble_device_info.conn_state == BLE_DEVICE_DISCONNECTED))
        {
            ble_device_info.conn_state = BLE_DEVICE_CONNECTED;  
        }
        ble_device_info.dev_role = AT_BLE_ROLE_PERIPHERAL;
        memcpy((uint8_t *)&ble_device_info.conn_info, (uint8_t *)&connected_state_info, sizeof(at_ble_connected_t));
        peripheral_device_added = true;     
    }
    else
    {
        DBG_LOG_DEV("##########Device Not Found");
        memcpy(&ble_device_info.conn_info, (uint8_t *)&connected_state_info, sizeof(at_ble_connected_t));
        ble_device_info.conn_state = BLE_DEVICE_CONNECTED;
        ble_device_info.dev_role = AT_BLE_ROLE_PERIPHERAL;
        peripheral_device_added = true;
    }
    if(!resolve_addr_flag)
    {
        send_slave_security_flag = false;
        ble_encryption_request_handler((void *)temp_param);
    }
    else
    {
        resolve_addr_flag = false;
    }
    
    #if ((BLE_DEVICE_ROLE == BLE_ROLE_PERIPHERAL) || (BLE_DEVICE_ROLE == BLE_ROLE_ALL))
    /* Do not send security request, let the host make one once it wants to connect to protected characteristics */
    /*if(peripheral_device_added)
    {
        if((send_slave_security_flag) && (ble_device_info.dev_role == AT_BLE_ROLE_PERIPHERAL))
        {
            ble_send_slave_sec_request(connected_state_info.handle);
        }       
    }*/
    
    #endif
    ALL_UNUSED(peripheral_device_added);

    return AT_BLE_SUCCESS;
    
}

at_ble_status_t ble_mtu_changed_indication_handler(void *params)
{
    #if !defined(DEBUG_LOG_DISABLED)
        at_ble_mtu_changed_ind_t *mtu_changed_ind;
        mtu_changed_ind = (at_ble_mtu_changed_ind_t *)params;
        DBG_LOG_DEV("BLE-MTU Changed, Connection Handle: %d, New Value: %d", 
                                            mtu_changed_ind->conhdl, 
                                            mtu_changed_ind->mtu_value);
    #endif
    return AT_BLE_SUCCESS;
}

at_ble_status_t ble_mtu_changed_cmd_complete_handler(void *params)
{
    at_ble_cmd_complete_event_t *cmd_complete_event;
    cmd_complete_event = (at_ble_cmd_complete_event_t *)params;
    
    if (cmd_complete_event->status == AT_BLE_SUCCESS)
    {
        DBG_LOG_DEV("MTU Changed, Connection Handle:%d, Operation:%d", 
        cmd_complete_event->conn_handle, 
        cmd_complete_event->operation);
        return AT_BLE_SUCCESS;
    }
    else
    {
        DBG_LOG("MTU Change Failed, Connection Handle:%d, Operation:%d",
        cmd_complete_event->conn_handle,
        cmd_complete_event->operation);
    }
    return AT_BLE_FAILURE;
}

at_ble_status_t ble_characteristic_write_cmd_complete_handler(void *params)
{
    at_ble_cmd_complete_event_t *cmd_complete_event;
    cmd_complete_event = (at_ble_cmd_complete_event_t *)params;
    if (cmd_complete_event->status == AT_BLE_SUCCESS)
    {
        DBG_LOG_DEV("Char Write Cmd Complete, Connection Handle:%d, Operation:%d",
        cmd_complete_event->conn_handle,
        cmd_complete_event->operation);
        return AT_BLE_SUCCESS;
    }
    else
    {
        DBG_LOG_DEV("Char Write Cmd Failed, Connection Handle:%d, Operation:%d",
        cmd_complete_event->conn_handle,
        cmd_complete_event->operation);
    }
    return AT_BLE_FAILURE;
}

/** @brief function handles disconnection event received from stack */
at_ble_status_t ble_disconnected_state_handler(void *params)
{
    #if !defined(DEBUG_LOG_DISABLED)
     at_ble_disconnected_t* disconnect = (at_ble_disconnected_t *)params;
     DBG_LOG("Device disconnected Reason:0x%02x Handle=0x%x", disconnect->reason, disconnect->handle);
    #endif
    send_slave_security_flag = true;
    ble_clear_bond_info();
    return AT_BLE_SUCCESS;
}

/** @brief connection update parameter function */
at_ble_status_t ble_conn_param_update(void *params)
{
    at_ble_conn_param_update_done_t * conn_param_update;
    conn_param_update = (at_ble_conn_param_update_done_t *)params;
    DBG_LOG_DEV("AT_BLE_CONN_PARAM_UPDATE ");
    ALL_UNUSED(conn_param_update);  //To avoid compiler warning
    return AT_BLE_SUCCESS;
}

at_ble_status_t ble_conn_param_update_req(void *params)
{
    at_ble_conn_param_update_request_t * conn_param_req;
    conn_param_req = (at_ble_conn_param_update_request_t *)params;
    at_ble_conn_update_reply(conn_param_req->handle, true, 1, 120);
    return AT_BLE_SUCCESS;
}

at_ble_status_t ble_slave_security_request_handler(void* params)
{
    at_ble_pair_features_t features;
    at_ble_slave_sec_request_t* slave_sec_req;
    bool device_found = false;
    bool handle_found = false;
    
    slave_sec_req = (at_ble_slave_sec_request_t*)params; 
    DBG_LOG("ble_slave_security_request_handler"); 
    
    if (logic_bluetooth_get_open_to_pairing() == FALSE)
    {
        /* No intention to pair... disconnect */
        at_ble_disconnect(connected_state_info.handle, AT_BLE_AUTH_FAILURE);
        DBG_LOG("Unknown device and we don't allow pairing... bye!");
        logic_bluetooth_denied_connection_trigger();
        return AT_BLE_FAILURE;
    }  
    
    //if (slave_sec_req->status != AT_BLE_SUCCESS)
    //{
        //at_ble_disconnect(slave_sec_req->handle, AT_BLE_AUTH_FAILURE);        
        //return AT_BLE_FAILURE;
        //@Todo Status is not handled in the Library
    //}

    if((ble_device_info.conn_info.handle == slave_sec_req->handle) && (ble_device_info.conn_state == BLE_DEVICE_CONNECTED))
    {
        device_found = true;
        ble_device_info.conn_state = BLE_DEVICE_ENCRYPTION_STATE;
    }
    
    if (device_found)
    {
        DBG_LOG("Device found");
        
        if((ble_device_info.bond_info.auth & AT_BLE_AUTH_NO_MITM_BOND) && (slave_sec_req->bond == true))
        {
            if(at_ble_encryption_start(slave_sec_req->handle, &ble_device_info.bond_info.peer_ltk, ble_device_info.bond_info.auth) == AT_BLE_SUCCESS)
            {
                DBG_LOG("Encryption Started");
                return  AT_BLE_SUCCESS;
            }
            else
            {
                DBG_LOG("Encryption Not started");
                return AT_BLE_FAILURE;
            }           
        }
        else
        {
            DBG_LOG("No Encryption");
            ble_device_info.conn_state = BLE_DEVICE_CONNECTED;
        }
    }
    
    if((ble_device_info.conn_info.handle == slave_sec_req->handle) && (ble_device_info.conn_state == BLE_DEVICE_CONNECTED))
    {
        ble_device_info.conn_state = BLE_DEVICE_PAIRING;
        DBG_LOG("Pairing device");
        handle_found = true;
    }
    
    features.desired_auth =  BLE_AUTHENTICATION_LEVEL; 
    features.bond = slave_sec_req->bond;
    features.mitm_protection = true;
    features.io_cababilities = AT_BLE_IO_CAP_KB_DISPLAY;
    features.oob_avaiable = true;
            
    /* Distribution of LTK is required */
    features.initiator_keys =   AT_BLE_KEY_DIST_ENC; //Default
    features.responder_keys =   AT_BLE_KEY_DIST_ENC; //Default
    features.max_key_size = 16; //Default
    features.min_key_size = 16; //Default
    
    if(handle_found)
    {
        if (ble_device_info.conn_info.peer_addr.type == AT_BLE_ADDRESS_RANDOM_PRIVATE_RESOLVABLE)
        {
            features.initiator_keys =   (at_ble_key_dis_t)(AT_BLE_KEY_DIST_ENC | AT_BLE_KEY_DIST_ID);
            features.responder_keys =   (at_ble_key_dis_t)(AT_BLE_KEY_DIST_ENC | AT_BLE_KEY_DIST_ID);
        }
        
        /* Check if fresh pairing requested */
        if (ble_device_info.bond_info.status == AT_BLE_GAP_INVALID_PARAM)
        {
            /* Generate LTK */
            logic_rng_get_random_bytes(ble_device_info.host_ltk.key, sizeof(ble_device_info.host_ltk.key));
            logic_rng_get_random_bytes(ble_device_info.host_ltk.nb, sizeof(ble_device_info.host_ltk.nb));
            ble_device_info.host_ltk.ediv = logic_rng_get_uint16();
            ble_device_info.host_ltk.key_size = 16;
            /* Generate CSRK, not used though */
            logic_rng_get_random_bytes(ble_device_info.host_csrk.key, sizeof(ble_device_info.host_csrk.key));
            DBG_LOG("Keys generated");
        }
        else
        {
            /* Bonding information already exists */          
            DBG_LOG("Bonding info exists");
        }
        
        if(at_ble_authenticate(slave_sec_req->handle, &features, &ble_device_info.host_ltk, NULL) != AT_BLE_SUCCESS)
        {
            features.bond = false;
            features.mitm_protection = false;
            DBG_LOG("Slave Security Req - Authentication Failed");
            return AT_BLE_FAILURE;
        }
        else
        {
            DBG_LOG("Slave Security Req - Authenticate call success");
        }
    }   
    return AT_BLE_SUCCESS;
}

/** @brief function handles pair request */
at_ble_status_t ble_pair_request_handler(void *params)
{
    at_ble_pair_features_t features;
    at_ble_pair_request_t* pair_req;
    pair_req = (at_ble_pair_request_t*)params;
    bool device_found = false;
    
    DBG_LOG("ble_pair_request_handler");
    if (logic_bluetooth_get_open_to_pairing() == FALSE)
    {
        /* No intention to pair... disconnect */
        at_ble_disconnect(connected_state_info.handle, AT_BLE_AUTH_FAILURE);
        DBG_LOG("Unknown device and we don't allow pairing... bye!");
        logic_bluetooth_denied_connection_trigger();
        return AT_BLE_FAILURE;
    }
    
    if((ble_device_info.conn_info.handle == pair_req->handle) && (ble_device_info.conn_state == BLE_DEVICE_CONNECTED))
    {
        ble_device_info.conn_state = BLE_DEVICE_PAIRING;
        device_found = true;
    }
    
    features.desired_auth =  BLE_AUTHENTICATION_LEVEL; 
    features.bond = BLE_BOND_REQ;
    features.mitm_protection = BLE_MITM_REQ;
    features.io_cababilities = BLE_IO_CAPABALITIES;
    features.oob_avaiable = BLE_OOB_REQ;    
    /* Distribution of LTK is required */
    if (ble_device_info.conn_info.peer_addr.type == AT_BLE_ADDRESS_RANDOM_PRIVATE_RESOLVABLE)
    {
        /* Distribution of IRK is required */
        features.initiator_keys =   (at_ble_key_dis_t)(AT_BLE_KEY_DIST_ENC | AT_BLE_KEY_DIST_ID);
        features.responder_keys =   (at_ble_key_dis_t)(AT_BLE_KEY_DIST_ENC | AT_BLE_KEY_DIST_ID);
    }
    else
    {
        features.initiator_keys =   AT_BLE_KEY_DIST_ENC;
        features.responder_keys =   AT_BLE_KEY_DIST_ENC;
    }
    features.max_key_size = 16; //Default
    features.min_key_size = 16; //Default
    
    if (device_found)
    {        
        /* Check if fresh pairing requested */
        if (ble_device_info.bond_info.status == AT_BLE_GAP_INVALID_PARAM)
        {
            /* Generate LTK */
            logic_rng_get_random_bytes(ble_device_info.host_ltk.key, sizeof(ble_device_info.host_ltk.key));
            logic_rng_get_random_bytes(ble_device_info.host_ltk.nb, sizeof(ble_device_info.host_ltk.nb));
            ble_device_info.host_ltk.ediv = logic_rng_get_uint16();
            ble_device_info.host_ltk.key_size = 16;
            /* Generate CSRK, not used though... */
            logic_rng_get_random_bytes(ble_device_info.host_csrk.key, sizeof(ble_device_info.host_csrk.key));
            DBG_LOG("Keys generated");
        }
        else
        {
            /* Bonding information already exists */
            DBG_LOG("Bonding info exists");
        }

        /* Send pairing response */
        DBG_LOG_DEV("Sending pairing response");
        if(at_ble_authenticate(pair_req->handle, &features, &ble_device_info.host_ltk, NULL) != AT_BLE_SUCCESS)
        {
            features.bond = false;
            features.mitm_protection = false;
            DBG_LOG("Pair Request - Authentication Failed");
            return AT_BLE_FAILURE;
        }
        else
        {
            DBG_LOG("Call to ble authenticate succeeded");
        }
    }
    
    return AT_BLE_SUCCESS;
}

/** @brief function handles pair key request */
at_ble_status_t ble_pair_key_request_handler(void *params)
{
    at_ble_pair_key_request_t *pair_key;
    pair_key = (at_ble_pair_key_request_t *)params;
    /* Passkey has fixed value in this example MSB */
    RET_TYPE getting_digits_from_main_return = RETURN_OK;
    uint8_t passkey[6];
    uint8_t idx = 0;
        
    at_ble_pair_key_request_t pair_key_request;
    DBG_LOG("ble_pair_key_request_handler");
        
    memcpy((uint8_t *)&pair_key_request, pair_key, sizeof(at_ble_pair_key_request_t));
    
    if (pair_key_request.passkey_type == AT_BLE_PAIR_PASSKEY_ENTRY) 
    {
        /* Ask 6 digits pin from main MCU */
        getting_digits_from_main_return = comms_main_mcu_fetch_6_digits_pin(passkey);
    
    /*    
    #if defined DEBUG_LOG_DISABLED
        #warning "DEBUG LOG is disabled, Default PASSKEY used, Enable Debug Log to Enter PIN from console"
        pin = pin;
    #else
      DBG_LOG("Enter the Passkey(6-Digit) in Terminal:");
      for (idx = 0; idx < 6;) {          
        pin = getchar_timeout(PIN_TIMEOUT);
        if (pin == 0xFF) {
            DBG_LOG("Pin Timeout");
            DBG_LOG("Disconnecting ...");
            if (!(at_ble_disconnect(pair_key->handle,
                        AT_BLE_TERMINATED_BY_USER) == AT_BLE_SUCCESS)) {
                DBG_LOG("Disconnect Request Failed");
            }
            return AT_BLE_FAILURE;
        }
        if ((pin >= '0') && ( pin <= '9')) {
          passkey[idx++] = pin;
          DBG_LOG_CONT("%c", pin);
        } 
      }
    #endif*/
    }   
    
    /* Display passkey */
    if((((pair_key_request.passkey_type == AT_BLE_PAIR_PASSKEY_DISPLAY) &&
       (pair_key_request.type == AT_BLE_PAIR_PASSKEY)) || (pair_key_request.passkey_type == AT_BLE_PAIR_PASSKEY_ENTRY)) &&
       (getting_digits_from_main_return == RETURN_OK))
    {
          if(pair_key_request.passkey_type == AT_BLE_PAIR_PASSKEY_ENTRY)
          {
            DBG_LOG("Entered Pass-code:");
          }
          else
          {
            DBG_LOG("Please Enter the following Pass-code(on other Device):");
          }
          
          /* Convert passkey to ASCII format */
          for(idx=0; idx<AT_BLE_PASSKEY_LEN; idx++)
          {
                  passkey[idx] = passkey[idx] + '0';
                  DBG_LOG_CONT("%c",passkey[idx]);
          }     
          
          if(!((at_ble_pair_key_reply(pair_key->handle, pair_key_request.type, passkey)) == AT_BLE_SUCCESS))
          {
                  DBG_LOG("Pair-key reply failed");
                  return AT_BLE_FAILURE;
          }
          return AT_BLE_SUCCESS;  
    }
    else 
    {
        if(pair_key_request.type == AT_BLE_PAIR_OOB)
        {
            DBG_LOG("OOB Feature Not supported");
        }
        else if (getting_digits_from_main_return != RETURN_OK)
        {
            DBG_LOG("Request denied by main");
        }
        else
        {
            DBG_LOG("Unknown pair request type: %d", pair_key_request.type);
        }
        if (!(at_ble_disconnect(pair_key->handle, AT_BLE_TERMINATED_BY_USER) == AT_BLE_SUCCESS)) 
        {
            DBG_LOG("Disconnect Request Failed");
        }
        return AT_BLE_FAILURE;
    }
}

/** @brief function handles pair done event */
at_ble_status_t ble_pair_done_handler(void *params)
{
    at_ble_pair_done_t *pairing_params;
    bool device_found = false;
    pairing_params = (at_ble_pair_done_t *)params;
    
    if((ble_device_info.conn_info.handle == pairing_params->handle) && (ble_device_info.conn_state == BLE_DEVICE_PAIRING))
    {
        ble_device_info.conn_state = BLE_DEVICE_PAIRED;
        device_found = true;
    }
    
    if(pairing_params->status == AT_BLE_SUCCESS)
    {
        DBG_LOG("Pairing procedure completed successfully");
        if (device_found)
        {
            ble_device_info.bond_info.auth = pairing_params->auth;
            ble_device_info.bond_info.status = pairing_params->status;
            memcpy((uint8_t *)&ble_device_info.bond_info.peer_csrk, (uint8_t *)&pairing_params->peer_csrk, sizeof(at_ble_CSRK_t));
            memcpy((uint8_t *)&ble_device_info.bond_info.peer_irk, (uint8_t *)&pairing_params->peer_irk, sizeof(at_ble_IRK_t));
            memcpy((uint8_t *)&ble_device_info.bond_info.peer_ltk, (uint8_t *)&pairing_params->peer_ltk, sizeof(at_ble_LTK_t));
            ble_device_info.conn_state = BLE_DEVICE_PAIRED;
            
            /* Call logic bluetooth handler */
            logic_bluetooth_successfull_pairing_call(&ble_device_info, &connected_state_info);
        }
        else
        {
            DBG_LOG("BLE Device not found to store the pairing info");
            return AT_BLE_FAILURE;
        }       
    }
    else
    {
        ble_device_info.conn_state = BLE_DEVICE_PAIRING_FAILED;
        DBG_LOG("Pairing failed...status %d",pairing_params->status);
        if (ble_check_ispheripheral(pairing_params->handle)) {
            DBG_LOG("Disconnecting ....");
            if(!(at_ble_disconnect(pairing_params->handle, AT_BLE_TERMINATED_BY_USER) == AT_BLE_SUCCESS))
            {
            DBG_LOG("Disconnect Request Failed");
            logic_bluetooth_start_advertising();
            return AT_BLE_FAILURE;
            }   
            logic_bluetooth_start_advertising();    
        }
    }
    return AT_BLE_SUCCESS;
}

/** @brief function handles encryption status change */
at_ble_status_t ble_encryption_status_change_handler(void *params)
{
    at_ble_encryption_status_changed_t *enc_status;
    bool device_found = false;
    
    enc_status = (at_ble_encryption_status_changed_t *)params;
    
    if((ble_device_info.conn_info.handle == enc_status->handle) && (ble_device_info.conn_state == BLE_DEVICE_ENCRYPTION_STATE))
    {
        device_found = true;
    }
    
    if(enc_status->status == AT_BLE_SUCCESS)
    {
        DBG_LOG("Encryption completed successfully");
        if (device_found)
        {
            ble_device_info.conn_state = BLE_DEVICE_ENCRYPTION_COMPLETED;
            ble_device_info.bond_info.auth = enc_status->authen;
            ble_device_info.bond_info.status = enc_status->status;
            
            /* Call logic bluetooth function */
            logic_bluetooth_encryption_changed_success(ble_device_info.conn_info.peer_addr.addr);
        }
        else
        {
            DBG_LOG("BLE Device not found encryption info");
            return AT_BLE_FAILURE;
        }
    }
    else
    {
        if(device_found)
        {
            ble_device_info.bond_info.status = enc_status->status;
            ble_device_info.conn_state = BLE_DEVICE_ENCRYPTION_FAILED;
        }               
        DBG_LOG("Encryption failed");
        return AT_BLE_FAILURE;
    }
    send_slave_security_flag = true;
    return AT_BLE_SUCCESS;
}


/** @brief function handles encryption requests */
at_ble_status_t ble_encryption_request_handler(void *params)
{
    
    /*Encryption request comes before random resolve address request, 
      hence copy the buffer and recall this function after resolving the address*/
    if(resolve_addr_flag)
    {
        memcpy(temp_param,params,sizeof(at_ble_encryption_request_t));
        resolve_addr_flag = false;
        return AT_BLE_SUCCESS;      
    }
    at_ble_encryption_request_t *enc_req;
    bool key_found = false;
    bool device_found = false;
    enc_req = (at_ble_encryption_request_t *)params;

    if((ble_device_info.conn_info.handle == enc_req->handle) && (ble_device_info.conn_state != BLE_DEVICE_DISCONNECTED) && (ble_device_info.conn_state != BLE_DEVICE_DEFAULT_IDLE))
    {
        device_found = true;
        ble_device_info.conn_state = BLE_DEVICE_ENCRYPTION_STATE;
        DBG_LOG_DEV("ENC-Req: Conn Handle Device Found:hdl:%d", enc_req->handle);
    }
    
    if (device_found)
    {
        DBG_LOG_DEV("host device ediv %x",ble_device_info.host_ltk.ediv);
        DBG_LOG_DEV("peer device ediv %x",ble_device_info.bond_info.peer_ltk.ediv);
        DBG_LOG_DEV("enc_req ediv %x", enc_req->ediv);
        if((ble_device_info.host_ltk.ediv == enc_req->ediv) && !memcmp(&enc_req->nb[0],&ble_device_info.host_ltk.nb[0],8))
        {
            key_found = true;
            DBG_LOG_DEV("ENC-Req: Key Found");
        }
    }
    
    if(key_found == false)
    {
        logic_bluetooth_denied_connection_trigger();
        DBG_LOG("Pairing information of peer device is not available.");
        DBG_LOG("Please unpair the device from peer device(mobile) settings menu and start pairing again");
        if (at_ble_disconnect(ble_device_info.conn_info.handle, AT_BLE_AUTH_FAILURE) != AT_BLE_SUCCESS)
        {
            DBG_LOG("Couldn't call disconnect!");
        }
        return AT_BLE_FAILURE;
    }

    if(!(at_ble_encryption_request_reply(enc_req->handle, ble_device_info.bond_info.auth, key_found, &ble_device_info.host_ltk) == AT_BLE_SUCCESS))
    {
        DBG_LOG("Encryption Request Reply Failed");
    }
    else
    {      
        DBG_LOG_DEV("Encryption Request Reply");
    }
    return AT_BLE_SUCCESS;
}

 at_ble_status_t ble_device_dead_handler(void *params)
 {
     DBG_LOG("Device dead event received");
     ALL_UNUSED(params);
     return AT_BLE_SUCCESS;
 }
 
  at_ble_status_t ble_device_out_of_sync_handler(void *params)
  {
      DBG_LOG("Device out of sync event received");
      ALL_UNUSED(params);
      return AT_BLE_SUCCESS;
  } 

void ble_event_manager(at_ble_events_t events, void *event_params)
{
    DBG_LOG_DEV("BLE-Event:%d", events);
    switch(events)
    {       
     /* BLE GAP events */
    case AT_BLE_UNDEFINED_EVENT:
    case AT_BLE_SCAN_INFO:
    case AT_BLE_SCAN_REPORT:
    case AT_BLE_ADV_REPORT:
    case AT_BLE_RAND_ADDR_CHANGED:
    case AT_BLE_CONNECTED:
    case AT_BLE_DISCONNECTED:
    case AT_BLE_CONN_PARAM_UPDATE_DONE:
    case AT_BLE_CONN_PARAM_UPDATE_REQUEST:
    case AT_BLE_PAIR_DONE:
    case AT_BLE_PAIR_REQUEST:
    case AT_BLE_SLAVE_SEC_REQUEST:
    case AT_BLE_PAIR_KEY_REQUEST:
    case AT_BLE_ENCRYPTION_REQUEST:
    case AT_BLE_ENCRYPTION_STATUS_CHANGED:
    case AT_BLE_RESOLV_RAND_ADDR_STATUS:
    case AT_BLE_SIGN_COUNTERS_IND:
    case AT_BLE_PEER_ATT_INFO_IND:
    case AT_BLE_CON_CHANNEL_MAP_IND:
    {
        uint8_t idx;
        for (idx = 0; idx < MAX_GAP_EVENT_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_gap_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_gap_event_cb[idx];
                
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* BLE GATT Client Events */
    case AT_BLE_PRIMARY_SERVICE_FOUND:
    case AT_BLE_INCLUDED_SERVICE_FOUND:
    case AT_BLE_CHARACTERISTIC_FOUND:
    case AT_BLE_DESCRIPTOR_FOUND:
    case AT_BLE_DISCOVERY_COMPLETE:
    case AT_BLE_CHARACTERISTIC_READ_BY_UUID_RESPONSE:
    case AT_BLE_CHARACTERISTIC_READ_MULTIBLE_RESPONSE:
    case AT_BLE_CHARACTERISTIC_WRITE_RESPONSE:
    case AT_BLE_NOTIFICATION_RECIEVED:
    case AT_BLE_INDICATION_RECIEVED:
    {
        uint8_t idx;
        events -= (AT_BLE_CON_CHANNEL_MAP_IND + 1);
        for (idx = 0; idx < MAX_GATT_CLIENT_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_gatt_client_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_gatt_client_event_cb[idx];
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* BLE GATT Server events */
    case AT_BLE_NOTIFICATION_CONFIRMED:
    case AT_BLE_INDICATION_CONFIRMED:
    case AT_BLE_CHARACTERISTIC_CHANGED:
    case AT_BLE_CHARACTERISTIC_CONFIGURATION_CHANGED:
    case AT_BLE_SERVICE_CHANGED_INDICATION_SENT:
    case AT_BLE_WRITE_AUTHORIZE_REQUEST:
    case AT_BLE_MTU_CHANGED_INDICATION:
    case AT_BLE_MTU_CHANGED_CMD_COMPLETE:
    case AT_BLE_CHARACTERISTIC_WRITE_CMD_CMP:
    case AT_BLE_READ_AUTHORIZE_REQUEST:
    {
        uint8_t idx;
        events -= (AT_BLE_INDICATION_RECIEVED + 1);
        DBG_LOG_DEV("GATT_Server Event:%d", events);
        for (idx = 0; idx < MAX_GATT_SERVER_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_gatt_server_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_gatt_server_event_cb[idx];
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* BLE L2CAP Events */
    case AT_BLE_LECB_CONN_REQ:
    case AT_BLE_LECB_CONNECTED:
    case AT_BLE_LECB_DISCONNECTED:
    case AT_BLE_LECB_ADD_CREDIT_IND:
    case AT_BLE_LECB_SEND_RESP:
    case AT_BLE_LECB_DATA_RECIEVED:
    {
        uint8_t idx;
        events -= (AT_BLE_READ_AUTHORIZE_REQUEST + 1);
        for (idx = 0; idx < MAX_L2CAP_EVENT_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_l2cap_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_l2cap_event_cb[idx];
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* BLE HTPT Events */
    case AT_BLE_HTPT_CREATE_DB_CFM:
    case AT_BLE_HTPT_ERROR_IND:
    case AT_BLE_HTPT_DISABLE_IND:
    case AT_BLE_HTPT_TEMP_SEND_CFM:
    case AT_BLE_HTPT_MEAS_INTV_CHG_IND:
    case AT_BLE_HTPT_CFG_INDNTF_IND:
    case AT_BLE_HTPT_ENABLE_RSP:
    case AT_BLE_HTPT_MEAS_INTV_UPD_RSP:
    case AT_BLE_HTPT_MEAS_INTV_CHG_REQ:
    {
        uint8_t idx;
        events -= (AT_BLE_LECB_DATA_RECIEVED + 1);
        for (idx = 0; idx < MAX_HTPT_EVENT_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_htpt_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_htpt_event_cb[idx];
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* BLE DTM Events */
    case AT_BLE_LE_TEST_STATUS:
    case AT_BLE_LE_PACKET_REPORT:
    {
        uint8_t idx;
        events -= (AT_BLE_HTPT_MEAS_INTV_CHG_REQ + 1);
        for (idx = 0; idx < MAX_DTM_EVENT_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_dtm_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_dtm_event_cb[idx];
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* BLE custom events */
    case AT_BLE_CUSTOM_EVENT:
    case AT_BLE_DEVICE_READY:
    case AT_BLE_EVENT_MAX:
    {
        uint8_t idx;
        events -= (AT_BLE_LE_PACKET_REPORT + 1);
        for (idx = 0; idx < MAX_CUSTOM_EVENT_SUBSCRIBERS; idx++)
        {
            if (ble_mgr_custom_event_cb[idx] != NULL)
            {
                const ble_event_callback_t *event_cb_fn = (ble_event_callback_t *)ble_mgr_custom_event_cb[idx];
                if(event_cb_fn[events] != NULL)
                {
                    event_cb_fn[events](event_params);
                }
            }
        }
    }
    break;
    /* Unknown BLE event */
    default:
    {
        DBG_LOG("BLE-Manager:Unknown Event=0x%X", events);
        DBG_LOG("\r\n");
    }
    break;      
  }
}

/* Advertisement Data will be set based on the advertisement configuration */
at_ble_status_t ble_advertisement_data_set(dis_device_information_t* device_info)
{
    uint8_t adv_buf[AT_BLE_ADV_MAX_SIZE];
    uint8_t scn_resp[AT_BLE_ADV_MAX_SIZE];
    adv_element_container_t adv_data_element = {0, NULL};
    scan_resp_element_t scan_resp_data_element = {0, NULL};
    adv_data_element.adv_ptr = adv_buf;
    scan_resp_data_element.scn_ptr = scn_resp;
    at_ble_status_t status = AT_BLE_SUCCESS;
    
    #if BLE_GAP_ADV_SERVICE_16BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_16BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = COMPLETE_LIST_16BIT_SERV_UUIDS;
        MREPEAT(SERVICE_UUID16_MAX_NUM, _CONF_SERVICE_16BIT_UUID, &adv_data_element);
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len) 
        {           
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;     
        }
    }
    #else
    if(false){}
    #endif
    #if BLE_GAP_ADV_SERVICE_16BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = COMPLETE_LIST_16BIT_SERV_UUIDS;
        MREPEAT(SERVICE_UUID16_MAX_NUM, _CONF_SERVICE_16BIT_UUID_SCAN_RSP, &scan_resp_data_element);
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len) 
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        } 
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add 16-bit UUIDs");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_32BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_32BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = COMPLETE_LIST_32BIT_SERV_UUIDS;
        MREPEAT(SERVICE_UUID32_MAX_NUM, _CONF_SERVICE_32BIT_UUID, &adv_data_element)
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if BLE_GAP_ADV_SERVICE_32BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = COMPLETE_LIST_32BIT_SERV_UUIDS;
        MREPEAT(SERVICE_UUID32_MAX_NUM, _CONF_SERVICE_32BIT_UUID_SCAN_RSP, &adv_data_element)
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add List of 32-bit Service Class UUIDs");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_128BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_128BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = COMPLETE_LIST_128BIT_SERV_UUIDS;
        MREPEAT(SERVICE_UUID128_MAX_NUM, _CONF_SERVICE_128BIT_UUID, &adv_data_element)
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if BLE_GAP_ADV_SERVICE_128BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = COMPLETE_LIST_128BIT_SERV_UUIDS;
        MREPEAT(SERVICE_UUID128_MAX_NUM, _CONF_SERVICE_128BIT_UUID_SCAN_RSP, &adv_data_element)
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add List of 128-bit Service Class UUIDs");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SHORTENED_LOCAL_NAME_ENABLE
    #if (BLE_GAP_ADV_SHORTENED_LOCAL_NAME_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_SHORTENED_LOCAL_NAME_LEN))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_SHORTENED_LOCAL_NAME_LEN + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = SHORTENED_LOCAL_NAME;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_DATA_SHORTENED_LOCAL_NAME, BLE_GAP_ADV_DATA_SHORTENED_LOCAL_NAME_LEN);
        adv_data_element.len += BLE_GAP_ADV_DATA_SHORTENED_LOCAL_NAME_LEN;
    }
    #else
    if(false){}
    #endif
    else {
        DBG_LOG_ADV("Failed to add Shortened Local Name");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if (BLE_GAP_ADV_COMPLETE_LOCAL_NAME_ENABLE && !BLE_GAP_ADV_SHORTENED_LOCAL_NAME_ENABLE)
    #if (BLE_GAP_ADV_COMPLETE_LOCAL_NAME_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + strlen((char*)device_info->custom_device_name)))) {
        adv_buf[adv_data_element.len++] = strlen((char*)device_info->custom_device_name) + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = COMPLETE_LOCAL_NAME;
        memcpy(&adv_buf[adv_data_element.len], device_info->custom_device_name, strlen((char*)device_info->custom_device_name));
        adv_data_element.len += strlen((char*)device_info->custom_device_name);
    }
    #else
    if(false){}
    #endif
    else {
        DBG_LOG_ADV("Failed to add Complete local name");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if ( BLE_GAP_ADV_SHORTENED_LOCAL_NAME_ENABLE && BLE_GAP_ADV_COMPLETE_LOCAL_NAME_ENABLE )
    #if ((BLE_GAP_ADV_SHORTENED_LOCAL_NAME_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SHORTENED_LOCAL_NAME_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_COMPLETE_LOCAL_NAME_LENGTH))){
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_COMPLETE_LOCAL_NAME_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = COMPLETE_LOCAL_NAME;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_DATA_COMPLETE_LOCAL_NAME, BLE_GAP_ADV_DATA_COMPLETE_LOCAL_NAME_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_DATA_COMPLETE_LOCAL_NAME_LENGTH;
    }
    #else
    if(false){}
    #endif
    else {
        DBG_LOG_ADV("Failed to add Complete local name");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_TX_POWER_ENABLE
    #if (BLE_GAP_ADV_TX_POWER_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if(adv_data_element.len <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_TX_POWER_SIZE))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_TX_POWER_SIZE + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = TX_POWER_LEVEL;
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_TX_POWER;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_TX_POWER_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_TX_POWER_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if(scan_resp_data_element.len <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_TX_POWER_SIZE))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_TX_POWER_SIZE + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = TX_POWER_LEVEL;
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_TX_POWER;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Tx Power");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SLAVE_CONN_INTERVAL_RANGE_ENABLE
    #if (BLE_GAP_ADV_SLAVE_CONN_INTERVAL_RANGE_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH + BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH + BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = SLAVE_CONNECTION_INTERVAL_RANGE;
        adv_buf[adv_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MIN;
        adv_buf[adv_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MIN >> 8);
        adv_buf[adv_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MAX;
        adv_buf[adv_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MAX >> 8);
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SLAVE_CONN_INTERVAL_RANGE_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SLAVE_CONN_INTERVAL_RANGE_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH + BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH + BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = SLAVE_CONNECTION_INTERVAL_RANGE;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MIN;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MIN >> 8);
        scn_resp[scan_resp_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MAX;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_SLAVE_CONN_INTERVAL_RANGE_MAX >> 8);
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to Slave connection interval range");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_SOLTN_16BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_SOLTN_16BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = LIST_16BIT_SERV_SOLICITATION_UUIDS;
        MREPEAT(SERVICE_UUID16_MAX_NUM, _CONF_SERVICE_SOLTN_16BIT_UUID, &adv_data_element);
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SERVICE_SOLTN_16BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SERVICE_SOLTN_16BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = LIST_16BIT_SERV_SOLICITATION_UUIDS;
        MREPEAT(SERVICE_UUID16_MAX_NUM, _CONF_SERVICE_SOLTN_16BIT_UUID_SCAN_RSP, &scan_resp_data_element);
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add List of 16-bit Service Solicitation UUIDs");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_SOLTN_32BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_SOLTN_32BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = LIST_32BIT_SERV_SOLICITATION_UUIDS;
        MREPEAT(SERVICE_UUID32_MAX_NUM, _CONF_SERVICE_SOLTN_32BIT_UUID, &adv_data_element)
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SERVICE_SOLTN_32BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SERVICE_SOLTN_32BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = LIST_32BIT_SERV_SOLICITATION_UUIDS;
        MREPEAT(SERVICE_UUID32_MAX_NUM, _CONF_SERVICE_SOLTN_32BIT_UUID_SCAN_RSP, &scan_resp_data_element)
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add List of 32-bit Service Solicitation UUIDs");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_SOLTN_128BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_SOLTN_128BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = LIST_128BIT_SERV_SOLICITATION_UUIDS;
        MREPEAT(SERVICE_UUID128_MAX_NUM, _CONF_SERVICE_SOLTN_128BIT_UUID, &adv_data_element)
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SERVICE_SOLTN_128BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SERVICE_SOLTN_128BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = LIST_128BIT_SERV_SOLICITATION_UUIDS;
        MREPEAT(SERVICE_UUID128_MAX_NUM, _CONF_SERVICE_SOLTN_128BIT_UUID_SCAN_RSP, &scan_resp_data_element)
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add List of 128-bit Service Solicitation UUIDs");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_DATA_16BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_DATA_16BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = SERVICE_DATA;
        adv_buf[adv_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_SERVICE_DATA_16BIT_UUID;
        adv_buf[adv_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_SERVICE_DATA_16BIT_UUID >> 8);
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID, BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH);
        adv_data_element.len += BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SERVICE_DATA_16BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SERVICE_DATA_16BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_16BIT_UUID_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = SERVICE_DATA;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_SERVICE_DATA_16BIT_UUID;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_SERVICE_DATA_16BIT_UUID >> 8);
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID, BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_16BIT_UUID_LENGTH;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add service data of 16bits");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_DATA_32BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_DATA_32BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = SERVICE_DATA_32BIT;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_DATA_SERVICE_DATA_32BIT_UUID, BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH);
        adv_data_element.len += BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID, BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH);
        adv_data_element.len += BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SERVICE_DATA_32BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SERVICE_DATA_32BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = SERVICE_DATA_32BIT;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_DATA_SERVICE_DATA_32BIT_UUID, BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_SERVICE_32BIT_UUID_LENGTH;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID, BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_32BIT_UUID_LENGTH;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add service data of 32bits");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_SERVICE_DATA_128BIT_UUID_ENABLE
    #if (BLE_GAP_ADV_SERVICE_DATA_128BIT_UUID_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = SERVICE_DATA_128BIT;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_DATA_SERVICE_DATA_128BIT_UUID, BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH);
        adv_data_element.len += BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID, BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH);
        adv_data_element.len += BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_SERVICE_DATA_128BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_SERVICE_DATA_128BIT_UUID_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH + BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = SERVICE_DATA_128BIT;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_DATA_SERVICE_DATA_128BIT_UUID, BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_SERVICE_128BIT_UUID_LENGTH;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID, BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_ADDITIONAL_DATA_SERVICE_DATA_128BIT_UUID_LENGTH;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add service data of 128bits");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_PUBLIC_TARGET_ADDR_ENABLE
    #if (BLE_GAP_ADV_PUBLIC_TARGET_ADDR_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_PUBLIC_TARGET_ADDR_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = PUBLIC_TARGET_ADDRESS;
        MREPEAT(PUBLIC_TARGET_ADDR_MAX_NUM, _CONF_PUBLIC_TARGET_ADDR, &adv_data_element);
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_PUBLIC_TARGET_ADDR_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_PUBLIC_TARGET_ADDR_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_PUBLIC_TARGET_ADDR_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = PUBLIC_TARGET_ADDRESS;
        MREPEAT(PUBLIC_TARGET_ADDR_MAX_NUM, _CONF_PUBLIC_TARGET_ADDR_SCAN_RSP, &scan_resp_data_element);
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Public target addresses");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_RANDOM_TARGET_ADDR_ENABLE
    #if (BLE_GAP_ADV_RANDOM_TARGET_ADDR_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_RANDOM_TARGET_ADDR_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t adv_element_len;
        adv_buf[adv_data_element.len] = adv_data_element.len;
        length_field_ind = adv_data_element.len;
        adv_data_element.len++;
        adv_buf[adv_data_element.len++] = RANDOM_TARGET_ADDRESS;
        MREPEAT(PUBLIC_RANDOM_ADDR_MAX_NUM, _CONF_RANDOM_TARGET_ADDR, &adv_data_element);
        adv_element_len = adv_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(adv_element_len)
        {
            adv_buf[length_field_ind] = adv_element_len+ADV_TYPE_SIZE;
        }
        else
        {
            adv_data_element.len = adv_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_RANDOM_TARGET_ADDR_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_RANDOM_TARGET_ADDR_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_RANDOM_TARGET_ADDR_LENGTH))) {
        uint8_t length_field_ind;
        uint8_t scan_resp_element_len;
        scn_resp[scan_resp_data_element.len] = scan_resp_data_element.len;
        length_field_ind = scan_resp_data_element.len;
        scan_resp_data_element.len++;
        scn_resp[scan_resp_data_element.len++] = RANDOM_TARGET_ADDRESS;
        MREPEAT(PUBLIC_RANDOM_ADDR_MAX_NUM, _CONF_RANDOM_TARGET_ADDR_SCAN_RSP, &scan_resp_data_element);
        scan_resp_element_len = scan_resp_data_element.len - (length_field_ind + ADV_ELEMENT_SIZE);
        if(scan_resp_element_len)
        {
            scn_resp[length_field_ind] = scan_resp_element_len + ADV_TYPE_SIZE;
        }
        else
        {
            scan_resp_data_element.len = scan_resp_data_element.len - ADV_ELEMENT_SIZE;
        }
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Random target addresses");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_APPEARANCE_ENABLE
    #if (BLE_GAP_ADV_APPEARANCE_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_APPEARANCE_SIZE))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_APPEARANCE_SIZE + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = APPEARANCE;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_DATA_APPEARANCE, BLE_GAP_ADV_DATA_APPEARANCE_SIZE);
        adv_data_element.len += BLE_GAP_ADV_DATA_APPEARANCE_SIZE;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_APPEARANCE_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_APPEARANCE_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_APPEARANCE_SIZE))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_APPEARANCE_SIZE + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = APPEARANCE;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_DATA_APPEARANCE, BLE_GAP_ADV_DATA_APPEARANCE_SIZE);
        scan_resp_data_element.len += BLE_GAP_ADV_DATA_APPEARANCE_SIZE;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Appearance");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_ADVERTISING_INTERVAL_ENABLE
    #if (BLE_GAP_ADV_ADVERTISING_INTERVAL_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADVERTISING_INTERVAL_LENGTH))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_ADVERTISING_INTERVAL_LENGTH + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = ADVERTISING_INTERVAL;
        adv_buf[adv_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_ADVERTISING_INTERVAL;
        adv_buf[adv_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_ADVERTISING_INTERVAL >> 8);
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_ADVERTISING_INTERVAL_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_ADVERTISING_INTERVAL_SCN_RSP_ENABLE) == SCAN_RESPONSE_ONLY_ENABLE)
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_ADVERTISING_INTERVAL_LENGTH))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_ADVERTISING_INTERVAL_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = ADVERTISING_INTERVAL;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)BLE_GAP_ADV_DATA_ADVERTISING_INTERVAL;
        scn_resp[scan_resp_data_element.len++] = (uint8_t)(BLE_GAP_ADV_DATA_ADVERTISING_INTERVAL >> 8);
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Advertisement interval");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_LE_BT_DEVICE_ADDR_ENABLE
    #if (BLE_GAP_ADV_LE_BT_DEVICE_ADDR_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = LE_BLUETOOTH_DEVICE_ADDRESS;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR, BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH);
        adv_data_element.len += BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_LE_BT_DEVICE_ADDR_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_LE_BT_DEVICE_ADDR_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = LE_BLUETOOTH_DEVICE_ADDRESS;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR, BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH);
        scan_resp_data_element.len += BLE_GAP_ADV_DATA_LE_BT_DEVICE_ADDR_LENGTH;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Bluetooth device addresses");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_LE_ROLE_ENABLE
    #if (BLE_GAP_ADV_LE_ROLE_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_LE_ROLE_SIZE))) {
        adv_buf[adv_data_element.len++] = ADV_TYPE_SIZE + BLE_GAP_ADV_DATA_LE_ROLE_SIZE;
        adv_buf[adv_data_element.len++] = LE_ROLE;
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_LE_ROLE;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_LE_ROLE_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_LE_ROLE_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_LE_ROLE_SIZE))) {
        scn_resp[scan_resp_data_element.len++] = ADV_TYPE_SIZE + BLE_GAP_ADV_DATA_LE_ROLE_SIZE;
        scn_resp[scan_resp_data_element.len++] = LE_ROLE;
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_LE_ROLE;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add LE role");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    #if BLE_GAP_ADV_MANUFACTURER_SPECIFIC_DATA_ENABLE
    #if (BLE_GAP_ADV_MANUFACTURER_SPECIFIC_DATA_SCN_RSP_ENABLE != SCAN_RESPONSE_ONLY_ENABLE)
    if((adv_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_TYPE_FLAG_SIZE + ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE))) {
        adv_buf[adv_data_element.len++] = BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE + ADV_TYPE_SIZE;
        adv_buf[adv_data_element.len++] = MANUFACTURER_SPECIFIC_DATA;
        memcpy(&adv_buf[adv_data_element.len], BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA, BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE);
        adv_data_element.len += BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE;
    }
    #else
    if(false){}
    #endif
    #if ((BLE_GAP_ADV_MANUFACTURER_SPECIFIC_DATA_SCN_RSP_ENABLE == SCAN_RESPONSE_ENABLE) || (BLE_GAP_ADV_MANUFACTURER_SPECIFIC_DATA_SCN_RSP_ENABLE == SCAN_RESPONSE_ONLY_ENABLE))
    else if((scan_resp_data_element.len) <= (AT_BLE_ADV_MAX_SIZE - (ADV_ELEMENT_SIZE + BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE))) {
        scn_resp[scan_resp_data_element.len++] = BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE + ADV_TYPE_SIZE;
        scn_resp[scan_resp_data_element.len++] = MANUFACTURER_SPECIFIC_DATA;
        memcpy(&scn_resp[scan_resp_data_element.len], BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA, BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE);
        scan_resp_data_element.len += BLE_GAP_ADV_DATA_MANUFACTURER_SPECIFIC_DATA_SIZE;
    }
    #endif
    else {
        DBG_LOG_ADV("Failed to add Manufacturer specific data");
        return AT_BLE_GAP_INVALID_PARAM;
    }
    #endif
    
    if (at_ble_adv_data_set(adv_buf, adv_data_element.len, scn_resp,
    scan_resp_data_element.len) != AT_BLE_SUCCESS) {
        DBG_LOG("BLE Advertisement data set failed");
        return status;
        } else {
        DBG_LOG_DEV("BLE Advertisement data set success");
        return AT_BLE_SUCCESS;
    }
}



