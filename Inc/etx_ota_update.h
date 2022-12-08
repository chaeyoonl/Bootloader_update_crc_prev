

#ifndef INC_ETX_OTA_UPDATE_H_
#define INC_ETX_OTA_UPDATE_H_

#define ETX_OTA_SOF  0xAA    // Start of Frame
#define ETX_OTA_EOF  0xBB    // End of Frame
#define ETX_OTA_ACK  0x00    // ACK
#define ETX_OTA_NACK 0x01    // NACK

#define ETX_APP_FLASH_ADDR        0x08040000   //Application's Flash Address (Sector 6, 7)
#define ETX_APP_SLOT0_FLASH_ADDR  0x08080000   //App slot 0 address	(Sector 8, 9)
#define ETX_APP_SLOT1_FLASH_ADDR  0x080C0000   //App slot 1 address (Sector 10, 11)
#define ETX_CONFIG_FLASH_ADDR     0x08020000   //Configuration's address (Sector 5)

#define ETX_NO_OF_SLOTS           2            //Number of slots
#define ETX_SLOT_MAX_SIZE        (512 * 1024)  //Each slot size (512KB)

#define ETX_OTA_DATA_MAX_SIZE ( 1024 )  //Maximum data Size
#define ETX_OTA_DATA_OVERHEAD (    9 )  //data overhead
#define ETX_OTA_PACKET_MAX_SIZE ( ETX_OTA_DATA_MAX_SIZE + ETX_OTA_DATA_OVERHEAD )
#define ETX_OTA_MAX_FW_SIZE ( 1024 * 30 )

/*
 * Reboot reason
 */
#define ETX_FIRST_TIME_BOOT       ( 0xFFFFFFFF )      //First time boot
#define ETX_NORMAL_BOOT           ( 0xBEEFFEED )      //Normal Boot
#define ETX_OTA_REQUEST           ( 0xDEADBEEF )      //OTA request by application
#define ETX_LOAD_PREV_APP         ( 0xFACEFADE )      //App requests to load the previous version


/*
 * Exception codes
 */
typedef enum
{
  ETX_OTA_EX_OK       = 0,    // Success
  ETX_OTA_EX_ERR      = 1,    // Failure
}ETX_OTA_EX_;

/*
 * OTA process state
 */
typedef enum
{
  ETX_OTA_STATE_IDLE    = 0,
  ETX_OTA_STATE_START   = 1,
  ETX_OTA_STATE_HEADER  = 2,
  ETX_OTA_STATE_DATA    = 3,
  ETX_OTA_STATE_END     = 4,
}ETX_OTA_STATE_;

/*
 * Packet type
 */
typedef enum
{
  ETX_OTA_PACKET_TYPE_CMD       = 0,    // Command
  ETX_OTA_PACKET_TYPE_DATA      = 1,    // Data
  ETX_OTA_PACKET_TYPE_HEADER    = 2,    // Header
  ETX_OTA_PACKET_TYPE_RESPONSE  = 3,    // Response
}ETX_OTA_PACKET_TYPE_;

/*
 * Slot table
 */
typedef struct
{
	uint8_t  is_this_new_version;	  //Is this new version slot?
    uint8_t  is_this_slot_not_valid;  //Is this slot has a valid firmware/application?
    uint8_t  is_this_slot_active;     //Is this slot's firmware is currently running?
    uint8_t  should_we_run_this_fw;   //Do we have to run this slot's firmware?
    uint32_t  fw_size;                //Slot's firmware/application size
    uint32_t  fw_crc;                 //Slot's firmware/application CRC
}__attribute__((packed)) ETX_SLOT_;

/*
 * General configuration
 */
typedef struct
{
    ETX_SLOT_ slot_table[ETX_NO_OF_SLOTS];
}__attribute__((packed)) ETX_GNRL_CFG_;

/*
 * OTA Commands
 */
typedef enum
{
  ETX_OTA_CMD_START = 0,    // OTA Start command
  ETX_OTA_CMD_END   = 1,    // OTA End command
  ETX_OTA_CMD_ABORT = 2,    // OTA Abort command
}ETX_OTA_CMD_;

/*
 * OTA meta info
 */

typedef struct
{
  uint8_t   sof;
  uint8_t   packet_type;
  uint8_t  data_len_1;
  uint8_t  data_len_2;
  uint8_t   cmd;
  uint32_t  crc;
  uint8_t   eof;
}__attribute__((packed)) ETX_OTA_COMMAND_;

typedef struct
{
  uint8_t     sof;
  uint8_t     packet_type;
  uint8_t    app_total_size_1;
  uint8_t    app_total_size_2;
  uint8_t    app_total_size_3;
  uint8_t    app_size;
  uint8_t    crc_1;
  uint8_t    crc_2;
  uint8_t    crc_3;
  uint8_t    crc_4;
  uint8_t     eof;
}__attribute__((packed)) ETX_OTA_HEADER_;

typedef struct
{
  uint8_t     sof;
  uint8_t     packet_type;
  uint8_t    data_len_2;
  uint8_t    data_len_1;
  uint8_t     *data;
  uint8_t     eof;
}__attribute__((packed)) ETX_OTA_DATA_;


ETX_OTA_EX_ etx_ota_download_and_flash();
#endif /* INC_ETX_OTA_UPDATE_H_ */
