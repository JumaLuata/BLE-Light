/*! *********************************************************************************
* \addtogroup Proximity Reporter
* @{
********************************************************************************** */
/*!
* Copyright (c) 2014, Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
* \file
*
* This file is the source file for the Proximity Reporter application
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:
*
* o Redistributions of source code must retain the above copyright notice, this list
*   of conditions and the following disclaimer.
*
* o Redistributions in binary form must reproduce the above copyright notice, this
*   list of conditions and the following disclaimer in the documentation and/or
*   other materials provided with the distribution.
*
* o Neither the name of Freescale Semiconductor, Inc. nor the names of its
*   contributors may be used to endorse or promote products derived from this
*   software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/************************************************************************************
*************************************************************************************
* Include
*************************************************************************************
************************************************************************************/
/* Framework / Drivers */
#include "RNG_Interface.h"
#include "Keyboard.h"
#include "LED.h"
#include "TimersManager.h"
#include "FunctionLib.h"
#include "Panic.h"

/* BLE Host Stack */
#include "gatt_server_interface.h"
#include "gatt_client_interface.h"
#include "gap_interface.h"
#include "gatt_db_handles.h"

/* Profile / Services */
#include "battery_interface.h"
#include "device_info_interface.h"
#include "link_loss_interface.h"
#include "tx_power_interface.h"
#include "immediate_alert_interface.h"
#include "hid_interface.h"

/* Connection Manager */
#include "ble_conn_manager.h"

#include "board.h"
#include "ApplMain.h"
#include "proximity_reporter.h"

/************************************************************************************
*************************************************************************************
* Private constants & macros
*************************************************************************************
************************************************************************************/

#define mBatteryLevelReportInterval_c   (5)        /* battery level report interval in seconds  */
#define mHidReportInterval_c            (1)        /* hid report interval in seconds  */
/************************************************************************************
*************************************************************************************
* Private type definitions
*************************************************************************************
************************************************************************************/
typedef enum
{
#if gAppUseBonding_d
    fastWhiteListAdvState_c,
#endif
    fastAdvState_c,
    slowAdvState_c
}advType_t;

typedef struct advState_tag{
    bool_t      advOn;
    advType_t   advType;
}advState_t;

/************************************************************************************
*************************************************************************************
* Private memory declarations
*************************************************************************************
************************************************************************************/

/* Adv State */
static advState_t  mAdvState;
static deviceId_t  mPeerDeviceId = gInvalidDeviceId_c;

/* Service Data*/
static basConfig_t basServiceConfig = {service_battery, 0};
static disConfig_t disServiceConfig = {service_device_info};
//static iasConfig_t serviceIasConfig = {service_LED_Control, gLls_NoAlert_c};
static hidConfig_t hidServiceConfig = {service_LED_Control, gHid_ReportProtocolMode_c, gHid_LED_OFF_c};
static uint16_t cpHandles[] = { value_led_status_ctrl_point };

/* Application specific data*/
static tmrTimerID_t mAdvTimerId;
static tmrTimerID_t mHidDemoTimerId;
static tmrTimerID_t mBatteryMeasurementTimerId;

/************************************************************************************
*************************************************************************************
* Private functions prototypes
*************************************************************************************
************************************************************************************/

/* Gatt and Att callbacks */
static void BleApp_AdvertisingCallback (gapAdvertisingEvent_t* pAdvertisingEvent);
static void BleApp_ConnectionCallback (deviceId_t peerDeviceId, gapConnectionEvent_t* pConnectionEvent);
static void BleApp_GattServerCallback (deviceId_t deviceId, gattServerEvent_t* pServerEvent);
static void BleApp_Config();

/* Timer Callbacks */
static void AdvertisingTimerCallback (void *);
static void BatteryMeasurementTimerCallback (void *);
static void HIDTimerCallback(void * pParam);

static void BleApp_Advertise(void);

/* Application specific */
void AlertUI(uint8_t alertLevel);
void LightUI(uint8_t led_status);
/************************************************************************************
*************************************************************************************
* Public functions
*************************************************************************************
************************************************************************************/

/*! *********************************************************************************
* \brief    Initializes application specific functionality before the BLE stack init.
*
********************************************************************************** */
void BleApp_Init(void)
{
    /* Initialize application support for drivers */
    BOARD_InitAdc();
}

/*! *********************************************************************************
* \brief    Starts the BLE application.
*
********************************************************************************** */
void BleApp_Start(void)
{
#if gAppUseBonding_d
    if (gcBondedDevices > 0)
    {
        mAdvState.advType = fastWhiteListAdvState_c;
    }
    else
    {
#endif
        mAdvState.advType = fastAdvState_c;
#if gAppUseBonding_d
    }
#endif

    BleApp_Advertise();
}

/*! *********************************************************************************
* \brief        Handles keyboard events.
*
* \param[in]    events    Key event structure.
********************************************************************************** */
void BleApp_HandleKeys(key_event_t events)
{
    switch (events)
    {
        case gKBD_EventPressPB1_c:
        {
            BleApp_Start();
            break;
        }
        break;
        case gKBD_EventLongPB1_c:
        {
            if (mPeerDeviceId != gInvalidDeviceId_c)
                Gap_Disconnect(mPeerDeviceId);
            break;
        }
        case gKBD_EventPressPB2_c:
        case gKBD_EventLongPB2_c:
        default:
            break;
    }
}

/*! *********************************************************************************
* \brief        Handles BLE generic callback.
*
* \param[in]    pGenericEvent    Pointer to gapGenericEvent_t.
********************************************************************************** */
void BleApp_GenericCallback (gapGenericEvent_t* pGenericEvent)
{
    /* Call BLE Conn Manager */
    BleConnManager_GenericEvent(pGenericEvent);
    
    switch (pGenericEvent->eventType)
    {
        case gInitializationComplete_c:    
        {
            BleApp_Config();
        }
        break;    
        
        case gAdvertisingParametersSetupComplete_c:
        {
            App_StartAdvertising(BleApp_AdvertisingCallback, BleApp_ConnectionCallback);
        }
        break;         

        default: 
            break;
    }
}

/************************************************************************************
*************************************************************************************
* Private functions
*************************************************************************************
************************************************************************************/

/*! *********************************************************************************
* \brief        Configures BLE Stack after initialization. Usually used for
*               configuring advertising, scanning, white list, services, et al.
*
********************************************************************************** */
static void BleApp_Config()
{
    /* Configure as GAP peripheral */
    BleConnManager_GapPeripheralConfig();

    /* Register for callbacks*/
    GattServer_RegisterHandlesForWriteNotifications(NumberOfElements(cpHandles), cpHandles);
    App_RegisterGattServerCallback(BleApp_GattServerCallback);
    GattServer_RegisterHandlesForWriteNotifications(NumberOfElements(cpHandles), cpHandles);    
       
    /* Setup Advertising and scanning data */
    Gap_SetAdvertisingData(&gAppAdvertisingData, &gAppScanRspData);

    mAdvState.advOn = FALSE;

    /* Start services */
    
    basServiceConfig.batteryLevel = BOARD_GetBatteryLevel();
    Bas_Start(&basServiceConfig);
    Dis_Start(&disServiceConfig);
    Hid_Start(&hidServiceConfig);
    
    /* Allocate application timers */
    mAdvTimerId = TMR_AllocateTimer();
    mHidDemoTimerId = TMR_AllocateTimer();
    mBatteryMeasurementTimerId = TMR_AllocateTimer();
}

/*! *********************************************************************************
* \brief        Configures GAP Advertise parameters. Advertise will satrt after
*               the parameters are set.
*
********************************************************************************** */
static void BleApp_Advertise(void)
{
    uint32_t timeout = 0;

    switch (mAdvState.advType)
    {
#if gAppUseBonding_d
        case fastWhiteListAdvState_c:
        {
            gAdvParams.minInterval = gFastConnMinAdvInterval_c;
            gAdvParams.maxInterval = gFastConnMaxAdvInterval_c;
            gAdvParams.filterPolicy = gProcessWhiteListOnly_c;
            timeout = gFastConnWhiteListAdvTime_c;
        }
        break;
#endif
        case fastAdvState_c:
        {
            gAdvParams.minInterval = gFastConnMinAdvInterval_c;
            gAdvParams.maxInterval = gFastConnMaxAdvInterval_c;
            gAdvParams.filterPolicy = gProcessAll_c;
            timeout = gFastConnAdvTime_c - gFastConnWhiteListAdvTime_c;
        }
        break;

        case slowAdvState_c:
        {
            gAdvParams.minInterval = gReducedPowerMinAdvInterval_c;
            gAdvParams.maxInterval = gReducedPowerMinAdvInterval_c;
            gAdvParams.filterPolicy = gProcessAll_c;
            timeout = gReducedPowerAdvTime_c;
        }
        break;
    }

    /* Set advertising parameters*/
    Gap_SetAdvertisingParameters(&gAdvParams);

    /* Start advertising timer */
    TMR_StartLowPowerTimer(mAdvTimerId,gTmrLowPowerSecondTimer_c,
               TmrSeconds(timeout), AdvertisingTimerCallback, NULL);
}

/*! *********************************************************************************
* \brief        Handles BLE Advertising callback from host stack.
*
* \param[in]    pAdvertisingEvent    Pointer to gapAdvertisingEvent_t.
********************************************************************************** */
static void BleApp_AdvertisingCallback (gapAdvertisingEvent_t* pAdvertisingEvent)
{
    switch (pAdvertisingEvent->eventType)
    {
        case gAdvertisingStateChanged_c:
        {
            mAdvState.advOn = !mAdvState.advOn;
            LED_StopFlashingAllLeds();
            Led1Flashing();

            if(!mAdvState.advOn)
            {
                Led2Flashing();
                Led3Flashing();
                Led4Flashing();
            }
        }
        break;

        case gAdvertisingCommandFailed_c:
        {
            panic(0,0,0,0);
        }
        break;

        default:
            break;
    }
}

/*! *********************************************************************************
* \brief        Handles BLE Connection callback from host stack.
*
* \param[in]    peerDeviceId        Peer device ID.
* \param[in]    pConnectionEvent    Pointer to gapConnectionEvent_t.
********************************************************************************** */
static void BleApp_ConnectionCallback (deviceId_t peerDeviceId, gapConnectionEvent_t* pConnectionEvent)
{
	/* Connection Manager to handle Host Stack interactions */
	BleConnManager_GapPeripheralEvent(peerDeviceId, pConnectionEvent);

    switch (pConnectionEvent->eventType)
    {
        case gConnEvtConnected_c:
        {
            mPeerDeviceId = peerDeviceId;

            /* Advertising stops when connected */
            mAdvState.advOn = FALSE;            
        
            /* Subscribe client*/
            Bas_Subscribe(peerDeviceId);        
            Hid_Subscribe(peerDeviceId);
            /* UI */
            LED_StopFlashingAllLeds();
            Led1On();
            LightUI(gHid_LED_OFF_c);
          
                        
            /* Stop Advertising Timer*/
            TMR_StopTimer(mAdvTimerId);

            /* Start HID Timer  */
            TMR_StartLowPowerTimer(mHidDemoTimerId, gTmrLowPowerIntervalMillisTimer_c,
                       TmrSeconds(mHidReportInterval_c), HIDTimerCallback, NULL);  
            
            /* Start battery measurements */
            TMR_StartLowPowerTimer(mBatteryMeasurementTimerId, gTmrLowPowerIntervalMillisTimer_c,
                       TmrSeconds(mBatteryLevelReportInterval_c), BatteryMeasurementTimerCallback, NULL);            
        }
        break;
        
        case gConnEvtDisconnected_c:
        {
            /* Unsubscribe client */
            Bas_Unsubscribe();
            //Ias_Unsubscribe();
            Hid_Unsubscribe();
            mPeerDeviceId = gInvalidDeviceId_c;

            if (pConnectionEvent->eventData.disconnectedEvent.reason == gHciConnectionTimeout_c)
            {
                /* Link loss detected*/
                BleApp_Start();
            }
            else
            {
              /* Connection was terminated by peer or application */
                BleApp_Start();
            }
        }
        break;
    default:
        break;
    }
}

/*! *********************************************************************************
* \brief        Handles GATT server callback from host stack.
*
* \param[in]    deviceId        Peer device ID.
* \param[in]    pServerEvent    Pointer to gattServerEvent_t.
********************************************************************************** */
static void BleApp_GattServerCallback (deviceId_t deviceId, gattServerEvent_t* pServerEvent)
{    
    switch (pServerEvent->eventType)
    {
        case gEvtAttributeWrittenWithoutResponse_c:
        {
            if (pServerEvent->eventData.attributeWrittenEvent.handle == value_led_status_ctrl_point)
            {
               hidServiceConfig.ledInfo = pServerEvent->eventData.attributeWrittenEvent.aValue[0];
               LightUI(hidServiceConfig.ledInfo);        
            }
            if (pServerEvent->eventData.attributeWrittenEvent.handle == value_led_status_str)
            {
               hidServiceConfig.ledInfo = pServerEvent->eventData.attributeWrittenEvent.aValue[0];
               LightUI(hidServiceConfig.ledInfo);        
            }
        }
        break;
            
        default:
            break;
    }
}


/*! *********************************************************************************
* \brief        Handles advertising timer callback.
*
* \param[in]    pParam        Calback parameters.
********************************************************************************** */
static void AdvertisingTimerCallback(void * pParam)
{
    /* Stop and restart advertising with new parameters */
    Gap_StopAdvertising();

    switch (mAdvState.advType)
    {
#if gAppUseBonding_d
        case fastWhiteListAdvState_c:
        {
            mAdvState.advType = fastAdvState_c;
        }
        break;
#endif
        case fastAdvState_c:
        {
            mAdvState.advType = slowAdvState_c;
        }
        break;

        default:
        break;
    }
    BleApp_Advertise();
}

/*! *********************************************************************************
* \brief        Updates UI according to the alert level.
*
* \param[in]    alertLevel        Alert Level.
********************************************************************************** */
void AlertUI(uint8_t alertLevel)
{
    StopLed3Flashing();
    StopLed4Flashing();
    
    switch (alertLevel)
    {
        case gLls_NoAlert_c:
            break;
        case gLls_MildAlert_c:
            Led4Flashing();
            break;
        case gLls_HighAlert_c:
            Led3Flashing();
            Led4Flashing();            
            break;
        default:
            break;
    }
}

void LightUI(uint8_t led_status)
{
    Led2Off();
    StopLed2Flashing();
    switch (led_status)
    {
        case 0x00:
            break;
        case 0x01:
            Led2On();
            break;
        case 0x02:
            Led2Flashing();  
            break;
        default:
            break;
    } 
}
/*! *********************************************************************************
* \brief        Handles battery measurement timer callback.
*
* \param[in]    pParam        Calback parameters.
********************************************************************************** */
static void BatteryMeasurementTimerCallback(void * pParam)
{
    basServiceConfig.batteryLevel = BOARD_GetBatteryLevel();
    Bas_RecordBatteryMeasurement(basServiceConfig.serviceHandle, basServiceConfig.batteryLevel);
}

/*! *********************************************************************************
* @}
********************************************************************************** */
static void HIDTimerCallback(void * pParam)
{
  switch (hidServiceConfig.ledInfo)
  {
      case gHid_LED_OFF_c:
          Hid_SendBleStatusReport(hidServiceConfig.serviceHandle, "LED_OFF");
          break;
      case gHid_LED_ON_c:
          Hid_SendBleStatusReport(hidServiceConfig.serviceHandle, "LED_ON");
          break;
      case gHid_LED_FLASH_c:
          Hid_SendBleStatusReport(hidServiceConfig.serviceHandle, "Flashing");
          break;
      default:
          Hid_SendBleStatusReport(hidServiceConfig.serviceHandle, "Error");
            break;  
  }
    
}