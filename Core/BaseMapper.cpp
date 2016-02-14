#include "stdafx.h"
#include "BaseMapper.h"
#include <assert.h>
#include "../Utilities/FolderUtilities.h"
#include "CheatManager.h"

uint16_t BaseMapper::InternalGetPrgPageSize()
{
	//Make sure the page size is no bigger than the size of the ROM itself
	//Otherwise we will end up reading from unallocated memory
	return std::min((uint32_t)GetPRGPageSize(), _prgSize);
}

uint16_t BaseMapper::InternalGetChrPageSize()
{
	//Make sure the page size is no bigger than the size of the ROM itself
	//Otherwise we will end up reading from unallocated memory
	return std::min((uint32_t)GetCHRPageSize(), _chrRomSize);
}
	
void BaseMapper::SetCpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, int16_t pageNumber, PrgMemoryType type, int8_t accessType)
{
#ifdef _DEBUG
	if((startAddr & 0xFF) || (endAddr & 0xFF) != 0xFF) {
		throw new std::runtime_error("Start/End address must be multiples of 256/0x100");
	}
#endif

	uint8_t* source = nullptr;
	uint32_t pageCount;
	uint32_t pageSize;
	uint8_t defaultAccessType = MemoryAccessType::Read;
	switch(type) {
		case PrgMemoryType::PrgRom:
			source = _prgRom;
			pageCount = GetPRGPageCount();
			pageSize = InternalGetPrgPageSize();
			break;
		case PrgMemoryType::SaveRam:
			source = _saveRam;
			pageCount = _saveRamSize / GetSaveRamPageSize();
			pageSize = GetSaveRamPageSize();
			defaultAccessType |= MemoryAccessType::Write;
			break;
		case PrgMemoryType::WorkRam:
			source = _workRam;
			pageCount = GetWorkRamSize() / GetWorkRamPageSize();
			pageSize = GetWorkRamPageSize();
			defaultAccessType |= MemoryAccessType::Write;
			break;
		default:
			throw new std::runtime_error("Invalid parameter");
	}

	if(pageNumber < 0) {
		//Can't use modulo for negative number because pageCount is sometimes not a power of 2.  (Fixes some Mapper 191 games)
		pageNumber = pageCount + pageNumber;
	} else {
		pageNumber = pageNumber % pageCount;
	}
	source = &source[pageNumber * pageSize];

	startAddr >>= 8;
	endAddr >>= 8;
	for(uint16_t i = startAddr; i <= endAddr; i++) {
		_prgPages[i] = source;
		_prgPageAccessType[i] = accessType != -1 ? accessType : defaultAccessType;

		source += 0x100;
	}
}

void BaseMapper::SetPpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, uint16_t pageNumber, ChrMemoryType type, int8_t accessType)
{
	uint32_t pageCount;
	uint32_t pageSize;
	uint8_t* sourceMemory = nullptr;
	uint8_t defaultAccessType = MemoryAccessType::Read;
	switch(type) {
		case ChrMemoryType::Default:
			pageCount = GetCHRPageCount();
			pageSize = InternalGetChrPageSize();
			sourceMemory = _onlyChrRam ? _chrRam : _chrRom;
			if(_onlyChrRam) {
				defaultAccessType |= MemoryAccessType::Write;
			}
			break;

		case ChrMemoryType::ChrRom:
			pageCount = GetCHRPageCount();
			pageSize = InternalGetChrPageSize();
			sourceMemory = _chrRom;
			break;

		case ChrMemoryType::ChrRam:
			pageSize = GetChrRamPageSize();
			pageCount = _chrRamSize / pageSize;
			sourceMemory = _chrRam;
			defaultAccessType |= MemoryAccessType::Write;
			break;
	}

	SetPpuMemoryMapping(startAddr, endAddr, sourceMemory + (pageNumber % pageCount) * pageSize, accessType == -1 ? defaultAccessType : accessType);
}

void BaseMapper::SetPpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, uint8_t* sourceMemory, int8_t accessType)
{
	#ifdef _DEBUG
	if((startAddr & 0xFF) || (endAddr & 0xFF) != 0xFF) {
		throw new std::runtime_error("Start/End address must be multiples of 256/0x100");
	}
	#endif

	startAddr >>= 8;
	endAddr >>= 8;
	for(uint16_t i = startAddr; i <= endAddr; i++) {
		_chrPages[i] = sourceMemory;
		_chrPageAccessType[i] = accessType != -1 ? accessType : MemoryAccessType::ReadWrite;

		if(sourceMemory != nullptr) {
			sourceMemory += 0x100;
		}
	}
}

void BaseMapper::RemovePpuMemoryMapping(uint16_t startAddr, uint16_t endAddr)
{
	//Unmap this section of memory (causing open bus behavior)
	SetPpuMemoryMapping(startAddr, endAddr, nullptr, MemoryAccessType::NoAccess);
}

uint8_t BaseMapper::InternalReadRam(uint16_t addr)
{
	return _prgPages[addr >> 8] ? _prgPages[addr >> 8][addr & 0xFF] : 0;
}

void BaseMapper::SelectPrgPage4x(uint16_t slot, uint16_t page, PrgMemoryType memoryType)
{
	SelectPrgPage2x(slot*2, page, memoryType);
	SelectPrgPage2x(slot*2+1, page+2, memoryType);
}

void BaseMapper::SelectPrgPage2x(uint16_t slot, uint16_t page, PrgMemoryType memoryType)
{
	SelectPRGPage(slot*2, page, memoryType);
	SelectPRGPage(slot*2+1, page+1, memoryType);
}

void BaseMapper::SelectPRGPage(uint16_t slot, uint16_t page, PrgMemoryType memoryType)
{
	_prgPageNumbers[slot] = page;

	if(_prgSize < PrgAddressRangeSize) {
		//Total PRG size is smaller than available memory range, map the entire PRG to all slots
		//i.e same logic as NROM (mapper 0) when PRG is 16kb
		//Needed by "Pyramid" (mapper 79)
		#ifdef _DEBUG
			MessageManager::DisplayMessage("Debug", "PRG size is smaller than 32kb");
		#endif

		for(slot = 0; slot < PrgAddressRangeSize / _prgSize; slot++) {
			uint16_t startAddr = 0x8000 + slot * _prgSize;
			uint16_t endAddr = startAddr + _prgSize - 1;
			SetCpuMemoryMapping(startAddr, endAddr, 0, memoryType);
		}
	} else {
		uint16_t startAddr = 0x8000 + slot * InternalGetPrgPageSize();
		uint16_t endAddr = startAddr + InternalGetPrgPageSize() - 1;
		SetCpuMemoryMapping(startAddr, endAddr, page, memoryType);
	}
}

void BaseMapper::SelectChrPage8x(uint16_t slot, uint16_t page, ChrMemoryType memoryType)
{
	SelectChrPage4x(slot, page, memoryType);
	SelectChrPage4x(slot*2+1, page+4, memoryType);
}

void BaseMapper::SelectChrPage4x(uint16_t slot, uint16_t page, ChrMemoryType memoryType)
{
	SelectChrPage2x(slot*2, page, memoryType);
	SelectChrPage2x(slot*2+1, page+2, memoryType);
}

void BaseMapper::SelectChrPage2x(uint16_t slot, uint16_t page, ChrMemoryType memoryType)
{
	SelectCHRPage(slot*2, page, memoryType);
	SelectCHRPage(slot*2+1, page+1, memoryType);
}

void BaseMapper::SelectCHRPage(uint16_t slot, uint16_t page, ChrMemoryType memoryType)
{
	_chrPageNumbers[slot] = page;

	uint16_t startAddr = slot * InternalGetChrPageSize();
	uint16_t endAddr = startAddr + InternalGetChrPageSize() - 1;
	SetPpuMemoryMapping(startAddr, endAddr, page, memoryType);
}
		
bool BaseMapper::HasBattery()
{
	return _hasBattery;
}

void BaseMapper::LoadBattery()
{
	ifstream batteryFile(_batteryFilename, ios::in | ios::binary);

	if(batteryFile) {
		batteryFile.read((char*)_saveRam, _saveRamSize);

		batteryFile.close();
	}

	//Set a default mapping for save ram (this is what most games/mappers use)
	SetCpuMemoryMapping(0x6000, 0x7FFF, 0, PrgMemoryType::SaveRam);
}

void BaseMapper::SaveBattery()
{
	ofstream batteryFile(_batteryFilename, ios::out | ios::binary);

	if(batteryFile) {
		batteryFile.write((char*)_saveRam, _saveRamSize);

		batteryFile.close();
	}
}

uint32_t BaseMapper::GetPRGPageCount()
{
	return _prgSize / InternalGetPrgPageSize();
}

uint32_t BaseMapper::GetCHRPageCount()
{
	return _chrRomSize / InternalGetChrPageSize();
}

string BaseMapper::GetBatteryFilename()
{
	return FolderUtilities::GetSaveFolder() + FolderUtilities::GetFilename(_romFilename, false) + ".sav";
}
		
void BaseMapper::RestoreOriginalPrgRam()
{
	memcpy(_prgRom, _originalPrgRom.data(), _originalPrgRom.size());
}

void BaseMapper::InitializeChrRam()
{
	_chrRamSize = GetChrRamSize();
	if(_chrRamSize > 0) {
		_chrRam = new uint8_t[_chrRamSize];
		memset(_chrRam, 0, _chrRamSize);
	}
}

void BaseMapper::AddRegisterRange(uint16_t startAddr, uint16_t endAddr)
{
	for(int i = startAddr; i <= endAddr; i++) {
		_isRegisterAddr[i] = true;
	}
}

void BaseMapper::RemoveRegisterRange(uint16_t startAddr, uint16_t endAddr)
{
	for(int i = startAddr; i <= endAddr; i++) {
		_isRegisterAddr[i] = false;
	}
}

void BaseMapper::StreamState(bool saving)
{
	StreamArray<uint8_t>(_chrRam, _chrRamSize);

	Stream<MirroringType>(_mirroringType);

	StreamArray<uint8_t>(_workRam, GetWorkRamSize());
	StreamArray<uint8_t>(_saveRam, _saveRamSize);

	StreamArray<uint32_t>(_prgPageNumbers, 64);
	StreamArray<uint32_t>(_chrPageNumbers, 64);

	StreamArray<uint8_t>(_nametableIndexes, 4);
	if(!saving) {
		for(uint16_t i = 0; i < 64; i++) {
			if(_prgPageNumbers[i] != 0xEEEEEEEE) {
				SelectPRGPage(i, (uint16_t)_prgPageNumbers[i]);
			}
		}

		for(uint16_t i = 0; i < 64; i++) {
			if(_chrPageNumbers[i] != 0xEEEEEEEE) {
				SelectCHRPage(i, (uint16_t)_chrPageNumbers[i]);
			}
		}

		for(int i = 0; i < 4; i++) {
			SetNametable(i, _nametableIndexes[i]);
		}
	}
}

void BaseMapper::Initialize(RomData &romData)
{
	_romFilename = romData.Filename;
	_batteryFilename = GetBatteryFilename();
	_saveRamSize = GetSaveRamSize(); //Needed because we need to call SaveBattery() in the destructor (and calling virtual functions in the destructor doesn't work correctly)

	_allowRegisterRead = AllowRegisterRead();

	memset(_isRegisterAddr, 0, sizeof(_isRegisterAddr));
	AddRegisterRange(RegisterStartAddress(), RegisterEndAddress());

	_mirroringType = romData.MirroringType;

	_prgSize = (uint32_t)romData.PrgRom.size();
	_chrRomSize = (uint32_t)romData.ChrRom.size();
	_originalPrgRom = romData.PrgRom;

	_prgRom = new uint8_t[_prgSize];
	_chrRom = new uint8_t[_chrRomSize];
	memcpy(_prgRom, romData.PrgRom.data(), _prgSize);
	if(_chrRomSize > 0) {
		memcpy(_chrRom, romData.ChrRom.data(), _chrRomSize);
	}

	_hasBattery = romData.HasBattery || ForceBattery();
	_isPalRom = romData.IsPalRom;
	_crc32 = romData.Crc32;
	_hasBusConflicts = HasBusConflicts();

	_saveRam = new uint8_t[_saveRamSize];
	_workRam = new uint8_t[GetWorkRamSize()];

	memset(_saveRam, 0, _saveRamSize);
	memset(_workRam, 0, GetWorkRamSize());

	memset(_prgPageNumbers, 0xEE, sizeof(_prgPageNumbers));
	memset(_chrPageNumbers, 0xEE, sizeof(_chrPageNumbers));

	memset(_cartNametableRam, 0, sizeof(_cartNametableRam));
	memset(_nametableIndexes, 0, sizeof(_nametableIndexes));

	for(int i = 0; i <= 0xFF; i++) {
		//Allow us to map a different page every 256 bytes
		_prgPages.push_back(nullptr);
		_prgPageAccessType.push_back(MemoryAccessType::NoAccess);
		_chrPages.push_back(nullptr);
		_chrPageAccessType.push_back(MemoryAccessType::NoAccess);
	}

	//Load battery data if present
	if(HasBattery()) {
		LoadBattery();
	}

	if(_chrRomSize == 0) {
		//Assume there is CHR RAM if no CHR ROM exists
		_onlyChrRam = true;
		InitializeChrRam();
		_chrRomSize = _chrRamSize;
	}

	//Setup a default work/save ram in 0x6000-0x7FFF space
	SetCpuMemoryMapping(0x6000, 0x7FFF, 0, HasBattery() ? PrgMemoryType::SaveRam : PrgMemoryType::WorkRam);

	InitMapper();
	InitMapper(romData);

	MessageManager::RegisterNotificationListener(this);

	ApplyCheats();
}

BaseMapper::~BaseMapper()
{
	if(HasBattery()) {
		SaveBattery();
	}
	delete[] _chrRam;
	delete[] _chrRom;
	delete[] _prgRom;
	delete[] _saveRam;
	delete[] _workRam;

	if(_cartNametableRam[0]) {
		delete[] _cartNametableRam[0];
	}

	if(_cartNametableRam[1]) {
		delete[] _cartNametableRam[1];
	}

	MessageManager::UnregisterNotificationListener(this);
}

void BaseMapper::ProcessNotification(ConsoleNotificationType type, void* parameter)
{
	switch(type) {
		case ConsoleNotificationType::CheatAdded:
		case ConsoleNotificationType::CheatRemoved:
			ApplyCheats();
			break;
		default:
			break;
	}
}


void BaseMapper::ApplyCheats()
{
	RestoreOriginalPrgRam();
	CheatManager::ApplyPrgCodes(_prgRom, GetPrgSize());
}

void BaseMapper::GetMemoryRanges(MemoryRanges &ranges)
{
	ranges.AddHandler(MemoryOperation::Read, 0x4018, 0xFFFF);
	ranges.AddHandler(MemoryOperation::Write, 0x4018, 0xFFFF);
}

void BaseMapper::SetDefaultNametables(uint8_t* nametableA, uint8_t* nametableB)
{
	_nesNametableRam[0] = nametableA;
	_nesNametableRam[1] = nametableB;
	SetMirroringType(_mirroringType);
}

void BaseMapper::AddNametable(uint8_t index, uint8_t *nametable)
{
	assert(index >= 4);
	_cartNametableRam[index - 2] = nametable;
}

uint8_t* BaseMapper::GetNametable(uint8_t index)
{
	if(index <= 1) {
		return _nesNametableRam[index];
	} else {
		return _cartNametableRam[index - 2];
	}
}

void BaseMapper::SetNametable(uint8_t index, uint8_t nametableIndex)
{
	if(nametableIndex == 2 && _cartNametableRam[0] == nullptr) {
		_cartNametableRam[0] = new uint8_t[0x400];
	}
	if(nametableIndex == 3 && _cartNametableRam[1] == nullptr) {
		_cartNametableRam[1] = new uint8_t[0x400];
	}

	_nametableIndexes[index] = nametableIndex;

	SetPpuMemoryMapping(0x2000 + index * 0x400, 0x2000 + (index + 1) * 0x400 - 1, GetNametable(nametableIndex));
}

void BaseMapper::SetNametables(uint8_t nametable1Index, uint8_t nametable2Index, uint8_t nametable3Index, uint8_t nametable4Index)
{
	SetNametable(0, nametable1Index);
	SetNametable(1, nametable2Index);
	SetNametable(2, nametable3Index);
	SetNametable(3, nametable4Index);
}

void BaseMapper::SetMirroringType(MirroringType type)
{
	_mirroringType = type;
	switch(type) {
		case MirroringType::Vertical: SetNametables(0, 1, 0, 1); break;
		case MirroringType::Horizontal: SetNametables(0, 0, 1, 1); break;
		case MirroringType::FourScreens:	SetNametables(0, 1, 2, 3); break;
		case MirroringType::ScreenAOnly: SetNametables(0, 0, 0, 0);	break;
		case MirroringType::ScreenBOnly: SetNametables(1, 1, 1, 1);	break;
	}
}

bool BaseMapper::IsPalRom()
{
	return _isPalRom;
}

uint32_t BaseMapper::GetCrc32()
{
	return _crc32;
}

MirroringType BaseMapper::GetMirroringType()
{
	return _mirroringType;
}
	
uint8_t BaseMapper::ReadRAM(uint16_t addr)
{
	if(_allowRegisterRead && _isRegisterAddr[addr]) {
		return ReadRegister(addr);
	} else if(_prgPageAccessType[addr >> 8] & MemoryAccessType::Read) {
		return _prgPages[addr >> 8][addr & 0xFF];
	} else {
		//assert(false);
	}
	return (addr & 0xFF00) >> 8;
}

void BaseMapper::WriteRAM(uint16_t addr, uint8_t value)
{
	if(_isRegisterAddr[addr]) {
		if(_hasBusConflicts) {
			value &= _prgPages[addr >> 8][addr & 0xFF];
		}
		WriteRegister(addr, value);
	} else {
		WritePrgRam(addr, value);
	}
}

void BaseMapper::WritePrgRam(uint16_t addr, uint8_t value)
{
	if(_prgPageAccessType[addr >> 8] & MemoryAccessType::Write) {
		_prgPages[addr >> 8][addr & 0xFF] = value;
	}
}
		
uint8_t BaseMapper::ReadVRAM(uint16_t addr)
{
	if(_chrPageAccessType[addr >> 8] & MemoryAccessType::Read) {
		return _chrPages[addr >> 8][addr & 0xFF];
	} else {
		//assert(false);
	}
	return 0;
}

void BaseMapper::WriteVRAM(uint16_t addr, uint8_t value)
{
	if(_chrPageAccessType[addr >> 8] & MemoryAccessType::Write) {
		_chrPages[addr >> 8][addr & 0xFF] = value;
	} else {
		//assert(false);
	}
}

void BaseMapper::NotifyVRAMAddressChange(uint16_t addr)
{
	//This is called when the VRAM addr on the PPU memory bus changes
	//Used by MMC3/MMC5/etc
}

//Debugger Helper Functions
uint8_t* BaseMapper::GetPrgRom()
{
	return _prgRom;
}

uint8_t* BaseMapper::GetWorkRam()
{
	return _workRam;
}

void BaseMapper::GetPrgCopy(uint8_t **buffer)
{
	*buffer = new uint8_t[_prgSize];
	memcpy(*buffer, _prgRom, _prgSize);
}

uint32_t BaseMapper::GetPrgSize(bool getWorkRamSize)
{
	return getWorkRamSize ? GetWorkRamSize() : _prgSize;
}

void BaseMapper::GetChrRomCopy(uint8_t **buffer)
{
	*buffer = new uint8_t[_chrRomSize];
	memcpy(*buffer, _chrRom, _chrRomSize);
}

uint32_t BaseMapper::GetChrSize(bool getRamSize)
{
	return getRamSize ? _chrRamSize : _chrRomSize;
}

void BaseMapper::GetChrRamCopy(uint8_t **buffer)
{
	*buffer = new uint8_t[_chrRamSize];
	memcpy(*buffer, _chrRam, _chrRamSize);
}

int32_t BaseMapper::ToAbsoluteAddress(uint16_t addr)
{
	uint8_t *prgAddr = _prgPages[addr >> 8] + (addr & 0xFF);
	if(prgAddr >= _prgRom && prgAddr < _prgRom + _prgSize) {
		return (uint32_t)(prgAddr - _prgRom);
	}
	return -1;
}

int32_t BaseMapper::ToAbsoluteRamAddress(uint16_t addr)
{
	uint8_t *prgRamAddr = _prgPages[addr >> 8] + (addr & 0xFF);
	if(prgRamAddr >= _workRam && prgRamAddr < _workRam + GetWorkRamSize()) {
		return (uint32_t)(prgRamAddr - _workRam);
	}
	return -1;
}

int32_t BaseMapper::ToAbsoluteChrAddress(uint16_t addr)
{
	uint8_t *chrAddr = _chrPages[addr >> 8] + (addr & 0xFF);
	if(chrAddr >= _chrRom && chrAddr < _chrRom + _chrRomSize) {
		return (uint32_t)(chrAddr - _chrRom);
	}
	return -1;
}

int32_t BaseMapper::FromAbsoluteAddress(uint32_t addr)
{
	uint8_t* ptrAddress = _prgRom + addr;

	for(int i = 0; i < 256; i++) {
		uint8_t* pageAddress = _prgPages[i];
		if(pageAddress != nullptr && ptrAddress >= pageAddress && ptrAddress <= pageAddress + 0xFF) {
			return (i << 8) + (uint32_t)(ptrAddress - pageAddress);
		}
	}
			
	//Address is currently not mapped
	return -1;
}