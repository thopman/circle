//
// wm8960soundcontroller.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2022-2024  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <circle/sound/wm8960soundcontroller.h>
#include <assert.h>

CWM8960SoundController::CWM8960SoundController (CI2CMaster *pI2CMaster, u8 uchI2CAddress,
						unsigned nSampleRate,
						boolean bOutSupported, boolean bInSupported)
:	m_pI2CMaster (pI2CMaster),
	m_uchI2CAddress (uchI2CAddress ? uchI2CAddress : 0x1A),
	m_nSampleRate (nSampleRate),
	m_bOutSupported (bOutSupported),
	m_bInSupported (bInSupported),
	m_uchInVolume {0x17, 0x17}		// 0 dB
{
	assert (m_bOutSupported || m_bInSupported);
}

boolean CWM8960SoundController::Probe (void)
{
	// Originally based on https://github.com/RASPIAUDIO/ULTRA/blob/main/ultra.c
	// Licensed under GPLv3
	
	// Reset - R15: Software reset, clears all registers to default state
	if (!WriteReg (15, 0x000))
	{
		return FALSE;
	}
	
	// Power Management
	// R25: Power Management (1) - Enable VREF, ADC L/R, DAC L/R, digital core
	// Bit 8: VREF=1, Bit 5: AINL=1, Bit 4: AINR=1, Bit 3: ADCL=1, Bit 2: ADCR=1, Bit 0: DIGENB=1
	if (   !WriteReg (25, m_bInSupported ? 0x1FC : 0x1C0)
	    // R26: Power Management (2) - Enable DACs and outputs 
	    // Bit 8: DACL=1, Bit 7: DACR=1, plus output enables based on support
	    || !WriteReg (26, m_bOutSupported ? 0x1F9 : 0x001)
	    // R47: Power Management (3) - Enable input/output mixers
	    // Bit 5: LMIC=1, Bit 4: RMIC=1 (if input supported), Bit 3: LOMIX=1, Bit 2: ROMIX=1 (if output supported)
	    || !WriteReg (47,   (m_bInSupported ? 0x030 : 0x000)
			      | (m_bOutSupported ? 0x00C : 0x000)))
	{
		return FALSE;
	}
	
	// Clocking / PLL Configuration
	if (m_nSampleRate == 44100)
	{
		// R4: Clocking (1) - Select PLL as clock source
		// Bit 0: CLKSEL=1 (use PLL), plus SYSCLKDIV and other clock settings
		if (   !WriteReg (4, 0x005)
		    // R52: PLL N - Integer part of PLL multiplier for 44.1kHz
		    || !WriteReg (52, 0x037)
		    // R53-55: PLL K (fractional part) - 24-bit fractional multiplier for 44.1kHz
		    || !WriteReg (53, 0x086)  // PLLK[23:16]
		    || !WriteReg (54, 0x0C2)  // PLLK[15:8] 
		    || !WriteReg (55, 0x026)) // PLLK[7:0]
		{
			return FALSE;
		}
	}
	else if (m_nSampleRate == 48000)
	{
		// R4: Clocking (1) - Select PLL as clock source
		if (   !WriteReg (4, 0x005)
		    // R52: PLL N - Integer part of PLL multiplier for 48kHz
		    || !WriteReg (52, 0x038)
		    // R53-55: PLL K (fractional part) - 24-bit fractional multiplier for 48kHz
		    || !WriteReg (53, 0x031)  // PLLK[23:16]
		    || !WriteReg (54, 0x026)  // PLLK[15:8]
		    || !WriteReg (55, 0x0E8)) // PLLK[7:0]
		{
			return FALSE;
		}
	}
	else
	{
		return FALSE;
	}
	
	// ADC/DAC and Audio Interface Configuration
	// R5: ADC & DAC Control (1) - Basic ADC/DAC settings, disable high-pass filter
	// All bits 0 = normal operation, no mute, no high-pass filter
	if (   !WriteReg (5, 0x000)
	    // R7: Audio Interface - I2S format, slave mode
	    // Bit 3: MS=0 (slave mode), Bit 1: LRP=1 (left channel on high LRCLK)
	    || !WriteReg (7, 0x00A)
	    // R20: Noise Gate - Enable noise gate with threshold
	    // Bits 4-0: NGAT=11001 (noise gate threshold)
	    || !WriteReg (20, 0x0F9))
	{
		return FALSE;
	}
	
	// Volume Settings
	// R2: LOUT1 volume (Headphone Left) - 0dB, enable volume update
	// Bit 8: OUT1VU=1 (volume update), Bits 6-0: volume = 0dB
	if (   !WriteReg (2, 0x179)
	    // R3: ROUT1 volume (Headphone Right) - 0dB, enable volume update  
	    || !WriteReg (3, 0x179)
	    // R40: LOUT2 volume (Speaker Left) - 0dB, enable volume update
	    || !WriteReg (40, 0x179)
	    // R41: ROUT2 volume (Speaker Right) - 0dB, enable volume update
	    || !WriteReg (41, 0x179)
	    // R51: Class D Control (2) - Speaker boost settings
	    // Bits 5-3: DCGAIN, Bits 2-0: ACGAIN for speaker boost
	    || !WriteReg (51, 0x08D)
	    // R0: Left Input PGA Volume - Use stored volume with update bit
	    // Bit 8: IPVU=1 (input volume update), Bits 5-0: volume level
	    || !WriteReg (0, 0x100 | m_uchInVolume[0])
	    // R1: Right Input PGA Volume - Use stored volume with update bit
	    || !WriteReg (1, 0x100 | m_uchInVolume[1]))
	{
		return FALSE;
	}
	
	// Input/Output Signal Path Configuration - MODIFIED FOR LOW-NOISE LINE INPUT
	// R32: ADCL Signal Path - Configure left channel input routing
	// Bit 8: LMN1=0 (disconnect LINPUT1 from PGA inverting input)
	// Bit 7: LMP3=0 (disconnect LINPUT3 from PGA non-inverting input) 
	// Bit 6: LMP2=0 (disconnect LINPUT2 from PGA non-inverting input)
	// Bit 3: LMIC2B=0 (disconnect input PGA from boost mixer - we'll use line inputs directly)
	// All 0 = disable microphone PGA path completely for lowest noise
	if (   !WriteReg (32, 0x000)
	    // R33: ADCR Signal Path - Configure right channel input routing (same as left)
	    // Disable all microphone PGA connections for right channel
	    || !WriteReg (33, 0x000)
	    // R43: Input Boost Mixer (1) - Configure LINPUT2/LINPUT3 to boost mixer
	    // Bits 6-4: LIN3BOOST[2:0]=001 (-12dB gain from LINPUT3 to boost mixer)
	    // Bits 3-1: LIN2BOOST[2:0]=000 (mute LINPUT2)
	    // Use LINPUT3 with minimal gain for clean line input
	    || !WriteReg (43, 0x010)
	    // R44: Input Boost Mixer (2) - Configure RINPUT2/RINPUT3 to boost mixer  
	    // Bits 6-4: RIN3BOOST[2:0]=001 (-12dB gain from RINPUT3 to boost mixer)
	    // Bits 3-1: RIN2BOOST[2:0]=000 (mute RINPUT2)
	    // Use RINPUT3 with minimal gain for clean line input
	    || !WriteReg (44, 0x010)
	    // R49: Class D Control (1) - Enable speaker outputs
	    // Bits 7-6: SPK_OP_EN[1:0]=11 (enable both left and right speakers)
	    || !WriteReg (49, 0x0F7)
	    // R34: Left Output Mix (1) - Route DAC to left output mixer
	    // Bit 8: LD2LO=1 (connect left DAC output to left output mixer)
	    || !WriteReg (34, 0x100)
	    // R37: Right Output Mix (2) - Route DAC to right output mixer  
	    // Bit 8: RD2RO=1 (connect right DAC output to right output mixer)
	    || !WriteReg (37, 0x100))
	{
		return FALSE;
	}
	
	return TRUE;
}

boolean CWM8960SoundController::EnableJack (TJack Jack)
{
	switch (Jack)
	{
	case JackSpeaker:
		return WriteReg (49, 0x0F7);	// Speaker Control

	case JackDefaultOut:
	case JackLineOut:
	case JackHeadphone:
	case JackDefaultIn:
	case JackMicrophone:
		return TRUE;

	default:
		break;
	}

	return FALSE;
}

// Headphone jack cannot be disabled
boolean CWM8960SoundController::DisableJack (TJack Jack)
{
	if (Jack == JackSpeaker)
	{
		return WriteReg (49, 0x037);	// Speaker Control
	}

	return FALSE;
}

const CSoundController::TControlInfo CWM8960SoundController::GetControlInfo (TControl Control,
									     TJack Jack,
									     TChannel Channel) const
{
	switch (Control)
	{
	case ControlMute:
		if (IsInputJack (Jack))
		{
			return { TRUE, 0, 1 };
		}
		break;

	case ControlVolume:
		if (IsInputJack (Jack))
		{
			return { TRUE, -17, 30 };
		}
		else
		{
			return { TRUE, -73, 6 };
		}
		break;

	case ControlALC:
		if (   IsInputJack (Jack)
		    && Channel == ChannelAll)
		{
			return { TRUE, 0, 1 };
		}
		break;

	default:
		break;
	}

	return { FALSE, 0, 0 };
}

boolean CWM8960SoundController::SetControl (TControl Control, TJack Jack, TChannel Channel, int nValue)
{
	switch (Control)
	{
	case ControlMute:
		if (IsInputJack (Jack))
		{
			if (   Channel == ChannelLeft
			    || Channel == ChannelAll)
			{
				m_uchInVolume[0] &= 0x3F;
				m_uchInVolume[0] |= nValue ? 0x80 : 0x00;

				if (!WriteReg (0, 0x100 | m_uchInVolume[0]))
				{
					return FALSE;
				}
			}

			if (   Channel == ChannelRight
			    || Channel == ChannelAll)
			{
				m_uchInVolume[1] &= 0x3F;
				m_uchInVolume[1] |= nValue ? 0x80 : 0x00;

				if (!WriteReg (1, 0x100 | m_uchInVolume[1]))
				{
					return FALSE;
				}
			}

			return TRUE;
		}
		break;

	case ControlVolume:
		if (IsInputJack (Jack))
		{
			if (!(-17 <= nValue && nValue <= 30))
			{
				return FALSE;
			}

			u8 uchValue = (nValue + 17) * 100 / 75;

			if (   Channel == ChannelLeft
			    || Channel == ChannelAll)
			{
				m_uchInVolume[0] &= 0x80;
				m_uchInVolume[0] |= uchValue;

				if (!WriteReg (0, 0x100 | m_uchInVolume[0]))
				{
					return FALSE;
				}
			}

			if (   Channel == ChannelRight
			    || Channel == ChannelAll)
			{
				m_uchInVolume[1] &= 0x80;
				m_uchInVolume[1] |= uchValue;

				if (!WriteReg (1, 0x100 | m_uchInVolume[1]))
				{
					return FALSE;
				}
			}

			return TRUE;
		}
		else
		{
			if (!(-73 <= nValue && nValue <= 6))
			{
				return FALSE;
			}

			u8 uchValue = nValue + 73 + 0x30;

			u8 nRegL = Jack == JackSpeaker ? 40 : 2;

			if (   Channel == ChannelLeft
			    || Channel == ChannelAll)
			{
				if (!WriteReg (nRegL, 0x100 | uchValue))
				{
					return FALSE;
				}
			}

			if (   Channel == ChannelRight
			    || Channel == ChannelAll)
			{
				if (!WriteReg (nRegL + 1, 0x100 | uchValue))
				{
					return FALSE;
				}
			}

			return TRUE;
		}
		break;

	case ControlALC:
		if (   IsInputJack (Jack)
		    && Channel == ChannelAll)
		{
			if (nValue)
			{
				// Ensure that left and right input volumes are the same
				m_uchInVolume[1] = m_uchInVolume[0];
				if (!WriteReg (1, 0x100 | m_uchInVolume[1]))
				{
					return FALSE;
				}

				return WriteReg (17, 0x1FB);
			}
			else
			{
				return WriteReg (17, 0x00B);
			}
		}
		break;

	default:
		break;
	}

	return FALSE;
}

boolean CWM8960SoundController::WriteReg (u8 nReg, u16 nValue)
{
	assert (nReg <= 0x7F);
	assert (nValue <= 0x1FF);
	const u8 Cmd[] = { (u8) ((nReg << 1) | (nValue >> 8)), (u8) nValue };

	assert (m_pI2CMaster);
	assert (m_uchI2CAddress);
	return m_pI2CMaster->Write (m_uchI2CAddress, Cmd, sizeof Cmd) == sizeof Cmd;
}