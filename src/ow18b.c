#include "ow18b.h"

#include <math.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

//Magic numbers for scanning:
#define OW_SCAN_ENABLE 0x01
#define OW_SCAN_DISABLE 0x00
#define OW_SCAN_FILTER_FLAGS_TYPE 0x01
#define OW_SCAN_FILTER_FLAGS_LIMITED (1 << 0)
#define OW_SCAN_FILTER_FLAGS_GENERAL (1 << 1)
#define OW_SCAN_SHORT_DEVICE_NAME 0x08
#define OW_SCAN_DEVICE_NAME 0x09
#define OW_SCAN_SUBEVENT_ADVERTISING_INFO 0x02

//Length validation for scanning:
#define OW_SCAN_META_OFFSET (1 + HCI_EVENT_HDR_SIZE)
#define OW_SCAN_MIN_LENGTH (OW_SCAN_META_OFFSET + sizeof(evt_le_meta_event) + 1 + sizeof(le_advertising_info))

//The maximum length of a device's friendly name, without zero terminator:
#define OW_MAX_NAME_LENGTH 29

//The exact length of a sample packet that contains measurement data:
#define OW_SAMPLE_LENGTH 18

//HCI-ACL-L2CAP-ATT magic numbers:
#define OW_L2CAP_DEST_CID ((uint16_t)0x0004)
#define OW_ATT_OPCODE_HANDLE_VALUE_NOTIFICATION ((uint8_t)0x001B)
#define OW_ATT_HANDLE ((uint16_t)0x001B)

//Internally used for ow_recv_n(...):
typedef struct __ow_recv_n_context_t__
{
	ow_sample_t* samples;
	int n;
	int count;
} ow_recv_n_context_t;

//The scan parameters for automatic scans:
static ow_scan_params automatic_scan_params =
{
	//Ripped from hcitool.
	.active_scan = true,
	.interval = 16,
	.window = 16,
	.use_public_addr = true,
	.use_whitelist = false,
	.to = 10000,
	.filter_dup = true,

	//No filtering by default:
	.filter_type = OW_SCAN_FILTER_TYPE_ALL,

	//This is the name our multimeter displays:
	.name = "BDM"
};

//The connect parameters for automatic connections:
static ow_connect_params automatic_connect_params =
{
	//Ripped from hcitool again.
	.interval = 4,
	.window = 4,
	.use_whitelist = false,
	.use_peer_public_addr = true,
	.use_own_public_addr = true,
	.min_interval = 15,
	.max_interval = 15,
	.latency = 0,
	.supervision_timeout = 3200,
	.min_ce_length = 1,
	.max_ce_length = 1,
	.to = 25000
};

//Get the device ID of the default Bluetooth adapter.
//Sets errno on error.
static bool ow_get_default_device_id(int* dev_id);

//Open a socket for the given device ID.
//Sets errno on error.
static bool ow_open_socket(int dev_id, int* bt_sock);

//Get resp. set a HCI filter for the given socket.
//Sets errno on error.
static bool ow_get_hci_filter(int bt_sock, struct hci_filter* filter, socklen_t* filter_length);
static bool ow_set_hci_filter(int bt_sock, struct hci_filter* filter, socklen_t filter_length);

//Read the flags from a given advertising info struct:
static bool ow_scan_read_flags(const le_advertising_info* info, uint8_t* flags);

//Does the given advertising info match the given filter type?
static bool ow_scan_filter_matches_info(ow_scan_filter_type_t filter_type, const le_advertising_info* info);

//Try to parse the friendly name of a device from a given advertising struct:
static bool ow_scan_parse_friendly_name(const le_advertising_info* info, char* name);

//Scan for the multimeter using the provided socket and scan parameters.
//Sets errno on error.
static bool ow_scan_for_address(int bt_sock, const ow_scan_params* params, struct hci_filter* old_hci_filter, socklen_t old_hci_filter_length, bdaddr_t* addr);

//Connect to the multimeter.
//Sets errno on error.
static bool ow_connect(int bt_sock, bdaddr_t addr, const ow_connect_params* params, uint16_t* hci_handle);

//An internal sample func for ow_recv_n(...):
static bool ow_recv_n_sample(ow_sample_t sample, void* context);

static bool ow_get_default_device_id(int* dev_id)
{
	//Get the device ID of the default adapter:
	*dev_id = hci_get_route(NULL);

	if (*dev_id < 0)
	{
		errno = ENODEV;
		return false;
	}

	return true;
}

static bool ow_open_socket(int dev_id, int* bt_sock)
{
	*bt_sock = hci_open_dev(dev_id);
	return (*bt_sock >= 0);
}

static bool ow_get_hci_filter(int bt_sock, struct hci_filter* filter, socklen_t* filter_length)
{
	//Tell getsockopt(...) how much buffer space there is:
	*filter_length = sizeof(struct hci_filter);

	//Get the filter:
	return (getsockopt(bt_sock, SOL_HCI, HCI_FILTER, filter, filter_length) == 0);
}

static bool ow_set_hci_filter(int bt_sock, struct hci_filter* filter, socklen_t filter_length)
{
	//Set the filter:
	return (setsockopt(bt_sock, SOL_HCI, HCI_FILTER, filter, filter_length) == 0);
}

static bool ow_scan_read_flags(const le_advertising_info* info, uint8_t* flags)
{
	size_t offset = 0;

	//Make sure we don't read too much:
	while (offset < info->length)
	{
		uint8_t length = info->data[offset];

		//Validate the length:
		if ((length == 0) || ((offset + length) >= info->length))
		{
			break;
		}

		//Get the type:
		uint8_t type = info->data[offset + 1];

		//Found the flags:
		if ((type == OW_SCAN_FILTER_FLAGS_TYPE) && (length >= 2))
		{
			*flags = info->data[offset + 2];
			return true;
		}

		//Increment the offset:
		offset += 1 + length;
	}

	return false;
}

static bool ow_scan_filter_matches_info(ow_scan_filter_type_t filter_type, const le_advertising_info* info)
{
	//Match everything?
	if (filter_type == OW_SCAN_FILTER_TYPE_ALL)
	{
		return true;
	}

	//Read the flags. This can fail (which will be treated as no-match).
	uint8_t flags;

	if (!ow_scan_read_flags(info, &flags))
	{
		return false;
	}

	switch (filter_type)
	{
	case OW_SCAN_FILTER_TYPE_LIMITED: return (flags & OW_SCAN_FILTER_FLAGS_LIMITED);
	case OW_SCAN_FILTER_TYPE_GENERAL: return (flags & (OW_SCAN_FILTER_FLAGS_LIMITED | OW_SCAN_FILTER_FLAGS_GENERAL));

	default: return false;
	}
}

static bool ow_scan_parse_friendly_name(const le_advertising_info* info, char* name)
{
	size_t offset = 0;

	//Make sure we don't read too much:
	while (offset < info->length)
	{
		uint8_t length = info->data[offset];

		//Validate the length:
		if ((length == 0) || ((offset + length) >= info->length))
		{
			break;
		}

		//Get the type:
		uint8_t type = info->data[offset + 1];

		if ((type == OW_SCAN_SHORT_DEVICE_NAME) || (type == OW_SCAN_DEVICE_NAME))
		{
			//Validate the name length:
			size_t name_length = length - 1;

			if (name_length <= OW_MAX_NAME_LENGTH)
			{
				//Store the name:
				memcpy(name, &info->data[offset + 2], name_length);

				//Terminate it:
				name[name_length] = '\0';

				return true;
			}
		}

		//Increment the offset:
		offset += 1 + length;
	}

	return false;
}

static bool ow_scan_for_address(int bt_sock, const ow_scan_params* params, struct hci_filter* old_hci_filter, socklen_t old_hci_filter_length, bdaddr_t* addr)
{
	//Also ripped from hcitool :) thx, guys

	//Filter HCI events:
	struct hci_filter event_filter;

	hci_filter_clear(&event_filter);
	hci_filter_set_ptype(HCI_EVENT_PKT, &event_filter);
	hci_filter_set_event(EVT_LE_META_EVENT, &event_filter);

	if (!ow_set_hci_filter(bt_sock, &event_filter, sizeof(struct hci_filter)))
	{
		return false;
	}

	//Adjust the scan parameters:
	int error;

	if (hci_le_set_scan_parameters(bt_sock, params->active_scan ? 1 : 0, htobs(params->interval), htobs(params->window), params->use_public_addr ? LE_PUBLIC_ADDRESS : LE_RANDOM_ADDRESS, params->use_whitelist ? 1 : 0, params->to) < 0)
	{
		error = errno;
		goto restore_out;
	}

	//Enable the LE scan:
	if (hci_le_set_scan_enable(bt_sock, OW_SCAN_ENABLE, params->filter_dup ? 1 : 0, params->to) < 0)
	{
		error = errno;
		goto restore_out;
	}

	//Scan until we hit the device:
	while (1)
	{
		//Read a new buffer of data:
		uint8_t buf[HCI_MAX_EVENT_SIZE];
		int bytes_read = read(bt_sock, buf, HCI_MAX_EVENT_SIZE);

		//Error case?
		if (bytes_read < 0)
		{
			//Recoverable cases:
			if ((errno == EAGAIN) || (errno == EINTR))
			{
				continue;
			}

			//Fatal cases:
			error = errno;
			goto disable_restore_out;
		}

		//EoF case?
		if (bytes_read == 0)
		{
			error = ENODATA;
			goto disable_restore_out;
		}

		//Success case, but not enough bytes?
		if ((size_t)bytes_read < OW_SCAN_MIN_LENGTH)
		{
			continue;
		}

		//Retrieve a pointer to the meta struct:
		evt_le_meta_event* meta = (evt_le_meta_event*)(buf + OW_SCAN_META_OFFSET);

		if (meta->subevent != OW_SCAN_SUBEVENT_ADVERTISING_INFO)
		{
			error = ENODATA;
			goto disable_restore_out;
		}

		//Move forward to the info struct:
		le_advertising_info* info = (le_advertising_info*)(meta->data + 1);

		//Make sure there is enough space for the info's data member:
		if ((size_t)bytes_read < (OW_SCAN_MIN_LENGTH + info->length))
		{
			continue;
		}

		//Filtering?
		if (!ow_scan_filter_matches_info(params->filter_type, info))
		{
			continue;
		}

		//Parse the friendly name of the device:
		char name[OW_MAX_NAME_LENGTH + 1];

		if (!ow_scan_parse_friendly_name(info, name))
		{
			continue;
		}

		//Match?
		if (strcmp(name, params->name) == 0)
		{
			*addr = info->bdaddr;
			error = 0;

			goto disable_restore_out;
		}
	}

disable_restore_out:
	//Disable the LE scan:
	hci_le_set_scan_enable(bt_sock, OW_SCAN_DISABLE, params->filter_dup ? 1 : 0, params->to);

restore_out:
	//Go back to the old HCI filter:
	ow_set_hci_filter(bt_sock, old_hci_filter, old_hci_filter_length);

	if (error != 0)
	{
		errno = error;
		return false;
	}

	return true;
}

static bool ow_connect(int bt_sock, bdaddr_t addr, const ow_connect_params* params, uint16_t* hci_handle)
{
	return (hci_le_create_conn(bt_sock, htobs(params->interval), htobs(params->window), params->use_whitelist ? 1 : 0, params->use_peer_public_addr ? LE_PUBLIC_ADDRESS : LE_RANDOM_ADDRESS, addr, params->use_own_public_addr ? LE_PUBLIC_ADDRESS : LE_RANDOM_ADDRESS, htobs(params->min_interval), htobs(params->max_interval), htobs(params->latency), htobs(params->supervision_timeout), htobs(params->min_ce_length), htobs(params->max_ce_length), hci_handle, params->to) >= 0);
}

static bool ow_recv_n_sample(ow_sample_t sample, void* context)
{
	//Get the context:
	ow_recv_n_context_t* recv_n_context = context;

	//Store the sample:
	recv_n_context->samples[recv_n_context->count++] = sample;

	//More samples to receive?
	return (recv_n_context->count < recv_n_context->n);
}

const char* ow_unit_to_str(ow_unit_t unit)
{
	switch (unit)
	{
	case OW_UNIT_MILLIVOLT:			return "Millivolt";
	case OW_UNIT_VOLT: 				return "Volt";

	case OW_UNIT_MICROAMPERE: 		return "Microampere";
	case OW_UNIT_MILLIAMPERE: 		return "Milliampere";
	case OW_UNIT_AMPERE: 			return "Ampere";

	case OW_UNIT_OHM: 				return "Ohm";
	case OW_UNIT_KILOOHM: 			return "Kiloohm";
	case OW_UNIT_MEGAOHM: 			return "Megaohm";

	case OW_UNIT_NANOFARAD: 		return "Nanofarad";
	case OW_UNIT_MICROFARAD: 		return "Microfarad";
	case OW_UNIT_MILLIFARAD: 		return "Millifarad";
	case OW_UNIT_FARAD: 			return "Farad";

	case OW_UNIT_HERTZ: 			return "Hertz";
	case OW_UNIT_PERCENT: 			return "Percent";

	case OW_UNIT_CELSIUS: 			return "Celsius";
	case OW_UNIT_FAHRENHEIT: 		return "Fahrenheit";

	case OW_UNIT_NEARFIELD: 		return "Near field";

	default: 						return "Unknown";
	}
}

const char* ow_unit_to_short_str(ow_unit_t unit)
{
	switch (unit)
	{
	case OW_UNIT_MILLIVOLT:			return "mV";
	case OW_UNIT_VOLT: 				return "V";

	case OW_UNIT_MICROAMPERE: 		return "µA";
	case OW_UNIT_MILLIAMPERE: 		return "mA";
	case OW_UNIT_AMPERE: 			return "A";

	case OW_UNIT_OHM: 				return "Ω";
	case OW_UNIT_KILOOHM: 			return "kΩ";
	case OW_UNIT_MEGAOHM: 			return "MΩ";

	case OW_UNIT_NANOFARAD: 		return "nF";
	case OW_UNIT_MICROFARAD: 		return "µF";
	case OW_UNIT_MILLIFARAD: 		return "mF";
	case OW_UNIT_FARAD: 			return "F";

	case OW_UNIT_HERTZ: 			return "Hz";
	case OW_UNIT_PERCENT: 			return "%";

	case OW_UNIT_CELSIUS: 			return "°C";
	case OW_UNIT_FAHRENHEIT: 		return "°F";

	case OW_UNIT_NEARFIELD: 		return "NCV";

	default: 						return "?";
	}
}

const char* ow_current_type_to_str(ow_current_type_t current_type)
{
	switch (current_type)
	{
	case OW_CURRENT_TYPE_DC: return "DC";
	case OW_CURRENT_TYPE_AC: return "AC";

	default: return "?";
	}
}

bool ow_recv(const ow_config_t* config, ow_sample_func_t callback, void* context)
{
	//Do we have to query the default adapter's device ID?
	int dev_id;

	if (config->dev_id == OW_DEV_ID_AUTOMATIC)
	{
		if (!ow_get_default_device_id(&dev_id))
		{
			return false;
		}
	}
	else
	{
		dev_id = config->dev_id;
	}

	//Open a socket:
	int bt_sock;

	if (!ow_open_socket(dev_id, &bt_sock))
	{
		return false;
	}

	//Query the old HCI filter to restore later:
	int error;
	struct hci_filter old_hci_filter;
	socklen_t old_hci_filter_length;

	if (!ow_get_hci_filter(bt_sock, &old_hci_filter, &old_hci_filter_length))
	{
		error = errno;
		goto close_out;
	}

	//Do we have to scan for the multimeter's address?
	bdaddr_t addr;

	switch (config->scan_mode)
	{
	case OW_SCAN_MODE_NONE:

		addr = config->addr;
		break;

	case OW_SCAN_MODE_AUTOMATIC:

		if (!ow_scan_for_address(bt_sock, &automatic_scan_params, &old_hci_filter, old_hci_filter_length, &addr))
		{
			error = errno;
			goto close_out;
		}

		break;

	case OW_SCAN_MODE_MANUAL:

		if (!ow_scan_for_address(bt_sock, &config->scan_params, &old_hci_filter, old_hci_filter_length, &addr))
		{
			error = errno;
			goto close_out;
		}

		break;

	default:

		error = EINVAL;
		goto close_out;
	}

	//Connect to the multimeter:
	uint16_t hci_handle;

	switch (config->connect_mode)
	{
	case OW_CONNECT_MODE_AUTOMATIC:

		if (!ow_connect(bt_sock, addr, &automatic_connect_params, &hci_handle))
		{
			error = errno;
			goto close_out;
		}

		break;

	case OW_CONNECT_MODE_MANUAL:

		if (!ow_connect(bt_sock, addr, &config->connect_params, &hci_handle))
		{
			error = errno;
			goto close_out;
		}

		break;

	default:

		error = EINVAL;
		goto close_out;
	}

	//Make sure we only see asynchronous data packets:
	struct hci_filter async_filter;

	hci_filter_clear(&async_filter);
	hci_filter_set_ptype(HCI_ACLDATA_PKT, &async_filter);

	if (!ow_set_hci_filter(bt_sock, &async_filter, sizeof(struct hci_filter)))
	{
		error = errno;
		goto disc_close_out;
	}

	//Receive until the user signals us to end:
	ow_sample_t sample;
	bool shall_continue = true;

	do
	{
		//Read a bunch of data from the device:
		uint8_t buf[HCI_MAX_EVENT_SIZE];
		int bytes_read = read(bt_sock, buf, sizeof(buf));

		//Error case?
		if (bytes_read < 0)
		{
			//Recoverable cases:
			if ((errno == EAGAIN) || (errno == EINTR))
			{
				continue;
			}

			//Fatal cases:
			error = errno;
			goto restore_disc_close_out;
		}

		//EoF case?
		if (bytes_read == 0)
		{
			error = ENODATA;
			goto restore_disc_close_out;
		}

		//Success case, but wrong number of bytes?
		if ((size_t)bytes_read != OW_SAMPLE_LENGTH)
		{
			continue;
		}

		//Validate the data.
		//HCI packet type:
		if (buf[0] != HCI_ACLDATA_PKT)
		{
			continue;
		}

		//Flagged HCI handle:
		if (((*(uint16_t*)&buf[1]) & 0x0FFF) != hci_handle)
		{
			continue;
		}

		//Total length:
		if (*(uint16_t*)&buf[3] != (OW_SAMPLE_LENGTH - 5))
		{
			continue;
		}

		//L2CAP length:
		if (*(uint16_t*)&buf[5] != (OW_SAMPLE_LENGTH - 9))
		{
			continue;
		}

		//L2CAP destination CID:
		if (*(uint16_t*)&buf[7] != OW_L2CAP_DEST_CID)
		{
			continue;
		}

		//ATT command:
		if (buf[9] != OW_ATT_OPCODE_HANDLE_VALUE_NOTIFICATION)
		{
			continue;
		}

		//Handle:
		if (*(uint16_t*)&buf[10] != OW_ATT_HANDLE)
		{
			continue;
		}

		//Parse 6 bytes of data (one is unused).

		//Get the actual value and the sign bit:
		uint16_t value_sign =  *(uint16_t*)&buf[16];

		//Get unit and places:
		uint16_t unit_places = *(uint16_t*)&buf[12];

		//Determine the unit and the current type:
		switch (unit_places & 0xFFF8)
		{
		case 0xF018: sample.unit = OW_UNIT_MILLIVOLT; sample.current_type = OW_CURRENT_TYPE_DC; break;
		case 0xF058: sample.unit = OW_UNIT_MILLIVOLT; sample.current_type = OW_CURRENT_TYPE_AC; break;
		case 0xF020: sample.unit = OW_UNIT_VOLT; sample.current_type = OW_CURRENT_TYPE_DC; sample.is_diode_test = false; break;
		case 0xF060: sample.unit = OW_UNIT_VOLT; sample.current_type = OW_CURRENT_TYPE_AC; sample.is_diode_test = false; break;
		case 0xF2A0: sample.unit = OW_UNIT_VOLT; sample.current_type = OW_CURRENT_TYPE_DC; sample.is_diode_test = true; break;
		case 0xF090: sample.unit = OW_UNIT_MICROAMPERE; sample.current_type = OW_CURRENT_TYPE_DC; break;
		case 0xF0D0: sample.unit = OW_UNIT_MICROAMPERE; sample.current_type = OW_CURRENT_TYPE_AC; break;
		case 0xF098: sample.unit = OW_UNIT_MILLIAMPERE; sample.current_type = OW_CURRENT_TYPE_DC; break;
		case 0xF0D8: sample.unit = OW_UNIT_MILLIAMPERE; sample.current_type = OW_CURRENT_TYPE_AC; break;
		case 0xF0A0: sample.unit = OW_UNIT_AMPERE; sample.current_type = OW_CURRENT_TYPE_DC; break;
		case 0xF0E0: sample.unit = OW_UNIT_AMPERE; sample.current_type = OW_CURRENT_TYPE_AC; break;
		case 0xF120: sample.unit = OW_UNIT_OHM; sample.is_continuity_test = false; break;
		case 0xF2E0: sample.unit = OW_UNIT_OHM; sample.is_continuity_test = true; break;
		case 0xF128: sample.unit = OW_UNIT_KILOOHM; break;
		case 0xF130: sample.unit = OW_UNIT_MEGAOHM; break;
		case 0xF148: sample.unit = OW_UNIT_NANOFARAD; break;
		case 0xF150: sample.unit = OW_UNIT_MICROFARAD; break;
		case 0xF158: sample.unit = OW_UNIT_MILLIFARAD; break;
		case 0xF160: sample.unit = OW_UNIT_FARAD; break;
		case 0xF1A0: sample.unit = OW_UNIT_HERTZ; break;
		case 0xF1E0: sample.unit = OW_UNIT_PERCENT; break;
		case 0xF220: sample.unit = OW_UNIT_CELSIUS; break;
		case 0xF260: sample.unit = OW_UNIT_FAHRENHEIT; break;
		case 0xF360: sample.unit = OW_UNIT_NEARFIELD; break;

		default: sample.unit = OW_UNIT_UNKNOWN;
		}

		//Use value, sign bit and decimal places to retrieve the final value:
		//First, test for an overflow.
		if (unit_places & (1 << 2))
		{
			sample.value = NAN;
		}
		else
		{
			//Determine the factor to generate the decimal places:
			double factor;

			switch (unit_places & 0x0003)
			{
			case 0: factor = 1; break;
			case 1: factor = 0.1; break;
			case 2: factor = 0.01; break;
			case 3: factor = 0.001; break;
			}

			//Build the number using the sign bit:
			sample.value = ((value_sign & 0x8000) ? -1.0 : 1.0) * factor * (double)(value_sign & 0x3FFF);
		}

		//Get the flag byte:
		uint8_t flags = buf[14];

		//Query the flags:
		sample.is_data_hold = (flags & (1 << 0)) != 0;
		sample.is_relative = (flags & (1 << 1)) != 0;
		sample.is_auto_range = (flags & (1 << 2)) != 0;
		sample.is_low_battery = (flags & (1 << 3)) != 0;

		//Pass the sample to the callback:
		shall_continue = callback(sample, context);
	} while (shall_continue);

	//Success case:
	error = 0;

restore_disc_close_out:
	//Restore the old HCI filter:
	ow_set_hci_filter(bt_sock, &old_hci_filter, old_hci_filter_length);

disc_close_out:
	//Disconnect:
	hci_disconnect(bt_sock, hci_handle, HCI_OE_USER_ENDED_CONNECTION, 10000);

close_out:
	//Close the socket:
	hci_close_dev(bt_sock);

	if (error == 0)
	{
		return true;
	}
	else
	{
		errno = error;
		return false;
	}
}

bool ow_recv_n(const ow_config_t* config, ow_sample_t* samples, int n)
{
	//Initialize the context for receiving:
	ow_recv_n_context_t context =
	{
		.samples = samples,
		.n = n,
		.count = 0
	};

	//Receive using our internal sample func and the context:
	return ow_recv(config, ow_recv_n_sample, &context);
}
