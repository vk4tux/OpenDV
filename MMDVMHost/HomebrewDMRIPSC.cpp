/*
 *   Copyright (C) 2015 by Jonathan Naylor G4KLX
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "HomebrewDMRIPSC.h"
#include "StopWatch.h"
#include "SHA256.h"
#include "Utils.h"
#include "Log.h"

#include <cassert>

const unsigned int BUFFER_LENGTH = 500U;

const unsigned int HOMEBREW_DATA_PACKET_LENGTH = 53U;


CHomebrewDMRIPSC::CHomebrewDMRIPSC(const std::string& address, unsigned int port, unsigned int id, const std::string& password, const char* version) :
m_address(),
m_port(port),
m_id(NULL),
m_password(password),
m_version(version),
m_socket(),
m_status(DISCONNECTED),
m_retryTimer(1000U, 10U),
m_timeoutTimer(1000U, 600U),
m_pingTimer(1000U, 5U),
m_buffer(NULL),
m_salt(NULL),
m_streamId(NULL),
m_seqNo(NULL),
m_rxData(1000U),
m_callsign(),
m_rxFrequency(0U),
m_txFrequency(0U),
m_power(0U),
m_colorCode(0U),
m_latitude(0.0F),
m_longitude(0.0F),
m_height(0),
m_location(),
m_description(),
m_url()
{
	assert(!address.empty());
	assert(port > 0U);
	assert(id > 1000U);
	assert(!password.empty());

	m_address = CUDPSocket::lookup(address);

	m_buffer   = new unsigned char[BUFFER_LENGTH];
	m_salt     = new unsigned char[sizeof(uint32_t)];
	m_id       = new uint8_t[4U];
	m_streamId = new uint32_t[2U];
	m_seqNo    = new uint8_t[2U];

	m_streamId[0U] = 0x00U;
	m_streamId[1U] = 0x00U;

	m_seqNo[0U] = 0U;
	m_seqNo[1U] = 0U;

	m_id[0U] = id >> 24;
	m_id[1U] = id >> 16;
	m_id[2U] = id >> 8;
	m_id[3U] = id >> 0;

	CStopWatch stopWatch;
	::srand(stopWatch.start());
}

CHomebrewDMRIPSC::~CHomebrewDMRIPSC()
{
	delete[] m_buffer;
	delete[] m_salt;
	delete[] m_streamId;
	delete[] m_seqNo;
	delete[] m_id;
}

void CHomebrewDMRIPSC::setConfig(const std::string& callsign, unsigned int rxFrequency, unsigned int txFrequency, unsigned int power, unsigned int colorCode, float latitude, float longitude, int height, const std::string& location, const std::string& description, const std::string& url)
{
	m_callsign    = callsign;
	m_rxFrequency = rxFrequency;
	m_txFrequency = txFrequency;
	m_power       = power;
	m_colorCode   = colorCode;
	m_latitude    = latitude;
	m_longitude   = longitude;
	m_height      = height;
	m_location    = location;
	m_description = description;
	m_url         = url;
}

bool CHomebrewDMRIPSC::open()
{
	LogMessage("Opening DMR IPSC");

	bool ret = m_socket.open();
	if (!ret)
		return false;

	ret = writeLogin();
	if (!ret) {
		m_socket.close();
		return false;
	}

	m_status = WAITING_LOGIN;
	m_timeoutTimer.start();
	m_retryTimer.start();

	return true;
}

bool CHomebrewDMRIPSC::read(CDMRData& data)
{
	if (m_status != RUNNING)
		return false;

	if (m_rxData.isEmpty())
		return false;

	unsigned char length = 0U;

	m_rxData.getData(&length, 1U);
	m_rxData.getData(m_buffer, length);

	// Is this a data packet?
	if (::memcmp(m_buffer, "DMRD", 4U) != 0)
		return false;

	unsigned int srcId = (m_buffer[5U] << 16) | (m_buffer[6U] << 8) | (m_buffer[7U] << 0);

	unsigned int dstId = (m_buffer[8U] << 16) | (m_buffer[9U] << 8) | (m_buffer[10U] << 0);

	unsigned int slotNo = (m_buffer[15] & 0x80U) == 0x80U ? 2U : 1U;

	FLCO flco = (m_buffer[15U] & 0x40U) == 0x40U ? FLCO_USER_USER : FLCO_GROUP;

	data.setSlotNo(slotNo);
	data.setSrcId(srcId);
	data.setDstId(dstId);
	data.setFLCO(flco);

	unsigned char slotType = m_buffer[15U] & 0x3FU;

	if (slotType == 0x26U) {				// Data header
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_DATA_HEADER);
	} else if (slotType == 0x23U) {			// CSBK
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_CSBK);
	} else if (slotType == 0x28U) {			// Data 3/4 rate
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_RATE34_DATA);
	} else if (slotType == 0x21U) {			// Voice Header
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE_HEADER);
	} else if (slotType == 0x22U) {			// Terminator
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_TERMINATOR);
	} else if (slotType == 0x10U) {			// Voice Sync
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE_SYNC);
		data.setN(0U);
	} else if (slotType == 0x01U) {			// Voice
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE);
		data.setN(1U);
	} else if (slotType == 0x02U) {			// Voice
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE);
		data.setN(2U);
	} else if (slotType == 0x03U) {			// Voice
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE);
		data.setN(3U);
	} else if (slotType == 0x04U) {			// Voice
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE);
		data.setN(4U);
	} else if (slotType == 0x05U) {			// Voice
		data.setData(m_buffer + 20U);
		data.setDataType(DMRDT_VOICE);
		data.setN(5U);
	} else {
		::LogMessage("SlotType = 0x%02X", slotType);
		CUtils::dump("????", m_buffer, length);
		return false;
	}

	return true;
}

bool CHomebrewDMRIPSC::write(const CDMRData& data)
{
	if (m_status != RUNNING)
		return false;

	unsigned char buffer[HOMEBREW_DATA_PACKET_LENGTH];
	::memset(buffer, 0x00U, HOMEBREW_DATA_PACKET_LENGTH);

	buffer[0U]  = 'D';
	buffer[1U]  = 'M';
	buffer[2U]  = 'R';
	buffer[3U]  = 'D';

	unsigned int srcId = data.getSrcId();
	buffer[5U]  = srcId >> 16;
	buffer[6U]  = srcId >> 8;
	buffer[7U]  = srcId >> 0;

	unsigned int dstId = data.getDstId();
	buffer[8U]  = dstId >> 16;
	buffer[9U]  = dstId >> 8;
	buffer[10U] = dstId >> 0;

	::memcpy(buffer + 11U, m_id, 4U);

	unsigned int slotNo = data.getSlotNo();
	buffer[15U] = slotNo == 1U ? 0x00U : 0x80U;

	FLCO flco = data.getFLCO();
	buffer[15U] |= flco == FLCO_GROUP ? 0x00U : 0x40U;

	unsigned int slotIndex = slotNo - 1U;

	DMR_DATA_TYPE dataType = data.getDataType();
	if (dataType == DMRDT_VOICE_HEADER) {
		m_streamId[slotIndex] = ::rand() + 1U;
		m_seqNo[slotIndex]    = 0U;
		buffer[15U] |= 0x21U;
	} else if (dataType == DMRDT_TERMINATOR) {
		buffer[15U] |= 0x22U;
	} else if (dataType == DMRDT_CSBK) {
		buffer[15U] |= 0x23U;
	} else if (dataType == DMRDT_DATA_HEADER) {
		m_streamId[slotIndex] = ::rand() + 1U;
		m_seqNo[slotIndex]    = 0U;
		buffer[15U] |= 0x26U;
	} else if (dataType == DMRDT_RATE34_DATA) {
		buffer[15U] |= 0x28U;
	} else if (dataType == DMRDT_VOICE_SYNC) {
		buffer[15U] |= 0x10U;
	} else if (dataType == DMRDT_VOICE) {
		buffer[15U] |= 0x00U;
		unsigned int n = data.getN();
		buffer[15U] |= n;
	} else {
		::LogError("Unknown DMR data type");
		return false;
	}

	buffer[4U]  = m_seqNo[slotIndex];
	m_seqNo[slotIndex]++;

	::memcpy(buffer + 16U, m_streamId + slotIndex, 4U);

	data.getData(buffer + 20U);

	CUtils::dump(1U, "Homebrew out", buffer, HOMEBREW_DATA_PACKET_LENGTH);

	return m_socket.write(buffer, HOMEBREW_DATA_PACKET_LENGTH, m_address, m_port);
}

void CHomebrewDMRIPSC::close()
{
	LogMessage("Closing DMR IPSC");

	unsigned char buffer[9U];
	::memcpy(buffer + 0U, "RPTCL", 5U);
	::memcpy(buffer + 5U, m_id, 4U);
	m_socket.write(buffer, 9U, m_address, m_port);

	m_socket.close();
}

void CHomebrewDMRIPSC::clock(unsigned int ms)
{
	in_addr address;
	unsigned int port;
	unsigned int length = m_socket.read(m_buffer, BUFFER_LENGTH, address, port);

	if (length > 0U && m_address.s_addr == address.s_addr && m_port == port) {
		if (::memcmp(m_buffer, "DMRD", 4U) == 0) {
			unsigned char len = length;
			m_rxData.addData(&len, 1U);
			m_rxData.addData(m_buffer, len);
		} else if (::memcmp(m_buffer, "MSTNAK",  6U) == 0) {
			LogError("Login to the master has failed");
			m_status = DISCONNECTED;		// XXX
			m_timeoutTimer.stop();
			m_retryTimer.stop();
		} else if (::memcmp(m_buffer, "RPTACK",  6U) == 0) {
			switch (m_status) {
				case WAITING_LOGIN:
					::memcpy(m_salt, m_buffer + 6U, sizeof(uint32_t));  
					writeAuthorisation();
					m_status = WAITING_AUTHORISATION;
					m_timeoutTimer.start();
					m_retryTimer.start();
					break;
				case WAITING_AUTHORISATION:
					writeConfig();
					m_status = WAITING_CONFIG;
					m_timeoutTimer.start();
					m_retryTimer.start();
					break;
				case WAITING_CONFIG:
					LogMessage("Logged into the master succesfully");
					m_status = RUNNING;
					m_timeoutTimer.start();
					m_retryTimer.stop();
					m_pingTimer.start();
					break;
				default:
					break;
			}
		} else if (::memcmp(m_buffer, "MSTCL",   5U) == 0) {
			LogError("Master is closing down");
			m_status = DISCONNECTED;		// XXX
			m_timeoutTimer.stop();
			m_retryTimer.stop();
		} else if (::memcmp(m_buffer, "MSTPONG", 7U) == 0) {
			m_timeoutTimer.start();
		} else if (::memcmp(m_buffer, "RPTSBKN", 7U) == 0) {
			// Nothing to do for now
		} else {
			CUtils::dump("Unknown packet from the master", m_buffer, length);
		}
	}

	if (m_status != RUNNING) {
		m_retryTimer.clock(ms);
		if (m_retryTimer.isRunning() && m_retryTimer.hasExpired()) {
			switch (m_status) {
				case WAITING_LOGIN:
					writeLogin();
					break;
				case WAITING_AUTHORISATION:
					writeAuthorisation();
					break;
				case WAITING_CONFIG:
					writeConfig();
					break;
				default:
					break;
			}

			m_retryTimer.start();
		}
	} else {
		m_pingTimer.clock(ms);
		if (m_pingTimer.isRunning() && m_pingTimer.hasExpired()) {
			writePing();
			m_pingTimer.start();
		}
	}

	m_timeoutTimer.clock(ms);
	if (m_timeoutTimer.isRunning() && m_timeoutTimer.hasExpired()) {
		LogError("Connection to the master has timed out");
		m_status = DISCONNECTED;
		m_timeoutTimer.stop();
		m_retryTimer.stop();
	}
}

bool CHomebrewDMRIPSC::writeLogin()
{
	unsigned char buffer[8U];

	::memcpy(buffer + 0U, "RPTL", 4U);
	::memcpy(buffer + 4U, m_id, 4U);

	return m_socket.write(buffer, 8U, m_address, m_port);
}

bool CHomebrewDMRIPSC::writeAuthorisation()
{
	unsigned int size = m_password.size();

	unsigned char* in = new unsigned char[size + sizeof(uint32_t)];
	::memcpy(in, m_salt, sizeof(uint32_t));
	for (unsigned int i = 0U; i < size; i++)
		in[i + sizeof(uint32_t)] = m_password.at(i);

	unsigned char out[40U];
	::memcpy(out + 0U, "RPTK", 4U);
	::memcpy(out + 4U, m_id, 4U);

	CSHA256 sha256;
	sha256.buffer(in, size + sizeof(uint32_t), out + 8U);

	delete[] in;

	return m_socket.write(out, 40U, m_address, m_port);
}

bool CHomebrewDMRIPSC::writeConfig()
{
	char* buffer = new char[400U];

	::memcpy(buffer + 0U, "RPTC", 4U);
	::memcpy(buffer + 4U, m_id, 4U);

	::sprintf(buffer + 8U, "%.8s%09u%09u%02u%02u%02.8f%03.8f%03d%.20s%.20s%.124s%.40s%.40s", m_callsign.c_str(),
		m_rxFrequency, m_txFrequency, m_power, m_colorCode, m_latitude, m_longitude, m_height, m_location.c_str(),
		m_description.c_str(), m_url.c_str(), m_version, m_version);

	bool ret = m_socket.write((unsigned char*)buffer, 300U, m_address, m_port);

	delete[] buffer;

	return ret;
}

bool CHomebrewDMRIPSC::writePing()
{
	unsigned char buffer[11U];

	::memcpy(buffer + 0U, "RPTPING", 7U);
	::memcpy(buffer + 7U, m_id, 4U);

	return m_socket.write(buffer, 11U, m_address, m_port);
}

