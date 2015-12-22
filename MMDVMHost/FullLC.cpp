/*
 *	 Copyright (C) 2012 by Ian Wraith
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

#include "FullLC.h"
#include "Log.h"
#include "DMRDefines.h"
#include "RS129.h"

#include <cstdio>
#include <cassert>

CFullLC::CFullLC() :
m_bptc()
{
}

CFullLC::~CFullLC()
{
}

CLC* CFullLC::decode(const unsigned char* data, unsigned char type)
{
	assert(data != NULL);

	unsigned char lcData[12U];
	m_bptc.decode(data, lcData);

	switch (type) {
		case DT_VOICE_LC_HEADER:
			lcData[9U]  ^= VOICE_LC_HEADER_CRC_MASK[0U];
			lcData[10U] ^= VOICE_LC_HEADER_CRC_MASK[1U];
			lcData[11U] ^= VOICE_LC_HEADER_CRC_MASK[2U];
			break;

		case DT_TERMINATOR_WITH_LC:
			lcData[9U]  ^= TERMINATOR_WITH_LC_CRC_MASK[0U];
			lcData[10U] ^= TERMINATOR_WITH_LC_CRC_MASK[1U];
			lcData[11U] ^= TERMINATOR_WITH_LC_CRC_MASK[2U];
			break;

		default:
			::LogError("Unsupported LC type - %d", int(type));
			return NULL;
	}

	if (!CRS129::check(lcData)) {
		::LogDebug("Checksum failed for the LC");
		return NULL;
	}

	return new CLC(lcData);
}

void CFullLC::encode(const CLC& lc, unsigned char* data, unsigned char type)
{
	assert(data != NULL);

	unsigned char lcData[12U];
	lc.getData(lcData);

	unsigned char parity[4U];
	CRS129::encode(lcData, 9U, parity);

	switch (type) {
		case DT_VOICE_LC_HEADER:
			lcData[9U]  = parity[2U] ^ VOICE_LC_HEADER_CRC_MASK[0U];
			lcData[10U] = parity[1U] ^ VOICE_LC_HEADER_CRC_MASK[1U];
			lcData[11U] = parity[0U] ^ VOICE_LC_HEADER_CRC_MASK[2U];
			break;

		case DT_TERMINATOR_WITH_LC:
			lcData[9U]  = parity[2U] ^ TERMINATOR_WITH_LC_CRC_MASK[0U];
			lcData[10U] = parity[1U] ^ TERMINATOR_WITH_LC_CRC_MASK[1U];
			lcData[11U] = parity[0U] ^ TERMINATOR_WITH_LC_CRC_MASK[2U];
			break;

		default:
			::LogError("Unsupported LC type - %d", int(type));
			return;
	}

	m_bptc.encode(lcData, data);
}
