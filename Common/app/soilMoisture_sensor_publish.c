/*
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 * Derived from simple_sub_pub_demo.c
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

// MICHAEL - clock library
#include <time.h>
#include <unistd.h>
#include <math.h>

#include "logging_levels.h"
/* define LOG_LEVEL here if you want to modify the logging level from the default */

#define LOG_LEVEL    LOG_DEBUG

#include "logging.h"

/* Standard includes. */
#include <string.h>
#include <stdio.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "kvstore.h"

/* MQTT library includes. */
#include "core_mqtt.h"
#include "core_mqtt_agent.h"
#include "sys_evt.h"

/* Subscription manager header include. */
#include "subscription_manager.h"

#define moistureReport_JSON \
    "{"                     \
    "\"SoilMoisture\":%.2f," \
	"\"ADC_Reading\":%1u"   \
    "}"

/* Sensor includes */

/*
 */
#define MQTT_PUBLISH_MAX_LEN                 ( 512 )
#define MQTT_PUBLISH_TIME_BETWEEN_MS         ( 3000 )
#define MQTT_PUBLISH_TOPIC                   "SoilMoisture_sensor_data"
#define MQTT_PUBLICH_TOPIC_STR_LEN           ( 256 )
#define MQTT_PUBLISH_BLOCK_TIME_MS           ( 1000 )
#define MQTT_PUBLISH_NOTIFICATION_WAIT_MS    ( 1000 )

#define MQTT_NOTIFY_IDX                      ( 1 )
#define MQTT_PUBLISH_QOS                     ( MQTTQoS0 )

ADC_HandleTypeDef hadc4;

#define adc_handler hadc4

const float MOIST_SESNOR_HIGH_MOISTURE = 1000.0;
const float MOIST_SESNOR_LOW_MOISTURE  = 1750.0;
/*-----------------------------------------------------------*/

/**
 * @brief Defines the structure to use as the command callback context in this
 * demo.
 */
struct MQTTAgentCommandContext
{
    MQTTStatus_t xReturnStatus;
    TaskHandle_t xTaskToNotify;
};

typedef struct
{
	float SoilMoisture;
	int16_t ADC_Reading;
} MoistSensorData_t;

static void MX_ADC4_Init(void);
/*-----------------------------------------------------------*/

static void prvPublishCommandCallback( MQTTAgentCommandContext_t * pxCommandContext,
                                       MQTTAgentReturnInfo_t * pxReturnInfo )
{
    configASSERT( pxCommandContext != NULL );
    configASSERT( pxReturnInfo != NULL );

    pxCommandContext->xReturnStatus = pxReturnInfo->returnCode;

    if( pxCommandContext->xTaskToNotify != NULL )
    {
        /* Send the context's ulNotificationValue as the notification value so
         * the receiving task can check the value it set in the context matches
         * the value it receives in the notification. */
        ( void ) xTaskNotifyGiveIndexed( pxCommandContext->xTaskToNotify,
                                         MQTT_NOTIFY_IDX );
    }
}

/*-----------------------------------------------------------*/

static BaseType_t prvPublishAndWaitForAck( MQTTAgentHandle_t xAgentHandle,
                                           const char * pcTopic,
                                           const void * pvPublishData,
                                           size_t xPublishDataLen )
{
    BaseType_t xResult = pdFALSE;
    MQTTStatus_t xStatus;

    configASSERT( pcTopic != NULL );
    configASSERT( pvPublishData != NULL );
    configASSERT( xPublishDataLen > 0 );

    MQTTPublishInfo_t xPublishInfo =
    {
        .qos             = MQTT_PUBLISH_QOS,
        .retain          = 0,
        .dup             = 0,
        .pTopicName      = pcTopic,
        .topicNameLength = strlen( pcTopic ),
        .pPayload        = pvPublishData,
        .payloadLength   = xPublishDataLen
    };

    MQTTAgentCommandContext_t xCommandContext =
    {
        .xTaskToNotify = xTaskGetCurrentTaskHandle(),
        .xReturnStatus = MQTTIllegalState,
    };

    MQTTAgentCommandInfo_t xCommandParams =
    {
        .blockTimeMs                 = MQTT_PUBLISH_BLOCK_TIME_MS,
        .cmdCompleteCallback         = prvPublishCommandCallback,
        .pCmdCompleteCallbackContext = &xCommandContext,
    };

    /* Clear the notification index */
    xTaskNotifyStateClearIndexed( NULL, MQTT_NOTIFY_IDX );


    xStatus = MQTTAgent_Publish( xAgentHandle,
                                 &xPublishInfo,
                                 &xCommandParams );

    if( xStatus == MQTTSuccess )
    {
        xResult = ulTaskNotifyTakeIndexed( MQTT_NOTIFY_IDX,
                                           pdTRUE,
                                           pdMS_TO_TICKS( MQTT_PUBLISH_NOTIFICATION_WAIT_MS ) );

        if( xResult == 0 )
        {
            LogError( "Timed out while waiting for publish ACK or Sent event. xTimeout = %d",
                      pdMS_TO_TICKS( MQTT_PUBLISH_NOTIFICATION_WAIT_MS ) );
            xResult = pdFALSE;
        }
        else if( xCommandContext.xReturnStatus != MQTTSuccess )
        {
            LogError( "MQTT Agent returned error code: %d during publish operation.",
                      xCommandContext.xReturnStatus );
            xResult = pdFALSE;
        }
    }
    else
    {
        LogError( "MQTTAgent_Publish returned error code: %d.",
                  xStatus );
    }

    return xResult;
}

static BaseType_t xIsMqttConnected( void )
{
    /* Wait for MQTT to be connected */
    EventBits_t uxEvents = xEventGroupWaitBits( xSystemEvents,
                                                EVT_MASK_MQTT_CONNECTED,
                                                pdFALSE,
                                                pdTRUE,
                                                0 );

    return( ( uxEvents & EVT_MASK_MQTT_CONNECTED ) == EVT_MASK_MQTT_CONNECTED );
}

/*-----------------------------------------------------------*/

static BaseType_t xInitSensors( void )
{
    int32_t lBspError = HAL_OK;

    /* You can init the ADC here */
    MX_ADC4_Init();

    /* Perform ADC calibration */
    // MICHAEL - fix undefined reference
    lBspError = HAL_ADCEx_Calibration_Start(&adc_handler, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    return( lBspError == HAL_OK ? pdTRUE : pdFALSE );
}

static BaseType_t xUpdateSensorData(MoistSensorData_t *pxData)
{
	/* supply Vdda (unit: mV).                                                  */
#define VDDA_APPLI                       (3300UL)

	uint16_t uhADCxConvertedData;
	//uint16_t uhADCxConvertedData_Voltage_mVolt;

	int32_t lBspError = HAL_OK;

	/* Start ADC group regular conversion */

	// MICHAEL - fix undefined reference
	lBspError = HAL_ADC_Start(&adc_handler);

	if (lBspError == HAL_OK)
	{

		/* Wait for ADC end of conversion */
		// MICHAEL - fix
		while (HAL_ADC_PollForConversion(&adc_handler, 0xFFFFFFFF) != HAL_OK)
		{
			// Do nothing
		}

		/* Retrieve ADC conversion data */
		// MICHAEL - fix
		uhADCxConvertedData = HAL_ADC_GetValue(&adc_handler) & 0xFFFFFFFE;

		pxData->ADC_Reading = uhADCxConvertedData;

		pxData->SoilMoisture = (100 * (MOIST_SESNOR_LOW_MOISTURE - (float)uhADCxConvertedData))/(MOIST_SESNOR_LOW_MOISTURE - MOIST_SESNOR_HIGH_MOISTURE);

		if(pxData->SoilMoisture < 0)
		{
			pxData->SoilMoisture = 0;
		}

		if(pxData->SoilMoisture > 100)
		{
			pxData->SoilMoisture = 100;
		}

		if(pxData->SoilMoisture > 70)
		{
            HAL_GPIO_WritePin( RELAY_1_GPIO_Port, RELAY_1_Pin, GPIO_PIN_RESET );/* Turn the GPIO off */
		}
	}

	return (lBspError == HAL_OK ? pdTRUE : pdFALSE);
}

/*-----------------------------------------------------------*/

extern UBaseType_t uxRand( void );

void vSoilMoistureSensorPublishTask( void * pvParameters )
{
    BaseType_t xResult = pdFALSE;
    BaseType_t xExitFlag = pdFALSE;
    char payloadBuf[ MQTT_PUBLISH_MAX_LEN ];
    MQTTAgentHandle_t xAgentHandle = NULL;
    char pcTopicString[ MQTT_PUBLICH_TOPIC_STR_LEN ] = { 0 };
    size_t xTopicLen = 0;
    float saved_SoilMoisture = -1;

    ( void ) pvParameters;

    xResult = xInitSensors();

    if( xResult != pdTRUE )
    {
        LogError( "Error while initializing moist sensors." );
        vTaskDelete( NULL );
    }

    xTopicLen = strlcat( pcTopicString, "/", MQTT_PUBLICH_TOPIC_STR_LEN );

    if( xTopicLen + 1 < MQTT_PUBLICH_TOPIC_STR_LEN )
    {
        ( void ) KVStore_getString( CS_CORE_THING_NAME, &( pcTopicString[ xTopicLen ] ), MQTT_PUBLICH_TOPIC_STR_LEN - xTopicLen );

        xTopicLen = strlcat( pcTopicString, "/"MQTT_PUBLISH_TOPIC, MQTT_PUBLICH_TOPIC_STR_LEN );
    }

    xAgentHandle = xGetMqttAgentHandle();

    while( xExitFlag == pdFALSE )
    {
        TickType_t xTicksToWait = pdMS_TO_TICKS( MQTT_PUBLISH_TIME_BETWEEN_MS );
        TimeOut_t xTimeOut;

        vTaskSetTimeOutState( &xTimeOut );

        MoistSensorData_t xMoistData;
        xResult = xUpdateSensorData( &xMoistData );

    	if( xResult != pdTRUE )
        {
            LogError( "Error while reading moist data." );
        }
        else if(( xIsMqttConnected() == pdTRUE ) /*&& (saved_SoilMoisture != xMoistData.SoilMoisture)*/)
        {
            int bytesWritten = 0;

            saved_SoilMoisture = xMoistData.SoilMoisture;

            /* Write to */
            bytesWritten = snprintf( payloadBuf,
                                     MQTT_PUBLISH_MAX_LEN,
									 moistureReport_JSON,
                                     xMoistData.SoilMoisture,
									 xMoistData.ADC_Reading);

            if( bytesWritten < MQTT_PUBLISH_MAX_LEN )
            {

            	// MICHAEL - execution time
            	double time_spent = 0.0;
            	clock_t begin = clock( );

                xResult = prvPublishAndWaitForAck( xAgentHandle,
                                                   pcTopicString,
                                                   payloadBuf,
                                                   bytesWritten );

                // MICHAEL - execution time
                clock_t end = clock( );
                time_spent = ( double ) ( end - begin ) * 1000.0 / CLOCKS_PER_SEC;
                //LogError( "Execution time: %f", time_spent );

            }
            else if( bytesWritten > 0 )
            {
                LogError( "Not enough buffer space." );
            }
            else
            {
                LogError( "Printf call failed." );
            }

            if( xResult == pdTRUE )
            {
                LogDebug( payloadBuf );
            }
        }

        /* Adjust remaining tick count */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE )
        {
            /* Wait until its time to poll the sensors again */
            vTaskDelay( xTicksToWait );
        }
    }
}


/**
  * @brief ADC4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC4_Init(void)
{

  /* USER CODE BEGIN ADC4_Init 0 */

  /* USER CODE END ADC4_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC4_Init 1 */

  /* USER CODE END ADC4_Init 1 */

  /** Common config
  */
  hadc4.Instance = ADC4;
  hadc4.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc4.Init.Resolution = ADC_RESOLUTION_12B;
  hadc4.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc4.Init.ScanConvMode = ADC4_SCAN_DISABLE;
  hadc4.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc4.Init.LowPowerAutoPowerOff = ADC_LOW_POWER_NONE;
  hadc4.Init.LowPowerAutoWait = DISABLE;
  hadc4.Init.ContinuousConvMode = DISABLE;
  hadc4.Init.NbrOfConversion = 1;
  hadc4.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc4.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc4.Init.DMAContinuousRequests = DISABLE;
  hadc4.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_LOW;
  hadc4.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc4.Init.SamplingTimeCommon1 = ADC4_SAMPLETIME_19CYCLES_5;
  hadc4.Init.SamplingTimeCommon2 = ADC4_SAMPLETIME_1CYCLE_5;
  hadc4.Init.OversamplingMode = ENABLE;
  hadc4.Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_8;
  hadc4.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4;
  hadc4.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;

  // MICHAEL - fix
  if(HAL_ADC_Init(&hadc4) != HAL_OK)
  {
	  __BKPT(0);
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC4_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC4_SAMPLINGTIME_COMMON_1;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;

  // MICHAEL - fix
  if(HAL_ADC_ConfigChannel(&hadc4, &sConfig) != HAL_OK)
  {
    __BKPT(0);
  }
  /* USER CODE BEGIN ADC4_Init 2 */

  /* USER CODE END ADC4_Init 2 */

}

/**
* @brief ADC MSP Initialization
* This function configures the hardware resources used in this example
* @param hadc: ADC handle pointer
* @retval None
*/
void HAL_ADC_MspInit(ADC_HandleTypeDef* hadc)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(hadc->Instance==ADC4)
  {
  /* USER CODE BEGIN ADC4_MspInit 0 */

  /* USER CODE END ADC4_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC;
    PeriphClkInit.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_HSI;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      __BKPT(0);
    }

    /* Peripheral clock enable */
    __HAL_RCC_ADC4_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**ADC4 GPIO Configuration
    PC0     ------> ADC4_IN1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN ADC4_MspInit 1 */

  /* USER CODE END ADC4_MspInit 1 */
  }

}

/**
* @brief ADC MSP De-Initialization
* This function freeze the hardware resources used in this example
* @param hadc: ADC handle pointer
* @retval None
*/
void HAL_ADC_MspDeInit(ADC_HandleTypeDef* hadc)
{
  if(hadc->Instance==ADC4)
  {
  /* USER CODE BEGIN ADC4_MspDeInit 0 */

  /* USER CODE END ADC4_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ADC4_CLK_DISABLE();

    /**ADC4 GPIO Configuration
    PC0     ------> ADC4_IN1
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0);

  /* USER CODE BEGIN ADC4_MspDeInit 1 */

  /* USER CODE END ADC4_MspDeInit 1 */
  }

}


