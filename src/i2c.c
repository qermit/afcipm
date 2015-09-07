/*
 *   AFCIPMI  --
 *
 *   Copyright (C) 2015  Henrique Silva  <henrique.silva@lnls.br>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*!
 * @file i2c.c
 * @author Henrique Silva <henrique.silva@lnls.br>, LNLS
 * @date August 2015
 *
 * @brief Implementation of a generic I2C driver using FreeRTOS features
 */

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* C Standard includes */
#include "stdio.h"
#include "string.h"

/* Project includes */
#include "i2c.h"
#include "board_defs.h"

/* Project definitions */
/*! @todo Move these definitions to a LPC17x specific header, so we have a more generic macro */
#define I2CSTAT( id )               LPC_I2Cx(id)->STAT
#define I2CCONSET( id, val )        LPC_I2Cx(id)->CONSET = val
#define I2CCONCLR( id, val )        LPC_I2Cx(id)->CONCLR = val
#define I2CDAT_WRITE( id, val )     LPC_I2Cx(id)->DAT = val
#define I2CDAT_READ( id )           LPC_I2Cx(id)->DAT
#define I2CADDR_WRITE( id, val )    LPC_I2Cx(id)->ADR0 = val
#define I2CADDR_READ( id )          LPC_I2Cx(id)->ADR0
#define I2CMASK( id, val )          LPC_I2Cx(id)->MASK[0] = val

/*! @brief Configuration struct for each I2C interface */
xI2C_Config i2c_cfg[] = {
    {
        .reg = LPC_I2C0,
        .irq = I2C0_IRQn,
        .mode = I2C_Mode_IPMB,
        .pins = {
            .sda_port = I2C0_PORT,
            .sda_pin = I2C0_SDA_PIN,
            .scl_port = I2C0_PORT,
            .scl_pin = I2C0_SCL_PIN,
            .pin_func = I2C0_PIN_FUNC
        },
        .master_task_id = NULL,
        .slave_task_id = NULL,
        .rx_cnt = 0,
        .tx_cnt = 0,
		.mux_handler = NULL,
		.mux_state = -1,
    },
    {
        .reg = LPC_I2C1,
        .irq = I2C1_IRQn,
        .mode = I2C_Mode_Local_Master,
        .pins = {
            .sda_port = I2C1_PORT,
            .sda_pin = I2C1_SDA_PIN,
            .scl_port = I2C1_PORT,
            .scl_pin = I2C1_SCL_PIN,
            .pin_func = I2C1_PIN_FUNC
        },
        .master_task_id = NULL,
        .slave_task_id = NULL,
        .rx_cnt = 0,
        .tx_cnt = 0,
		.mux_handler = NULL,
		.mux_state = -1,
    },
    {
        .reg = LPC_I2C2,
        .irq = I2C2_IRQn,
        .mode = I2C_Mode_Local_Master,
        .pins = {
            .sda_port = I2C2_PORT,
            .sda_pin = I2C2_SDA_PIN,
            .scl_port = I2C2_PORT,
            .scl_pin = I2C2_SCL_PIN,
            .pin_func = I2C2_PIN_FUNC
        },
        .master_task_id = NULL,
        .slave_task_id = NULL,
        .rx_cnt = 0,
        .tx_cnt = 0,
		.mux_handler = NULL,
		.mux_state = -1,
    }
};

#define I2C_IFACE_COUNT (sizeof(i2c_cfg)/sizeof(xI2C_Config))

/*! @brief Array of mutexes to access #i2c_cfg global struct
 *
 * Each I2C interface has its own mutex and it must be taken
 * before setting/reading any field from #i2c_cfg struct,
 * since it's used by multiple tasks simultaneously
 */
static SemaphoreHandle_t I2C_mutex[I2C_IFACE_COUNT];

void vI2C_ISR( uint8_t i2c_id );

void I2C0_IRQHandler( void )
{
    vI2C_ISR( I2C0 );
}

void I2C1_IRQHandler( void )
{
    vI2C_ISR( I2C1 );
}

void I2C2_IRQHandler( void )
{
    vI2C_ISR( I2C2 );
}

#define I2C_CON_FLAGS (I2C_AA | I2C_SI | I2C_STO | I2C_STA)


/*! @brief I2C common interrupt service routine
 *
 * I2STAT register is handled inside this function, a state-machine-like implementation for I2C interface.
 *    
 * When a full message is trasmitted or received, the task whose handle is written to #i2c_cfg is notified, unblocking it. It also happens when an error occurs.
 * @warning Slave Transmitter mode states are not implemented in this driver and are just ignored.
 */
void vI2C_ISR( uint8_t i2c_id )
{
    /* Declare local variables */
    portBASE_TYPE xI2CSemaphoreWokeTask;

    /* Initialize variables */
    xI2CSemaphoreWokeTask = pdFALSE;
    uint32_t cclr = I2C_CON_FLAGS;

    /* I2C status handling */
    switch ( I2CSTAT( i2c_id ) ){
    case I2C_STAT_START:
    case I2C_STAT_REPEATED_START:
        i2c_cfg[i2c_id].rx_cnt = 0;
        i2c_cfg[i2c_id].tx_cnt = 0;
        /* Write Slave Address in the I2C bus, if there's nothing
         * to transmit, the last bit (R/W) will be set to 1 */
        I2CDAT_WRITE( i2c_id, ( i2c_cfg[i2c_id].msg.addr << 1 ) | ( i2c_cfg[i2c_id].msg.tx_len == 0 ) );
        break;

    case I2C_STAT_SLA_W_SENT_ACK:
        /* Send first data byte */
        I2CDAT_WRITE( i2c_id, i2c_cfg[i2c_id].msg.tx_data[i2c_cfg[i2c_id].tx_cnt] );
        i2c_cfg[i2c_id].tx_cnt++;
        break;

    case I2C_STAT_SLA_W_SENT_NACK:
        cclr &= ~I2C_STO;
        i2c_cfg[i2c_id].msg.error = i2c_err_SLA_W_SENT_NACK;
        vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].master_task_id, &xI2CSemaphoreWokeTask );
        break;

    case I2C_STAT_DATA_SENT_ACK:
        /* Transmit the remaining bytes */
        if ( i2c_cfg[i2c_id].msg.tx_len != i2c_cfg[i2c_id].tx_cnt ){
            I2CDAT_WRITE( i2c_id, i2c_cfg[i2c_id].msg.tx_data[i2c_cfg[i2c_id].tx_cnt] );
            i2c_cfg[i2c_id].tx_cnt++;
        } else {
            /* If there's no more data to be transmitted,
             * finish the communication and notify the caller task */
            cclr &= ~I2C_STO;
            vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].master_task_id, &xI2CSemaphoreWokeTask );
        }
        break;

    case I2C_STAT_DATA_SENT_NACK:
        cclr &= ~I2C_STO;
        i2c_cfg[i2c_id].msg.error = i2c_err_DATA_SENT_NACK;
        vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].master_task_id, &xI2CSemaphoreWokeTask );

    case I2C_STAT_SLA_R_SENT_ACK:
        /* SLA+R has been transmitted and ACK'd
         * If we want to receive only 1 byte, return NACK on the next byte */
        if ( i2c_cfg[i2c_id].msg.rx_len > 1 ){
             /* If we expect to receive more than 1 byte,
             * return ACK on the next byte */
            cclr &= ~I2C_AA;
        }
        break;

    case I2C_STAT_DATA_RECV_ACK:
        if ( i2c_cfg[i2c_id].rx_cnt < i2cMAX_MSG_LENGTH - 1 ){
            i2c_cfg[i2c_id].msg.rx_data[i2c_cfg[i2c_id].rx_cnt] = I2CDAT_READ( i2c_id );
            i2c_cfg[i2c_id].rx_cnt++;
            if (i2c_cfg[i2c_id].rx_cnt != (i2c_cfg[i2c_id].msg.rx_len) - 1 ){
                cclr &= ~I2C_AA;
            }
        }
        break;

    case I2C_STAT_DATA_RECV_NACK:
        i2c_cfg[i2c_id].msg.rx_data[i2c_cfg[i2c_id].rx_cnt] = I2CDAT_READ( i2c_id );
        i2c_cfg[i2c_id].rx_cnt++;
        cclr &= ~I2C_STO;
        /* There's no more data to be received */
        vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].master_task_id, &xI2CSemaphoreWokeTask );
        break;

    case I2C_STAT_SLA_R_SENT_NACK:
	cclr &= ~I2C_STO;
        /* Notify the error */
        i2c_cfg[i2c_id].msg.error = i2c_err_SLA_R_SENT_NACK;
        vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].master_task_id, &xI2CSemaphoreWokeTask );
        break;

        /* Slave Mode */
    case I2C_STAT_SLA_W_RECV_ACK:
    case I2C_STAT_ARB_LOST_SLA_W_RECV_ACK:

	    i2c_cfg[i2c_id].msg.i2c_id = i2c_id;
        i2c_cfg[i2c_id].rx_cnt = 0;

        if ( i2c_cfg[i2c_id].mode == I2C_Mode_IPMB ){
  	        i2c_cfg[i2c_id].msg.rx_data[i2c_cfg[i2c_id].rx_cnt] = I2CADDR_READ(i2c_id);
		    cclr &= ~I2C_AA;
		    i2c_cfg[i2c_id].rx_cnt++;
        }
		
        break;

    case I2C_STAT_SLA_DATA_RECV_ACK:
        /* Checks if the buffer is full */
        if ( i2c_cfg[i2c_id].rx_cnt < i2cMAX_MSG_LENGTH ){
            i2c_cfg[i2c_id].msg.rx_data[i2c_cfg[i2c_id].rx_cnt] = I2CDAT_READ( i2c_id );
            i2c_cfg[i2c_id].rx_cnt++;
            cclr &= ~I2C_AA;
        }
        break;

    case I2C_STAT_SLA_DATA_RECV_NACK:
        cclr &= ~I2C_AA;
        i2c_cfg[i2c_id].msg.error = i2c_err_SLA_DATA_RECV_NACK;
        break;

    case I2C_STAT_SLA_STOP_REP_START:
        i2c_cfg[i2c_id].msg.rx_len = i2c_cfg[i2c_id].rx_cnt;
        if (((i2c_cfg[i2c_id].rx_cnt > 0) && (i2c_cfg[i2c_id].mode == I2C_Mode_Local_Master ))) {
        		vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].slave_task_id, &xI2CSemaphoreWokeTask );
    	}
        if (((i2c_cfg[i2c_id].rx_cnt > 1) && (i2c_cfg[i2c_id].mode == I2C_Mode_IPMB ))) {
            vTaskNotifyGiveFromISR( i2c_cfg[i2c_id].slave_task_id, &xI2CSemaphoreWokeTask );
        }

        cclr &= ~I2C_AA;
        break;

    case I2C_STATUS_BUSERR:
	    cclr &= ~I2C_STO;
		break;
	
    default:
	    break;
    }

	if (!(cclr & I2C_CON_STO)) {
	    cclr &= ~I2C_CON_AA;

	}
	I2CCONSET(i2c_id, cclr ^ I2C_CON_FLAGS);
	I2CCONCLR(i2c_id, cclr);
	asm("nop");
	
    if (xI2CSemaphoreWokeTask == pdTRUE) {
        portYIELD_FROM_ISR(pdTRUE);
    }
}

void vI2CInit( I2C_ID_T i2c_id, I2C_Mode mode )
{
    char pcI2C_Tag[4];
    uint8_t sla_addr;

    sprintf( pcI2C_Tag, "I2C%u", i2c_id );
    /*! @todo Maybe wrap these functions, or use some board-specific defines
     * so this code is generic enough to be applied on other hardware.
     * Example: (if using LPC17xx and LPCOpen library)
     * @code
     * #define PIN_FUNC_CFG( port, pin, func ) Chip_IOCON_PinMux(...)
     * @endcode
    */
    Chip_IOCON_PinMux( LPC_IOCON, i2c_cfg[i2c_id].pins.sda_port, i2c_cfg[i2c_id].pins.sda_pin, IOCON_MODE_INACT, i2c_cfg[i2c_id].pins.pin_func );
    Chip_IOCON_PinMux( LPC_IOCON, i2c_cfg[i2c_id].pins.scl_port, i2c_cfg[i2c_id].pins.scl_pin, IOCON_MODE_INACT, i2c_cfg[i2c_id].pins.pin_func );
    Chip_IOCON_EnableOD( LPC_IOCON, i2c_cfg[i2c_id].pins.sda_port, i2c_cfg[i2c_id].pins.sda_pin );
    Chip_IOCON_EnableOD( LPC_IOCON, i2c_cfg[i2c_id].pins.scl_port, i2c_cfg[i2c_id].pins.scl_pin );
    NVIC_SetPriority(i2c_cfg[i2c_id].irq, configMAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ( i2c_cfg[i2c_id].irq );

    /* Create mutex for accessing the shared memory (i2c_cfg) */
    I2C_mutex[i2c_id] = xSemaphoreCreateMutex();

    /* Make sure that the mutex is freed */
    xSemaphoreGive( I2C_mutex[i2c_id] );

    /* Set I2C operating mode */
    if( xSemaphoreTake( I2C_mutex[i2c_id], 0 ) ) {
        i2c_cfg[i2c_id].mode = mode;
        xSemaphoreGive( I2C_mutex[i2c_id] );
    }

    /* Enable and configure I2C clock */
    Chip_I2C_Init( i2c_id );
    Chip_I2C_SetClockRate( i2c_id, 100000 );

    /* Enable I2C interface (Master Mode only) */
    I2CCONSET( i2c_id, I2C_I2EN );

    if ( mode == I2C_Mode_IPMB )
    {
        /* Configure Slave Address */
        sla_addr = get_ipmb_addr( );
        I2CADDR_WRITE( i2c_id, sla_addr );

        /* Configure Slave Address Mask */
        I2CMASK( i2c_id, 0xFE);

        /* Enable slave mode */
        I2CCONSET( i2c_id, I2C_AA );
    }

    /* Clear I2C0 interrupt (just in case) */
    I2CCONCLR( i2c_id, I2C_SI );

} /* End of vI2C_Init */


i2c_err xI2CWrite( I2C_ID_T i2c_id, uint8_t addr, uint8_t * tx_data, uint8_t tx_len )
{
    /* Checks if the message will fit in our buffer */
    if ( tx_len >= i2cMAX_MSG_LENGTH ) {
        return i2c_err_MAX_LENGTH;
    }

    /* Take the mutex to access the shared memory */
    if (xSemaphoreTake( I2C_mutex[i2c_id], 10 ) == pdFALSE) {
    	return i2c_err_FAILURE;
    }

    /* Populate the i2c config struct */
    i2c_cfg[i2c_id].msg.i2c_id = i2c_id;
    i2c_cfg[i2c_id].msg.addr = addr;
    memcpy(i2c_cfg[i2c_id].msg.tx_data, tx_data, tx_len);
    i2c_cfg[i2c_id].msg.tx_len = tx_len;
    i2c_cfg[i2c_id].msg.rx_len = 0;
    i2c_cfg[i2c_id].master_task_id = xTaskGetCurrentTaskHandle();


    /* Trigger the i2c interruption */
    /* @bug Is it safe to set the flag right now? Won't it stop another ongoing message that is being received for example? */
    I2CCONCLR( i2c_id, ( I2C_SI | I2C_STO | I2C_STA | I2C_AA));
    I2CCONSET( i2c_id, ( I2C_I2EN | I2C_STA ) );

    if ( ulTaskNotifyTake( pdTRUE, portMAX_DELAY ) == pdTRUE ){
        /* Include the error in i2c_cfg global structure */
        xSemaphoreGive( I2C_mutex[i2c_id] );

        return i2c_cfg[i2c_id].msg.error;
    }

    xSemaphoreGive( I2C_mutex[i2c_id] );
}

i2c_err xI2CRead( I2C_ID_T i2c_id, uint8_t addr, uint8_t * rx_data, uint8_t rx_len )
{
    /* Take the mutex to access shared memory */
	if (xSemaphoreTake( I2C_mutex[i2c_id], portMAX_DELAY ) == pdFALSE ) {
		return i2c_err_FAILURE;
	}

    i2c_cfg[i2c_id].msg.i2c_id = i2c_id;
    i2c_cfg[i2c_id].msg.addr = addr;
    i2c_cfg[i2c_id].msg.tx_len = 0;
    i2c_cfg[i2c_id].msg.rx_len = rx_len;
    i2c_cfg[i2c_id].master_task_id = xTaskGetCurrentTaskHandle();

    /* Trigger the i2c interruption */
    /* Is it safe to set the flag right now? Won't it stop another ongoing message that is being received for example? */
    I2CCONSET( i2c_id, ( I2C_I2EN | I2C_STA ) );

    /* Wait here until the message is received */
    if ( ulTaskNotifyTake( pdTRUE, portMAX_DELAY ) == pdTRUE ){
        /* Debug asserts */
        configASSERT(rx_data);
        configASSERT(i2c_cfg[i2c_id].msg.rx_data);

        /* Copy the received message to the given pointer */
        memcpy (rx_data, i2c_cfg[i2c_id].msg.rx_data, i2c_cfg[i2c_id].msg.rx_len );
    }

    xSemaphoreGive( I2C_mutex[i2c_id] );

    return i2c_cfg[i2c_id].msg.error;

}

uint8_t xI2CSlaveTransfer ( I2C_ID_T i2c_id, uint8_t * rx_data, uint32_t timeout )
{
    /* Take the mutex to access shared memory */
    xSemaphoreTake( I2C_mutex[i2c_id], portMAX_DELAY );

    /* Register this task as the one to be notified when a message comes */
    i2c_cfg[i2c_id].slave_task_id = xTaskGetCurrentTaskHandle();

    /* Relase mutex */
    xSemaphoreGive( I2C_mutex[i2c_id] );

    /* Function blocks here until a message is received */
    if ( ulTaskNotifyTake( pdTRUE, timeout ) == pdTRUE )
    {
            /* Debug asserts */
            configASSERT(rx_data);
            configASSERT(i2c_cfg[i2c_id].msg.rx_data);

            xSemaphoreTake( I2C_mutex[i2c_id], portMAX_DELAY );
            /* Copy the rx buffer to the pointer given */
            memcpy( rx_data, i2c_cfg[i2c_id].msg.rx_data, i2c_cfg[i2c_id].msg.rx_len );
            xSemaphoreGive( I2C_mutex[i2c_id] );
    } else {
        return 0;
    }
    /* Return message length */
    return i2c_cfg[i2c_id].msg.rx_len;
}

/*
 *==============================================================
 * MMC ADDRESSING
 *==============================================================
*/

/*! @brief Table holding all possible address values in IPMB specification
 * @see get_ipmb_addr()
 */
unsigned char IPMBL_TABLE[IPMBL_TABLE_SIZE] = {
    0x70, 0x8A, 0x72, 0x8E, 0x92, 0x90, 0x74, 0x8C, 0x76,
    0x98, 0x9C, 0x9A, 0xA0, 0xA4, 0x88, 0x9E, 0x86, 0x84,
    0x78, 0x94, 0x7A, 0x96, 0x82, 0x80, 0x7C, 0x7E, 0xA2 };

/*! The state of each GA signal is represented by G (grounded), U (unconnected), 
 *  or P (pulled up to Management Power).
 *
 *  The MMC drives P1 low and reads the GA lines. The MMC then drives P1 high and
 *  reads the GA lines. Any line that changes state between the two reads indicate
 *  an unconnected (U) pin.
 *
 *  The IPMB-L address of a Module can be calculated as (70h + Site Number x 2). <br>
 *  G = 0, P = 1, U = 2 <br>
 *  | Pin | Ternary | Decimal | Address |
 *  |:---:|:-------:|:-------:|:-------:|
 *  | GGG | 000 | 0  | 0x70 |
 *  | GGP | 001 | 1  | 0x8A |
 *  | GGU | 002 | 2  | 0x72 |
 *  | GPG | 010 | 3  | 0x8E |
 *  | GPP | 011 | 4  | 0x92 |
 *  | GPU | 012 | 5  | 0x90 |
 *  | GUG | 020 | 6  | 0x74 |
 *  | GUP | 021 | 7  | 0x8C |
 *  | GUU | 022 | 8  | 0x76 |
 *  | PGG | 100 | 9  | 0x98 |
 *  | PGP | 101 | 10 | 0x9C |
 *  | PGU | 102 | 11 | 0x9A |
 *  | PPG | 110 | 12 | 0xA0 |
 *  | PPP | 111 | 13 | 0xA4 |
 *  | PPU | 112 | 14 | 0x88 |
 *  | PUG | 120 | 15 | 0x9E |
 *  | PUP | 121 | 16 | 0x86 |
 *  | PUU | 122 | 17 | 0x84 |
 *  | UGG | 200 | 18 | 0x78 |
 *  | UGP | 201 | 19 | 0x94 |
 *  | UGU | 202 | 20 | 0x7A |
 *  | UPG | 210 | 21 | 0x96 |
 *  | UPP | 211 | 22 | 0x82 |
 *  | UPU | 212 | 23 | 0x80 |
 *  | UUG | 220 | 24 | 0x7C |
 *  | UUP | 221 | 25 | 0x7E |
 *  | UUU | 222 | 26 | 0xA2 |
 */
#define GPIO_GA_DELAY 10
uint8_t get_ipmb_addr( void )
{
    uint8_t ga0, ga1, ga2;
    uint8_t index;

    /* Set the test pin and read all GA pins */
    Chip_GPIO_SetPinState(LPC_GPIO, GA_TEST_PORT, GA_TEST_PIN, 1);

    /* when using NAMC-EXT-RTM at least 11 instruction cycles required
     *  to have correct GA value after GA_TEST_PIN changes */
    {
		uint8_t i;
		for (i = 0; i < GPIO_GA_DELAY; i++)
			asm volatile ("nop");
	}


    ga0 = Chip_GPIO_GetPinState(LPC_GPIO, GA0_PORT, GA0_PIN);
    ga1 = Chip_GPIO_GetPinState(LPC_GPIO, GA1_PORT, GA1_PIN);
    ga2 = Chip_GPIO_GetPinState(LPC_GPIO, GA2_PORT, GA2_PIN);

    /* Clear the test pin and see if any GA pin has changed is value,
     * meaning that it is unconnected */
    Chip_GPIO_SetPinState(LPC_GPIO, GA_TEST_PORT, GA_TEST_PIN, 0);

    /* when using NAMC-EXT-RTM at least 11 instruction cycles required
     *  to have correct GA value after GA_TEST_PIN changes */
    {
		uint8_t i;
		for (i = 0; i < GPIO_GA_DELAY; i++)
			asm volatile ("nop");
	}


    if ( ga0 != Chip_GPIO_GetPinState(LPC_GPIO, GA0_PORT, GA0_PIN) )
    {
        ga0 = UNCONNECTED;
    }

    if ( ga1 != Chip_GPIO_GetPinState(LPC_GPIO, GA1_PORT, GA1_PIN) )
    {
        ga1 = UNCONNECTED;
    }

    if ( ga2 != Chip_GPIO_GetPinState(LPC_GPIO, GA2_PORT, GA2_PIN) )
    {
        ga2 = UNCONNECTED;
    }

    /* Transform the 3-based code in a decimal number */
    index = (9 * ga2) + (3 * ga1) + (1 * ga0);

    if ( index >= IPMBL_TABLE_SIZE )
    {
        return 0;
    }

    return IPMBL_TABLE[index];
}
#undef GPIO_GA_DELAY

i2c_err xI2CMuxSetState(I2C_ID_T i2c_id, int8_t value, TickType_t xBlockTime) {
	if (i2c_id >= I2C_IFACE_COUNT) return i2c_err_UNKONWN_IFACE;
	BaseType_t semaphore_success = pdFALSE;

	xI2C_Config *p_i2c_config = &i2c_cfg[i2c_id];

	if (xBlockTime != 0) {
		semaphore_success = xSemaphoreTake(I2C_mutex[i2c_id], portMAX_DELAY);
		if (semaphore_success == pdFALSE) return i2c_err_FAILURE;
	}

	if (p_i2c_config->mux_handler != NULL && value != p_i2c_config->mux_state) {
		p_i2c_config->mux_handler(i2c_id, p_i2c_config, value);
	}

    if (xBlockTime != 0) xSemaphoreGive(I2C_mutex[i2c_id]);
    return i2c_err_SUCCESS;
}


i2c_err xI2CMuxRegister(I2C_ID_T i2c_id, MuxHandler_t handler, TickType_t xBlockTime) {
	if (i2c_id >= I2C_IFACE_COUNT) return i2c_err_UNKONWN_IFACE;
	BaseType_t semaphore_success = pdFALSE;

	xI2C_Config *p_i2c_config = &i2c_cfg[i2c_id];

	if (xBlockTime != 0) {
		semaphore_success = xSemaphoreTake(I2C_mutex[i2c_id], portMAX_DELAY);
		if (semaphore_success == pdFALSE) return i2c_err_FAILURE;
	}

	p_i2c_config->mux_handler = handler;
	p_i2c_config->mux_state = -1;

    if (xBlockTime != 0) xSemaphoreGive(I2C_mutex[i2c_id]);
    return i2c_err_SUCCESS;
}

