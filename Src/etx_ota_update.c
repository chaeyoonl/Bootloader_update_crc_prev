
#include <stdint.h>
#include <stdio.h>
#include "etx_ota_update.h"
#include "main.h"
#include <string.h>
#include <stdbool.h>
#include "rb.h"
#include "crc32.h"

uint8_t APP_BIN[ETX_OTA_MAX_FW_SIZE];
//uint8_t *APP_BIN;
int app_index = 0;

/* Buffer to hold the received data */
static uint8_t Rx_Buffer[ ETX_OTA_PACKET_MAX_SIZE ];

/* OTA State */
static ETX_OTA_STATE_ ota_state = ETX_OTA_STATE_IDLE;

/* Firmware Total Size that we are going to receive */
static uint8_t ota_fw_total_size;
static uint32_t app_total_size;
/* Firmware image's CRC32 */
static uint32_t ota_fw_crc;
/* Firmware Size that we have received */
static uint32_t ota_fw_received_size;
/* Slot number to write the received firmware */
static uint8_t slot_num_to_write;
/* Configuration */
ETX_GNRL_CFG_ *cfg_flash   = (ETX_GNRL_CFG_*) (ETX_CONFIG_FLASH_ADDR);

static uint16_t getDataLength(uint8_t *buf, int index);
static uint16_t etx_receive_chunk( uint8_t *buf, uint16_t max_len );
static ETX_OTA_EX_ etx_process_data( uint8_t *buf, uint16_t len );
static void etx_ota_send_resp( uint8_t type );
static HAL_StatusTypeDef write_data_to_slot( uint8_t slot_num,
		uint8_t *data,
		uint16_t data_len,
		bool is_first_block );
static HAL_StatusTypeDef write_data_to_flash_app( uint8_t *data,
		uint16_t data_len );
static uint8_t get_available_slot_number( void );
static HAL_StatusTypeDef write_cfg_to_flash( ETX_GNRL_CFG_ *cfg );

extern CRC_HandleTypeDef hcrc;
static UART_HandleTypeDef huart1;
extern RingFifo_t gtUart1Fifo;


/**
 * @brief Download the application from UART and flash it.
 * @param None
 * @retval ETX_OTA_EX_
 */
ETX_OTA_EX_ etx_ota_download_and_flash()
{
	ETX_OTA_EX_ ret  = ETX_OTA_EX_OK;
	uint16_t    len;

	printf("Waiting for the OTA data...\r\n");

	/* Reset the variables */
	ota_fw_total_size    = 0u;
	ota_fw_received_size = 0u;
	ota_fw_crc           = 0u;
	ota_state            = ETX_OTA_STATE_START;
	slot_num_to_write    = 0xFFu;

	do
	{
		//clear the buffer
		memset( Rx_Buffer, 0, ETX_OTA_PACKET_MAX_SIZE );

		len = etx_receive_chunk( Rx_Buffer, ETX_OTA_PACKET_MAX_SIZE );

		if( len != 0u )
		{
			ret = etx_process_data( Rx_Buffer, len );
		}
		else
		{
			//didn't received data. break.
			ret = ETX_OTA_EX_ERR;
		}

		//Send ACK or NACK
		if( ret != ETX_OTA_EX_OK )
		{
			printf("Sending NACK\r\n");
			break;
		}

	}while( ota_state != ETX_OTA_STATE_IDLE );

	return ret;
}

ETX_OTA_EX_ prev_download_and_flash()
{
	uint8_t prev_version = 0;	//Select Previous SLOT
	HAL_StatusTypeDef ret = ETX_OTA_EX_OK;;

	/* Read the configuration */
	ETX_GNRL_CFG_ cfg;
	memcpy( &cfg, cfg_flash, sizeof(ETX_GNRL_CFG_) );

	for( uint8_t i = 0; i < ETX_NO_OF_SLOTS; i++ ) {
		if( cfg.slot_table[i].is_this_new_version == 0u ) {
			prev_version = i;
			cfg.slot_table[i].should_we_run_this_fw  = 1u;	//update the slot

			break;
		}
	}


	uint32_t slot_addr;
	if( prev_version == 0u )
	{
		slot_addr = ETX_APP_SLOT0_FLASH_ADDR;
	}
	else
	{
		slot_addr = ETX_APP_SLOT1_FLASH_ADDR;
	}

	//Load the new app or firmware to app's flash address
	ret = write_data_to_flash_app( (uint8_t*)slot_addr, cfg.slot_table[prev_version].fw_size );
	if (ret != HAL_OK) {
		ret = ETX_OTA_EX_ERR;
	}else {
		ret = write_cfg_to_flash( &cfg );
		if( ret == HAL_OK ) ret = ETX_OTA_EX_OK;
	}

	//Verify the application is corrupted or not
	printf("Verifying the Application...");

	FLASH_WaitForLastOperation( HAL_MAX_DELAY );
	//Verify the application
	//uint32_t cal_data_crc = HAL_CRC_Calculate( &hcrc, (uint32_t*)ETX_APP_FLASH_ADDR, cfg.slot_table[slot_num].fw_size );
	uint32_t cal_data_crc = CalcCRC( (uint32_t*)ETX_APP_FLASH_ADDR, cfg.slot_table[prev_version].fw_size);
	FLASH_WaitForLastOperation( HAL_MAX_DELAY );

	//Verify the CRC
	if( cal_data_crc != cfg.slot_table[prev_version].fw_crc )
	{
		printf("ERROR!!!\r\n");
		printf("Invalid Application. HALT!!!\r\n");
		while(1);
	}
	return ret;
}


/**
 * @brief Process the received data from UART4.
 * @param buf buffer to store the received data
 * @param max_len maximum length to receive
 * @retval ETX_OTA_EX_
 */
static ETX_OTA_EX_ etx_process_data( uint8_t *buf, uint16_t len )
{
	ETX_OTA_EX_ ret = ETX_OTA_EX_ERR;

	do
	{
		if( ( buf == NULL ) || ( len == 0u) )
		{
			break;
		}

		//Check we received OTA Abort command
		ETX_OTA_COMMAND_ *cmd = (ETX_OTA_COMMAND_*)buf;
		if( cmd->packet_type == ETX_OTA_PACKET_TYPE_CMD )
		{
			if( cmd->cmd == ETX_OTA_CMD_ABORT )
			{
				//received OTA Abort command. Stop the process
				break;
			}
		}

		switch( ota_state )
		{
		case ETX_OTA_STATE_IDLE:
		{
			printf("ETX_OTA_STATE_IDLE...\r\n");
			ret = ETX_OTA_EX_OK;
		}
		break;

		case ETX_OTA_STATE_START:
		{
			ETX_OTA_COMMAND_ *cmd = (ETX_OTA_COMMAND_*)buf;

			if( cmd->packet_type == ETX_OTA_PACKET_TYPE_CMD )
			{
				if( cmd->cmd == ETX_OTA_CMD_START )
				{
					printf("Received OTA START Command\r\n");
					ota_state = ETX_OTA_STATE_HEADER;
					ret = ETX_OTA_EX_OK;
				}
			}
		}
		break;

		case ETX_OTA_STATE_HEADER:
		{
			ETX_OTA_HEADER_ *header = (ETX_OTA_HEADER_*)buf;
			if( header->packet_type == ETX_OTA_PACKET_TYPE_HEADER )
			{
				app_total_size = getAppTotalSaize(header->app_total_size_1, header->app_total_size_2, header->app_total_size_3);
				ota_fw_total_size = header->app_size;
				ota_fw_crc = getCRC(header->crc_1, header->crc_2, header->crc_3, header->crc_4);

				//get the slot number
				slot_num_to_write = get_available_slot_number();
				if( slot_num_to_write != 0xFF )
				{
					ota_state = ETX_OTA_STATE_DATA;
					ret = ETX_OTA_EX_OK;
				}
			}
		}
		break;


		case ETX_OTA_STATE_DATA:
		{
			ETX_OTA_DATA_     *data     = (ETX_OTA_DATA_*)buf;
			uint16_t          data_len = getDataLength(buf, 3);
			HAL_StatusTypeDef ex;

			if( data->packet_type == ETX_OTA_PACKET_TYPE_DATA )
			{

				bool is_first_block = false;
				if( ota_fw_received_size == 0 )
				{
					//This is the first block
					is_first_block = true;

					/* Read the configuration */
					ETX_GNRL_CFG_ cfg;
					memcpy( &cfg, cfg_flash, sizeof(ETX_GNRL_CFG_) );

					/* Before writing the data, reset the available slot */
					cfg.slot_table[slot_num_to_write].is_this_slot_not_valid = 1u;

					/* write back the updated config */
					ret = write_cfg_to_flash( &cfg );
					if( ret != ETX_OTA_EX_OK )
					{
						break;
					}
				}


				/* write the chunk to the Flash (App location) */
				ex = write_data_to_slot( slot_num_to_write, buf+4, data_len, is_first_block );

				if( ex == HAL_OK )
				{
					if( ota_fw_total_size > 0 )
					{
						ota_fw_total_size-=1;
					}else {
						//received the full data. So, move to end
						ota_state = ETX_OTA_STATE_END;
					}
					ret = ETX_OTA_EX_OK;
				}
			}
		}
		break;

		case ETX_OTA_STATE_END:
		{

			ETX_OTA_COMMAND_ *cmd = (ETX_OTA_COMMAND_*)buf;

			if( cmd->packet_type == ETX_OTA_PACKET_TYPE_CMD )
			{
				if( cmd->cmd == ETX_OTA_CMD_END )
				{
					printf("Received OTA END Command\r\n");

					uint32_t slot_addr;
					if( slot_num_to_write == 0u )
					{
						slot_addr = ETX_APP_SLOT0_FLASH_ADDR;
					}
					else
					{
						slot_addr = ETX_APP_SLOT1_FLASH_ADDR;
					}

					//Calculate and verify the CRC
					uint32_t cal_crc = CalcCRC( APP_BIN, app_total_size);

					if( cal_crc != ota_fw_crc )
					{
						printf("ERROR: FW CRC Mismatch\r\n");
						break;
					}

					/* Read the configuration */
					ETX_GNRL_CFG_ cfg;
					memcpy( &cfg, cfg_flash, sizeof(ETX_GNRL_CFG_) );

					//update the slot
					cfg.slot_table[slot_num_to_write].is_this_new_version = 1u;
					cfg.slot_table[slot_num_to_write].fw_crc                 = cal_crc;
					cfg.slot_table[slot_num_to_write].fw_size                = app_total_size;
					cfg.slot_table[slot_num_to_write].is_this_slot_not_valid = 0u;
					cfg.slot_table[slot_num_to_write].should_we_run_this_fw  = 1u;

					//reset other slots
					for( uint8_t i = 0; i < ETX_NO_OF_SLOTS; i++ )
					{
						if( slot_num_to_write != i )
						{
							//update the slot as inactive
							cfg.slot_table[i].should_we_run_this_fw = 0u;
							cfg.slot_table[i].is_this_new_version  = 0u;
						}
					}

					/* write back the updated config */
					ret = write_cfg_to_flash( &cfg );
					if( ret == ETX_OTA_EX_OK )
					{
						ota_state = ETX_OTA_STATE_IDLE;
						ret = ETX_OTA_EX_OK;
					}
				}
			}
		}
		break;

		default:
		{
			/* Should not come here */
			ret = ETX_OTA_EX_ERR;
		}
		break;
		};
	}while( false );

	return ret;
}

/**
 * @brief Receive a one chunk of data.
 * @param buf buffer to store the received data
 * @param max_len maximum length to receive
 * @retval ETX_OTA_EX_
 */
static uint16_t etx_receive_chunk( uint8_t *buf, uint16_t max_len )
{
	int16_t  ret;
	uint16_t data_len;
	int index = 0;
	int forNumber = 0;

	do
	{
		buf[index++] = RB_read(&gtUart1Fifo);	//SOF

		if( buf[index-1] != ETX_OTA_SOF )
		{
			//Not received start of frame
			ret = ETX_OTA_EX_ERR;
			break;
		}

		buf[index++] = RB_read(&gtUart1Fifo);	//packet type

		if (buf[index-1] == 2) {	//header
			forNumber = 8;	//app_total_size, app_size, CRC
		}else {
			forNumber = 2;	//length_1, length_2
		}

		for (int i = 0 ; i < forNumber ; i++) {
			buf[index++] = RB_read(&gtUart1Fifo);
		}




		data_len = getDataLength(buf, index-1);

		if (buf[1] != 2) {
			for( uint16_t i = 0u; i < data_len; i++ )
			{
				buf[index++] = RB_read(&gtUart1Fifo);
			}
		}

		do{
			buf[index] = RB_read(&gtUart1Fifo);	//EOF
		}while(buf[index] != ETX_OTA_EOF);

	}while( false );

	if( max_len < index )
	{
		printf("Received more data than expected. Expected = %d, Received = %d\r\n",
				max_len, index );
		index = 0u;
	}

	return index;
}

/**
 * @brief Write data to the Application's actual flash location.
 * @param data data to be written
 * @param data_len data length
 * @is_first_block true - if this is first block, false - not first block
 * @retval HAL_StatusTypeDef
 */
static HAL_StatusTypeDef write_data_to_flash_app( uint8_t *data,
		uint16_t data_len )
{
	HAL_StatusTypeDef ret;

	do
	{
		ret = HAL_FLASH_Unlock();
		if( ret != HAL_OK )
		{
			break;
		}

		//Check if the FLASH_FLAG_BSY.
		FLASH_WaitForLastOperation( HAL_MAX_DELAY );

		// clear all flags before you write it to flash
		__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
				FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR);


		printf("Erasing the Flash memory...\r\n");
		//Erase the Flash
		FLASH_EraseInitTypeDef EraseInitStruct;
		uint32_t SectorError;

		EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
		EraseInitStruct.Sector        = FLASH_SECTOR_6;
		EraseInitStruct.NbSectors     = 2;                    //erase 2 sectors(6,7)
		EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;

		ret = HAL_FLASHEx_Erase( &EraseInitStruct, &SectorError );
		if( ret != HAL_OK )
		{
			break;
		}

		for(int i = 0; i < data_len; i++ )
		{
			ret = HAL_FLASH_Program( FLASH_TYPEPROGRAM_BYTE,
					(ETX_APP_FLASH_ADDR + i),
					data[i]
			);
			if( ret == HAL_OK )
			{
				//update the data count
				ota_fw_received_size += 1;
			}
			else
			{
				printf("Flash Write Error\r\n");
				break;
			}
		}

		if( ret != HAL_OK )
		{
			break;
		}

		ret = HAL_FLASH_Lock();
		if( ret != HAL_OK )
		{
			break;
		}
	}while( false );

	return ret;
}

/**
 * @brief Load the new app to the app's actual flash memory.
 * @param none
 * @retval none
 */
void load_new_app( void )
{
	bool              is_update_available = false;
	uint8_t           slot_num;
	HAL_StatusTypeDef ret;

	/* Read the configuration */
	ETX_GNRL_CFG_ cfg;
	memcpy( &cfg, cfg_flash, sizeof(ETX_GNRL_CFG_) );

	/*
	 * Check the slot whether it has a new application.
	 */

	for( uint8_t i = 0; i < ETX_NO_OF_SLOTS; i++ )
	{
		if( cfg.slot_table[i].should_we_run_this_fw == 1u )
		{
			printf("New Application is available in the slot %d!!!\r\n", i);
			is_update_available               = true;
			slot_num                          = i;

			//update the slot
			cfg.slot_table[i].is_this_slot_active    = 1u;
			cfg.slot_table[i].should_we_run_this_fw  = 0u;

			break;
		}
	}

	if( is_update_available )
	{
		//make other slots inactive
		for( uint8_t i = 0; i < ETX_NO_OF_SLOTS; i++ )
		{
			if( slot_num != i )
			{
				//update the slot as inactive
				cfg.slot_table[i].is_this_slot_active = 0u;
			}
		}

		uint32_t slot_addr;
		if( slot_num == 0u )
		{
			slot_addr = ETX_APP_SLOT0_FLASH_ADDR;
		}
		else
		{
			slot_addr = ETX_APP_SLOT1_FLASH_ADDR;
		}

		//Load the new app or firmware to app's flash address
		ret = write_data_to_flash_app( (uint8_t*)slot_addr, cfg.slot_table[slot_num].fw_size );
		if( ret != HAL_OK )
		{
			printf("App Flash write Error\r\n");
		}
		else
		{
			/* write back the updated config */
			ret = write_cfg_to_flash( &cfg );
			if( ret != HAL_OK )
			{
				printf("Config Flash write Error\r\n");
			}
		}
	}
	else
	{
		//Find the active slot in case the update is not available
		for( uint8_t i = 0; i < ETX_NO_OF_SLOTS; i++ )
		{
			if( cfg.slot_table[i].is_this_slot_active == 1u )
			{
				slot_num = i;
				break;
			}
		}
	}

	//Verify the application is corrupted or not
	printf("Verifying the Application...");

	FLASH_WaitForLastOperation( HAL_MAX_DELAY );
	//Verify the application
	//uint32_t cal_data_crc = HAL_CRC_Calculate( &hcrc, (uint32_t*)ETX_APP_FLASH_ADDR, cfg.slot_table[slot_num].fw_size );
	uint32_t cal_data_crc = CalcCRC( (uint32_t*)ETX_APP_FLASH_ADDR, cfg.slot_table[slot_num].fw_size);
	FLASH_WaitForLastOperation( HAL_MAX_DELAY );

	//Verify the CRC
	if( cal_data_crc != cfg.slot_table[slot_num].fw_crc )
	{
		printf("ERROR!!!\r\n");
		printf("Invalid Application. HALT!!!\r\n");
		while(1);
	}
	printf("Done!!!\r\n");
}

/**
 * @brief Write data to the Slot
 * @param slot_num slot to be written
 * @param data data to be written
 * @param data_len data length
 * @is_first_block true - if this is first block, false - not first block
 * @retval HAL_StatusTypeDef
 */
static HAL_StatusTypeDef write_data_to_slot( uint8_t slot_num,
		uint8_t *data,
		uint16_t data_len,
		bool is_first_block )
{
	HAL_StatusTypeDef ret;

	do
	{

		if( slot_num >= ETX_NO_OF_SLOTS )
		{
			ret = HAL_ERROR;
			break;
		}

		ret = HAL_FLASH_Unlock();
		if( ret != HAL_OK )
		{
			break;
		}

		//No need to erase every time. Erase only the first time.
		if( is_first_block )
		{
			printf("Erasing the Slot %d Flash memory...\r\n", slot_num);
			//Erase the Flash
			FLASH_EraseInitTypeDef EraseInitStruct;
			uint32_t SectorError;

			EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
			if( slot_num == 0 )
			{
				EraseInitStruct.Sector        = FLASH_SECTOR_8;
			}
			else
			{
				EraseInitStruct.Sector        = FLASH_SECTOR_10;
			}
			EraseInitStruct.NbSectors     = 2;                    //erase 2 sectors
			EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;

			ret = HAL_FLASHEx_Erase( &EraseInitStruct, &SectorError );
			if( ret != HAL_OK )
			{
				printf("Flash Erase Error\r\n");
				break;
			}
		}

		uint32_t flash_addr;
		if( slot_num == 0 )
		{
			flash_addr = ETX_APP_SLOT0_FLASH_ADDR;
		}
		else
		{
			flash_addr = ETX_APP_SLOT1_FLASH_ADDR;
		}

		for(int i = 0; i < data_len; i++ )
		{
			ret = HAL_FLASH_Program( FLASH_TYPEPROGRAM_BYTE,
					(flash_addr + ota_fw_received_size),
					data[i]
			);
			APP_BIN[app_index++] = data[i];

			if( ret == HAL_OK )
			{
				//update the data count
				ota_fw_received_size += 1;
			}
			else
			{
				printf("Flash Write Error\r\n");
				break;
			}
		}

		if( ret != HAL_OK )
		{
			break;
		}

		ret = HAL_FLASH_Lock();
		if( ret != HAL_OK )
		{
			break;
		}
	}while( false );

	return ret;
}


/**
 * @brief Return the available slot number
 * @param none
 * @retval slot number
 */
static uint8_t get_available_slot_number( void )
{
	uint8_t   slot_number = 0xFF;

	/* Read the configuration */
	ETX_GNRL_CFG_ cfg;
	memcpy( &cfg, cfg_flash, sizeof(ETX_GNRL_CFG_) );
	/*
	 * Check the slot is valid or not. If it is valid,
	 * then check the slot is active or not.
	 *
	 * If it is valid and not active, then use that slot.
	 * If it is not valid, then use that slot.
	 *
	 */

	for( uint8_t i = 0; i < ETX_NO_OF_SLOTS; i++ )
	{
		if( ( cfg.slot_table[i].is_this_slot_not_valid != 0u ) || ( cfg.slot_table[i].is_this_slot_active == 0u ) )
		{
			slot_number = i;
			printf("Slot %d is available for OTA update\r\n", slot_number);
			break;
		}
	}

	return slot_number;
}

/**
 * @brief Write the configuration to flash
 * @param cfg config structure
 * @retval none
 */
static HAL_StatusTypeDef write_cfg_to_flash( ETX_GNRL_CFG_ *cfg )
{
	HAL_StatusTypeDef ret;

	do
	{
		if( cfg == NULL )
		{
			ret = HAL_ERROR;
			break;
		}

		ret = HAL_FLASH_Unlock();
		if( ret != HAL_OK )
		{
			break;
		}

		//Check if the FLASH_FLAG_BSY.
		FLASH_WaitForLastOperation( HAL_MAX_DELAY );

		//Erase the Flash
		FLASH_EraseInitTypeDef EraseInitStruct;
		uint32_t SectorError;

		EraseInitStruct.TypeErase     = FLASH_TYPEERASE_SECTORS;
		EraseInitStruct.Sector        = FLASH_SECTOR_5;
		EraseInitStruct.NbSectors     = 1;                    //erase only sector 5
		EraseInitStruct.VoltageRange  = FLASH_VOLTAGE_RANGE_3;

		// clear all flags before you write it to flash
		__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR |
				FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR);

		ret = HAL_FLASHEx_Erase( &EraseInitStruct, &SectorError );
		if( ret != HAL_OK )
		{
			break;
		}

		//write the configuration
		uint8_t *data = (uint8_t *) cfg;
		for( uint32_t i = 0u; i < sizeof(ETX_GNRL_CFG_); i++ )
		{
			ret = HAL_FLASH_Program( FLASH_TYPEPROGRAM_BYTE,
					ETX_CONFIG_FLASH_ADDR + i,
					data[i]
			);
			if( ret != HAL_OK )
			{
				printf("Slot table Flash Write Error\r\n");
				break;
			}

		}

		//Check if the FLASH_FLAG_BSY.
		FLASH_WaitForLastOperation( HAL_MAX_DELAY );

		if( ret != HAL_OK )
		{
			break;
		}

		ret = HAL_FLASH_Lock();
		if( ret != HAL_OK )
		{
			break;
		}
	}while( false );

	return ret;
}
