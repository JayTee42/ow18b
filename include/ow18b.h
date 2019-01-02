#ifndef __OW18B_H__
#define __OW18B_H__

#include <stdbool.h>
#include <stdint.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

//Use the device ID of the default adapter:
#define OW_DEV_ID_AUTOMATIC -1

typedef enum __ow_scan_mode_t__
{
	OW_SCAN_MODE_NONE,
	OW_SCAN_MODE_AUTOMATIC,
	OW_SCAN_MODE_MANUAL
} ow_scan_mode_t;

typedef enum __ow_scan_filter_type_t__
{
	OW_SCAN_FILTER_TYPE_ALL,
	OW_SCAN_FILTER_TYPE_LIMITED,
	OW_SCAN_FILTER_TYPE_GENERAL
} ow_scan_filter_type_t;

typedef struct __ow_scan_params_t__
{
	//Parameters to hci_le_set_scan_parameters(...):
	bool active_scan;
	uint16_t interval;
	uint16_t window;
	bool use_public_addr;
	bool use_whitelist;
	int to;

	//Parameters to hci_le_set_scan_enable(...):
	bool filter_dup;

	//Filter type to choose which advertising info blobs we look at:
	ow_scan_filter_type_t filter_type;

	//Which friendly device name to look for?
	const char* name;
} ow_scan_params;

typedef enum __ow_connect_mode_t__
{
	OW_CONNECT_MODE_AUTOMATIC,
	OW_CONNECT_MODE_MANUAL
} ow_connect_mode_t;

typedef struct __ow_connect_params_t__
{
	//Parameters to hci_le_create_conn(...):
	uint16_t interval;
	uint16_t window;
	bool use_whitelist;
	bool use_peer_public_addr;
	bool use_own_public_addr;
	uint16_t min_interval;
	uint16_t max_interval;
	uint16_t latency;
	uint16_t supervision_timeout;
	uint16_t min_ce_length;
	uint16_t max_ce_length;
	int to;
} ow_connect_params;

typedef struct __ow_config_t__
{
	//The device ID to use (can be OW_DEV_ID_AUTOMATIC):
	int dev_id;

	//The scan mode to find the address of the multimeter:
	ow_scan_mode_t scan_mode;

	union
	{
		//The address of the multimeter (only if scan_mode == OW_SCAN_MODE_NONE):
		bdaddr_t addr;

		//The parameters to use for scanning (only if scan_mode == OW_SCAN_MODE_MANUAL):
		ow_scan_params scan_params;
	};

	//The connect mode (could be bool, I guess ...):
	ow_connect_mode_t connect_mode;

	//The parameters to use for connecting (only if connect_mode == OW_CONNECT_MODE_AUTOMATIC):
	ow_connect_params connect_params;
} ow_config_t;

//The units of measurement:
typedef enum __ow_unit_t__
{
	OW_UNIT_MILLIVOLT, 			//0xF018 (DC), 0xF058 (AC)
	OW_UNIT_VOLT, 				//0xF020 (DC), 0xF060 (AC), 0xF2A0 (diode test)

	OW_UNIT_MICROAMPERE, 		//0xF090 (DC), 0xF0D0 (AC)
	OW_UNIT_MILLIAMPERE, 		//0xF098 (DC), 0xF0D8 (AC)
	OW_UNIT_AMPERE, 			//0xF0A0 (DC), 0xF0E0 (AC)

	OW_UNIT_OHM, 				//0xF120 (normal), 0xF2E0 (continuity test)
	OW_UNIT_KILOOHM, 			//0xF128
	OW_UNIT_MEGAOHM, 			//0xF130

	OW_UNIT_NANOFARAD, 			//0xF148
	OW_UNIT_MICROFARAD, 		//0xF150
	OW_UNIT_MILLIFARAD, 		//0xF158
	OW_UNIT_FARAD, 				//0xF160 (?)

	OW_UNIT_HERTZ, 				//0xF1A0
	OW_UNIT_PERCENT, 			//0xF1E0

	OW_UNIT_CELSIUS, 			//0xF220
	OW_UNIT_FAHRENHEIT, 		//0xF260

	OW_UNIT_NEARFIELD, 			//0xF360 (0...4)

	OW_UNIT_UNKNOWN
} ow_unit_t;

//The two types of current:
typedef enum __ow_current_type_t__
{
	OW_CURRENT_TYPE_DC,
	OW_CURRENT_TYPE_AC
} ow_current_type_t;

//A sample of measurement data:
typedef struct __ow_sample_t__
{
	//The unit of the data:
	ow_unit_t unit;

	//The current type (only defined for voltage and current units):
	ow_current_type_t current_type;

	//The sample value itself.
	//The decimal places are already incorporated.
	//On overflow, this will be NaN.
	double value;

	//Is the continuity test enabled (only defined for OW_UNIT_OHM)?
	bool is_continuity_test;

	//Is the diode test enabled (only defined for OW_UNIT_VOLT)?
	bool is_diode_test;

	//Is the multimeter in data-hold mode?
	bool is_data_hold;

	//Is the multimeter in relative mode?
	bool is_relative;

	//Is the multimeter in auto-range mode?
	bool is_auto_range;

	//Is the multimeter battery low?
	bool is_low_battery;
} ow_sample_t;

//A callback to a function that receives a sample and a user-provided context.
//The return value indicates if more samples shall be fetched.
typedef bool (*ow_sample_func_t)(ow_sample_t, void*);

//Convert sample stuff to strings:
const char* ow_unit_to_str(ow_unit_t unit);
const char* ow_unit_to_short_str(ow_unit_t unit);
const char* ow_current_type_to_str(ow_current_type_t current_type);

//Open a connection to the OWON device.
//Use the provided configuration.
//Provide samples via callback until false is returned.
//Then, perform a clean disconnect.
bool ow_recv(const ow_config_t* config, ow_sample_func_t callback, void* context);

//Same as "ow_recv(...)", but no callback is provided.
//Instead, we receive exactly n samples and write them to the given address.
bool ow_recv_n(const ow_config_t* config, ow_sample_t* samples, int n);

#endif
