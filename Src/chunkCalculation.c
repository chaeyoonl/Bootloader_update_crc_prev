

#include "chunkCalculation.h"
#include "etx_ota_update.h"
#include <stdint.h>



uint16_t getDataLength(uint8_t *buf, int index) {
	uint16_t data_len = 0;

	if (buf[index-1] > 0x00) {	//length_2
	    	data_len = (buf[index-1] << 8) | buf[index];
	    }else {
	    	data_len = *(uint16_t *)&buf[index];
	    }

	return data_len;
}

uint32_t getAppTotalSaize(uint8_t sizse_1, uint8_t sizse_2, uint8_t sizse_3) {
	uint32_t data_len = 0;

	if (sizse_1 > 0x00) {
	    	data_len = (sizse_1 << 16) | (sizse_2 << 8) | sizse_3;
	    }else if (sizse_2 > 0x00) {
	    	data_len = (sizse_2 << 8) | sizse_3;
	    }else {
	    	data_len = sizse_3;
	    }

	return data_len;
}

uint32_t getCRC(uint8_t crc_1, uint8_t crc_2, uint8_t crc_3, uint8_t crc_4) {
	uint32_t data_len = 0;

	if (crc_1 > 0x00) {
	    	data_len = (crc_1 << 24) | (crc_2 << 16) | (crc_3 << 8) | crc_4;
	    }else if (crc_2 > 0x00) {
	    	data_len = (crc_2 << 16) | (crc_3 << 8) | crc_4;
	    }else if (crc_3 > 0x00) {
	    	data_len = (crc_3 << 8) | crc_4;
	    }else {
	    	data_len = crc_4;
	    }

	return data_len;
}
