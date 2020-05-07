#include <io.h>
#include <string>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <winsqlite/winsqlite3.h>
#pragma comment(lib,"winsqlite3.lib")

#include <Windows.Devices.h>
#include <Windows.Devices.Bluetooth.h>
#include <Windows.Devices.Bluetooth.Advertisement.h>

#pragma region Namespaces

using namespace std;
using namespace Platform;
using namespace Windows::Devices;
using namespace Windows::Foundation;
using namespace Bluetooth;
using namespace Bluetooth::Advertisement;

#pragma endregion

#pragma region Types

using Watcher = BluetoothLEAdvertisementWatcher;
using EventArgs = BluetoothLEAdvertisementReceivedEventArgs;
using BluetoothLEEventHandler = TypedEventHandler<Watcher^, EventArgs^>;

#pragma pack(push,1)
struct WeightData
{
	uint8_t status;
	uint16_t weight;
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	bool operator ==(const WeightData& rhs)
	{
		return this->status == rhs.status &&
			this->weight == rhs.weight &&
			this->year == rhs.year &&
			this->month == rhs.month &&
			this->day == rhs.day &&
			this->hour == rhs.hour &&
			this->minute == rhs.minute &&
			this->second == rhs.second;
	}
};
#pragma pack(pop)

#pragma endregion

#pragma region Macros

// access() check if file has read privliges
#define R_OK 0x02

#ifdef _MSVC_LANG
#define access _access
#endif

#define STATUS_UNIT_LB 0x01
#define STATUS_UNIT_JIN 0x08
#define STATUS_STABLIZED 0x20
#define STATUS_WEIGHT_REMOVED 0x80

#pragma endregion

#pragma region Utilities

string trim(const string s)
{
	auto fs = s.find_first_not_of(' ');
	auto fe = s.find_last_not_of(' ') + 1;
	return s.substr(fs, fe - fs);
}

int hex2int(char hex)
{
	switch (hex)
	{
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'A': return 10;
	case 'B': return 11;
	case 'C': return 12;
	case 'D': return 13;
	case 'E': return 14;
	case 'F': return 15;
	case 'a': return 10;
	case 'b': return 11;
	case 'c': return 12;
	case 'd': return 13;
	case 'e': return 14;
	case 'f': return 15;
	default:
		cout << "illegal address char" << endl;
		throw exception();
	}
}

uint64_t ReadBindedDeviceAddr()
{
	if (access("config.ini", R_OK) != 0)
		return 0;
	auto is = ifstream("config.ini");
	int linec = 0;
	while (!is.eof())
	{
		string line;
		getline(is, line);
		linec++;
		bool commented = false;
		for (auto& ch : line)
		{
			if (ch == '#')
				commented = true;
			if (commented)
				ch = '\0';
		}
		size_t split = -1;
		if ((split = line.find('=')) != -1)
		{
			auto name = trim(line.substr(0, split));
			auto addr = trim(line.substr(split + 1));

			cout << "Device address was set to " << addr << endl;

			if (name != "DeviceAddress")
				continue;
			while (addr.find(':') != -1)
				addr = addr.replace(addr.find(':'), 1, 0, ' ');

			uint64_t iaddr = 0;
			for (size_t i = 0; i < 12; i++)
			{
				iaddr <<= 4;
				iaddr |= hex2int(addr[i]);
			}
			return iaddr;
		}
		else
		{
			cout << "ini synatax error found in line " << linec << endl;
		}
	}
	return 0;
}

vector<uint8_t> ReadBuffer(Windows::Storage::Streams::IBuffer^ buf)
{
	auto size = buf->Length;
	auto reader = Windows::Storage::Streams::DataReader::FromBuffer(buf);
	std::vector<uint8_t> res;
	res.reserve(size);
	while (size--)
		res.push_back(reader->ReadByte());
	return res;
}

auto GetDayByMonth(int m)
{
	switch (m)
	{
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		return 31;
	case 2:
		return 28;
	default:
		return 30;
	}
}

int64_t UnixTimeFromWeightData(const WeightData& data)
{
	// 1970/1/1 0:0:0
	// MI SCALE 2 Mainly used in China, So GMT + 8
	int64_t totalDays = data.day - 1;
	auto year = data.year - 1970;
	totalDays += year * 365;
	totalDays += (year + 2) / 4; // Feb with 29 days
	for (auto i = 1; i <= (data.month - 1); i++)
		totalDays += GetDayByMonth(i);
	auto totalSeconds = totalDays * 24 * 3600;
	totalSeconds += (data.hour * 60 + data.minute) * 60 + data.second;
	return totalSeconds;
}

#pragma endregion

#pragma region Constants
// 0x181d - Weight Scale
auto ServiceID = 0x181d;
// 0x0157 - Anhui Huami Information Technology Co., Ltd.
auto CompanyID = 0x0157;
/*const */auto serviceUUID = BluetoothUuidHelper::FromShortId(0x181d);

// cannot set to constant due to stupid UWP API
// I HATE UWP!

const auto KGPerLB = 0.45359237;
const auto doNothing = [](void*, int, char**, char**) {return 0; };

#pragma endregion

void StoreWeightData(WeightData& data)
{
	static WeightData LastWeightData;
	if (data == LastWeightData)
		return;
	else
		LastWeightData = data;
	double weight = 0;
	if (data.status & STATUS_UNIT_LB)
		weight = data.weight / 100.0 * KGPerLB;
	else
		weight = data.weight / 200.0;

	printf("[%02d-%02d-%02d %02d:%02d:%02d] ", data.year, data.month, data.day, data.hour, data.minute, data.second);

	printf("%.2f KG", weight);

	if (!(data.status & STATUS_STABLIZED))
	{
		putchar('\n');
		return;
	}

	printf(" stablized ");

	sqlite3* db = nullptr;
	if (sqlite3_open("weights.db", &db) != 0)
	{
		cerr << "Error when open weights.db, " << sqlite3_errmsg(db);
		return;
	}
	const auto sql = "SELECT id FROM weights WHERE (" + std::to_string(UnixTimeFromWeightData(data)) + " - data_time < 30);";
	int64_t mergeID = -1;

	char* errmsg = nullptr;

	int matchCount = 0;
	auto err = sqlite3_exec(db, sql.data(),
		[](void* pmid, int argc, char** argv, char** azColName)
		{
			auto& mergeID = *reinterpret_cast<int64_t*>(pmid);
			mergeID = std::atoll(argv[0]);
			return 0;
		}, &mergeID, &errmsg);
	if (err)
		cerr << errmsg;

	if (mergeID == -1)
	{
		printf("saved!\n");

		const auto sql = "INSERT INTO weights(weight,data_time) VALUES(" +
			std::to_string((int)(weight * 200)) + "," +
			std::to_string(UnixTimeFromWeightData(data)) + ");";
		err = sqlite3_exec(db, sql.data(), doNothing, nullptr, &errmsg);
		if (err)
			cerr << errmsg;
	}
	else
	{
		printf("updated!\n");
		err = sqlite3_exec(db,
			("UPDATE weights SET weight = " + to_string((int)(weight * 200)) + ",id = " + to_string(mergeID) + " WHERE id = " + to_string(mergeID)).data(),
			doNothing, nullptr, &errmsg);
		if (err)
			cerr << errmsg;
	}

	sqlite3_close(db);
}

// Where the magic happends.
int main(Array<String^>^ args)
{
	// begin with Initialization of Bluetooth and DeviceAddress.
#pragma region Initialization

	Microsoft::WRL::Wrappers::RoInitializeWrapper initialize(RO_INIT_MULTITHREADED);
	CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IDENTIFY, NULL, EOAC_NONE, nullptr);
	auto DevideAddr = ReadBindedDeviceAddr();

#pragma endregion

#pragma region Scanning
	auto watcher = ref new Watcher();
	watcher->ScanningMode = BluetoothLEScanningMode::Active;

	watcher->Received += ref new BluetoothLEEventHandler([&](Watcher^ watcher, EventArgs^ args)
		{
			auto devaddr = args->BluetoothAddress;
			// Skip if not the device we want.
			if (devaddr != DevideAddr)
				return;

			// Skip if not found the "Weight Scale" Services.
			auto serviceUuids = args->Advertisement->ServiceUuids;
			if (serviceUuids->Size == 0)
				return;
			if (serviceUuids->GetAt(0) != serviceUUID)
				return;

			auto sections = args->Advertisement->DataSections;

			vector<uint8_t> buffer;
			for (int i = 0; i < sections->Size; i++)
			{
				auto buf = ReadBuffer(sections->GetAt(i)->Data);
				buffer.insert(buffer.end(), buf.begin(), buf.end());
			}

			// Verify the data we recived.
			if (buffer.size() != 23)
				return;
			if (buffer[0] != 6)
				return;
			if (*reinterpret_cast<uint16_t*>(buffer.data() + 1) != ServiceID)
				return;
			if (*reinterpret_cast<uint16_t*>(buffer.data() + 11) != ServiceID)
				return;
			if (*reinterpret_cast<uint16_t*>(buffer.data() + 3) != CompanyID)
				return;

			auto& weightData = *reinterpret_cast<WeightData*>(buffer.data() + 13);

			StoreWeightData(weightData);
		});
#pragma endregion

	watcher->Start();

	int a;
	cin >> a;
}