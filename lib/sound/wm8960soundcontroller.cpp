//
// wm8960soundcontroller.cpp - CORRECTED FOR UNITY GAIN
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
// MODIFICATION NOTES:
// - Fixed boost mixer gain settings in R43/R44 to eliminate 12dB signal loss
// - Changed LIN3BOOST/RIN3BOOST from 001 (-12dB) to 101 (0dB) for unity gain
// - Signal path: LINPUT3 → Boost Mixer (0dB) → PGA → ADC → Faust DSP → DAC → Headphone
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
	m_uchInVolume {0x17, 0x17}		// 0 dB (23 decimal = 0x17, matches 0dB on input PGA scale)
{
	assert (m_bOutSupported || m_bInSupported);
}

boolean CWM8960SoundController::Probe (void)
{
	// Originally based on https://github.com/RASPIAUDIO/ULTRA/blob/main/ultra.c
	// Licensed under GPLv3
	
	// Reset - R15: Software reset, clears all registers to default state
	// This ensures we start from a known configuration
	if (!WriteReg (15, 0x000))
	{
		return FALSE;
	}
	
	// Power Management Configuration
	// Critical: Power up sequence must be correct for proper operation
	// R25: Power Management (1) - Enable VREF, ADC L/R, DAC L/R, digital core
	// Bit 8: VREF=1 (voltage reference - required for all analog circuits)
	// Bit 5: AINL=1 (left analog input power - needed for input processing)
	// Bit 4: AINR=1 (right analog input power - needed for input processing) 
	// Bit 3: ADCL=1 (left ADC power - converts analog input to digital)
	// Bit 2: ADCR=1 (right ADC power - converts analog input to digital)
	// Bit 0: DIGENB=1 (digital core enable - required for DSP processing)
	if (   !WriteReg (25, m_bInSupported ? 0x1FC : 0x1C0)
	    // R26: Power Management (2) - Enable DACs and output drivers
	    // Bit 8: DACL=1 (left DAC power - converts digital back to analog)
	    // Bit 7: DACR=1 (right DAC power - converts digital back to analog)
	    // Additional bits enable output drivers based on bOutSupported flag
	    || !WriteReg (26, m_bOutSupported ? 0x1F9 : 0x001)
	    // R47: Power Management (3) - Enable input/output mixers
	    // Bit 5: LMIC=1 (left input mixer - needed for input signal routing)
	    // Bit 4: RMIC=1 (right input mixer - needed for input signal routing) 
	    // Bit 3: LOMIX=1 (left output mixer - needed for output signal routing)
	    // Bit 2: ROMIX=1 (right output mixer - needed for output signal routing)
	    || !WriteReg (47,   (m_bInSupported ? 0x030 : 0x000)
			      | (m_bOutSupported ? 0x00C : 0x000)))
	{
		return FALSE;
	}
	
	// Clocking / PLL Configuration
	// The WM8960 needs precise clock generation for proper sample rates
	// PLL converts external clock (typically 12MHz) to required internal clocks
	if (m_nSampleRate == 44100)
	{
		// R4: Clocking (1) - Select PLL as clock source and configure dividers
		// Bit 0: CLKSEL=1 (use PLL output instead of direct MCLK)
		// Additional bits configure system clock dividers for 44.1kHz operation
		if (   !WriteReg (4, 0x005)
		    // R52: PLL N - Integer part of PLL multiplier for 44.1kHz
		    // PLL equation: Fout = (MCLK * N.K) / M, where N.K is the multiplier
		    || !WriteReg (52, 0x037)  // N = 55 decimal (0x37)
		    // R53-55: PLL K (fractional part) - 24-bit fractional multiplier for 44.1kHz
		    // These three registers form a 24-bit fractional value for precise tuning
		    || !WriteReg (53, 0x086)  // PLLK[23:16] - upper 8 bits of fractional part
		    || !WriteReg (54, 0x0C2)  // PLLK[15:8] - middle 8 bits of fractional part
		    || !WriteReg (55, 0x026)) // PLLK[7:0] - lower 8 bits of fractional part
		{
			return FALSE;
		}
	}
	else if (m_nSampleRate == 48000)
	{
		// R4: Clocking (1) - Select PLL as clock source and configure dividers
		// Same as 44.1kHz but different PLL settings for 48kHz family
		if (   !WriteReg (4, 0x005)
		    // R52: PLL N - Integer part of PLL multiplier for 48kHz  
		    || !WriteReg (52, 0x038)  // N = 56 decimal (0x38)
		    // R53-55: PLL K (fractional part) - 24-bit fractional multiplier for 48kHz
		    // Different fractional values needed for 48kHz vs 44.1kHz rates
		    || !WriteReg (53, 0x031)  // PLLK[23:16] for 48kHz
		    || !WriteReg (54, 0x026)  // PLLK[15:8] for 48kHz  
		    || !WriteReg (55, 0x0E8)) // PLLK[7:0] for 48kHz
		{
			return FALSE;
		}
	}
	else
	{
		// Only 44.1kHz and 48kHz sample rates are supported
		return FALSE;
	}
	
	// ADC/DAC and Audio Interface Configuration
	// These settings control the digital audio interface and processing options
	// R5: ADC & DAC Control (1) - Basic ADC/DAC settings
	// Bit 3: DACMU=0 (disable DAC soft mute - CRITICAL for signal passthrough)
	// Bit 2: DEEMPH=0 (disable de-emphasis filter)
	// Bit 1: DACOSR=0 (DAC oversampling rate normal - 64x)
	// Bit 0: ADCOSR=0 (ADC oversampling rate normal - 64x)
	// All bits 0 = normal operation, no mute, no high-pass filter, standard oversampling
	if (   !WriteReg (5, 0x000)
	    // R7: Audio Interface - Configure I2S format and timing
	    // Bit 3: MS=0 (slave mode - WM8960 follows external I2S clock)
	    // Bit 2: LRP=0 (left/right clock polarity normal)
	    // Bit 1: WL[1]=1, Bit 0: WL[0]=0 (word length = 16 bits)
	    // Format: I2S with WM8960 as slave, 16-bit data
	    || !WriteReg (7, 0x00A)
	    // R20: Noise Gate - Enable noise gate with threshold to reduce background noise
	    // Bits 4-0: NGAT=11001 (noise gate threshold setting)
	    // Helps reduce noise when no signal is present
	    || !WriteReg (20, 0x0F9))
	{
		return FALSE;
	}
	
	// Volume Settings - Configure all volume controls for 0dB operation
	// These registers control the final output levels and must include volume update bits
	// R2: LOUT1 volume (Headphone Left) - Set to 0dB with volume update enabled
	// Bit 8: OUT1VU=1 (volume update - required for changes to take effect immediately)
	// Bits 6-0: volume level = 121 decimal = 0dB on headphone scale (-73dB to +6dB range)
	if (   !WriteReg (2, 0x179)   // 0x100 (update) + 0x79 (121 decimal) = 0dB headphone volume
	    // R3: ROUT1 volume (Headphone Right) - Mirror left channel settings  
	    || !WriteReg (3, 0x179)   // Match left channel for stereo balance
	    // R40: LOUT2 volume (Speaker Left) - Set to 0dB (even if not using speakers)
	    || !WriteReg (40, 0x179)  // Same scale as headphone outputs
	    // R41: ROUT2 volume (Speaker Right) - Set to 0dB (even if not using speakers)
	    || !WriteReg (41, 0x179)  // Same scale as headphone outputs
	    // R51: Class D Control (2) - Speaker boost and Class D amplifier settings
	    // Bits 5-3: DCGAIN (DC gain control for Class D)
	    // Bits 2-0: ACGAIN (AC gain control for Class D) 
	    || !WriteReg (51, 0x08D)
	    // R0: Left Input PGA Volume - Set input gain to 0dB with volume update
	    // Bit 8: IPVU=1 (input PGA volume update - required for immediate effect)
	    // Bit 7: INMUTE=0 (not muted - CRITICAL for signal passthrough)
	    // Bits 5-0: volume level = 23 decimal (0x17) = 0dB on input PGA scale (-17.25dB to +30dB range)
	    || !WriteReg (0, 0x100 | m_uchInVolume[0])  // 0x100 (update) + 0x17 (0dB) = unmuted 0dB input
	    // R1: Right Input PGA Volume - Mirror left channel settings for stereo balance
	    || !WriteReg (1, 0x100 | m_uchInVolume[1])) // Same as left channel
	{
		return FALSE;
	}
	
	// Input/Output Signal Path Configuration 
	// CRITICAL SECTION: This configures the actual signal routing through the codec
	// Your signal path: LINPUT3/RINPUT3 → Boost Mixer → PGA → ADC → Faust DSP → DAC → Headphone
	
	// R32: ADCL Signal Path - Configure left channel input routing to ADC
	// IMPORTANT: Original setting 0x000 disables direct PGA connections
	// This forces the signal to go through the boost mixer path (which is what we want for LINPUT3)
	// Bit 8: LMN1=0 (disconnect LINPUT1 from PGA inverting input)
	// Bit 7: LMP3=0 (disconnect LINPUT3 from PGA non-inverting input - we use boost mixer instead)
	// Bit 6: LMP2=0 (disconnect LINPUT2 from PGA non-inverting input)  
	// Bit 3: LMIC2B=0 (boost mixer to PGA connection controlled by boost mixer settings)
	// All 0 = rely on boost mixer for LINPUT3 connection (your original working path)
	if (   !WriteReg (32, 0x000)  // Keep your original ADCL signal path configuration
	    // R33: ADCR Signal Path - Configure right channel input routing (mirror left channel)
	    // Same settings as left channel for stereo balance
	    || !WriteReg (33, 0x000)  // Keep your original ADCR signal path configuration
	    
	    // CRITICAL FIX: Input Boost Mixer Configuration
	    // This is where your 12dB signal loss was occurring!
	    // R43: Input Boost Mixer (1) - Configure LINPUT3 boost gain 
	    // Bits 6-4: LIN3BOOST[2:0] = boost gain from LINPUT3 to left boost mixer output
	    // ORIGINAL PROBLEM: 0x010 = LIN3BOOST = 001 = -12dB (caused your signal loss!)
	    // CORRECTED SOLUTION: 0x050 = LIN3BOOST = 101 = 0dB (unity gain!)
	    // 
	    // Boost gain encoding (bits 6-4):
	    // 000 = Muted, 001 = -12dB, 010 = -9dB, 011 = -6dB, 100 = -3dB, 101 = 0dB, 110 = +3dB, 111 = +6dB
	    // Bits 3-1: LIN2BOOST[2:0] = 000 (mute LINPUT2 since we're using LINPUT3)
	    || !WriteReg (43, 0x050)  // FIXED: LINPUT3 to boost mixer at 0dB gain (was 0x010 = -12dB)
	    // R44: Input Boost Mixer (2) - Configure RINPUT3 boost gain (mirror left channel)
	    // Same settings as left channel for stereo operation
	    // Bits 6-4: RIN3BOOST[2:0] = 101 = 0dB gain from RINPUT3 to right boost mixer
	    || !WriteReg (44, 0x050)  // FIXED: RINPUT3 to boost mixer at 0dB gain (was 0x010 = -12dB)
	    
	    // Output Signal Path Configuration
	    // R49: Class D Control (1) - Enable speaker outputs (even if using headphones)
	    // Bits 7-6: SPK_OP_EN[1:0]=11 (enable both left and right speaker drivers)
	    // Additional bits control Class D amplifier settings
	    || !WriteReg (49, 0x0F7)  // Keep your original speaker control settings
	    // R34: Left Output Mix (1) - Route DAC output to left output mixer
	    // Bit 8: LD2LO=1 (connect left DAC output to left output mixer)
	    // This is essential for digital audio (post-Faust DSP) to reach the headphone output
	    || !WriteReg (34, 0x100)  // Keep your original: left DAC → left output mixer
	    // R37: Right Output Mix (2) - Route DAC output to right output mixer  
	    // Bit 8: RD2RO=1 (connect right DAC output to right output mixer)
	    // Mirror of left channel for stereo output
	    || !WriteReg (37, 0x100)) // Keep your original: right DAC → right output mixer
	{
		return FALSE;
	}
	
	// Configuration complete - signal path should now have unity gain (0dB)
	// Total signal flow: LINPUT3 → Boost Mixer (0dB) → PGA (0dB) → ADC → Faust DSP (0dB) → DAC → Output Mixer → Headphone (0dB)
	return TRUE;
}

// Jack control methods - Enable/disable different output jacks
boolean CWM8960SoundController::EnableJack (TJack Jack)
{
	switch (Jack)
	{
	case JackSpeaker:
		// Enable speaker outputs via Class D amplifier control
		return WriteReg (49, 0x0F7);	// Speaker Control - enable both channels

	case JackDefaultOut:
	case JackLineOut:
	case JackHeadphone:
	case JackDefaultIn:
	case JackMicrophone:
		// These jacks are always enabled by default power management settings
		return TRUE;

	default:
		break;
	}

	return FALSE;
}

// Headphone jack cannot be disabled on WM8960 (always active when powered)
boolean CWM8960SoundController::DisableJack (TJack Jack)
{
	if (Jack == JackSpeaker)
	{
		// Disable speaker outputs while keeping other circuitry active
		return WriteReg (49, 0x037);	// Speaker Control - disable output drivers
	}

	// Other jacks cannot be individually disabled
	return FALSE;
}

// Get information about available controls for different jacks and channels
const CSoundController::TControlInfo CWM8960SoundController::GetControlInfo (TControl Control,
									     TJack Jack,
									     TChannel Channel) const
{
	switch (Control)
	{
	case ControlMute:
		if (IsInputJack (Jack))
		{
			// Input jacks support mute control (0=unmuted, 1=muted)
			return { TRUE, 0, 1 };
		}
		break;

	case ControlVolume:
		if (IsInputJack (Jack))
		{
			// Input PGA volume range: -17.25dB to +30dB (in 0.75dB steps)
			// Simplified to integer dB values: -17dB to +30dB  
			return { TRUE, -17, 30 };
		}
		else
		{
			// Output volume range: -73dB to +6dB (in 1dB steps)
			return { TRUE, -73, 6 };
		}
		break;

	case ControlALC:
		if (   IsInputJack (Jack)
		    && Channel == ChannelAll)
		{
			// Automatic Level Control available for input (0=disabled, 1=enabled)
			// Only works when both channels are controlled together
			return { TRUE, 0, 1 };
		}
		break;

	default:
		break;
	}

	// Control not supported for this jack/channel combination
	return { FALSE, 0, 0 };
}

// Set control values for different jacks and channels
boolean CWM8960SoundController::SetControl (TControl Control, TJack Jack, TChannel Channel, int nValue)
{
	switch (Control)
	{
	case ControlMute:
		if (IsInputJack (Jack))
		{
			// Input mute control - affects input PGA mute bit
			if (   Channel == ChannelLeft
			    || Channel == ChannelAll)
			{
				// Modify left channel input volume register
				m_uchInVolume[0] &= 0x3F;  // Clear mute bit (bit 7) and preserve volume (bits 5-0)
				m_uchInVolume[0] |= nValue ? 0x80 : 0x00;  // Set mute bit if nValue=1

				// Write to R0 with volume update bit (bit 8) always set
				if (!WriteReg (0, 0x100 | m_uchInVolume[0]))
				{
					return FALSE;
				}
			}

			if (   Channel == ChannelRight
			    || Channel == ChannelAll)
			{
				// Modify right channel input volume register (mirror left channel)
				m_uchInVolume[1] &= 0x3F;  // Clear mute bit and preserve volume
				m_uchInVolume[1] |= nValue ? 0x80 : 0x00;  // Set mute bit if nValue=1

				// Write to R1 with volume update bit always set
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
			// Input PGA volume control (-17dB to +30dB range)
			if (!(-17 <= nValue && nValue <= 30))
			{
				return FALSE;  // Value out of range
			}

			// Convert dB value to register value
			// Input PGA: 63 steps from -17.25dB to +30dB = 47.25dB range
			// Step size = 47.25dB / 63 = 0.75dB per step
			// Register value = (dB + 17.25) / 0.75 = (dB + 17) * 100 / 75 (approximately)
			u8 uchValue = (nValue + 17) * 100 / 75;

			if (   Channel == ChannelLeft
			    || Channel == ChannelAll)
			{
				// Update left channel volume while preserving mute bit
				m_uchInVolume[0] &= 0x80;  // Preserve mute bit (bit 7)
				m_uchInVolume[0] |= uchValue;  // Set volume bits (5-0)

				// Write to R0 with volume update bit
				if (!WriteReg (0, 0x100 | m_uchInVolume[0]))
				{
					return FALSE;
				}
			}

			if (   Channel == ChannelRight
			    || Channel == ChannelAll)
			{
				// Update right channel volume (mirror left channel)
				m_uchInVolume[1] &= 0x80;  // Preserve mute bit
				m_uchInVolume[1] |= uchValue;  // Set volume bits

				// Write to R1 with volume update bit
				if (!WriteReg (1, 0x100 | m_uchInVolume[1]))
				{
					return FALSE;
				}
			}

			return TRUE;
		}
		else
		{
			// Output volume control (headphone or speaker)
			if (!(-73 <= nValue && nValue <= 6))
			{
				return FALSE;  // Value out of range
			}

			// Convert dB value to register value
			// Output range: -73dB to +6dB = 79dB range in 1dB steps
			// Register encoding: 0x30 = -73dB, 0x7F = +6dB
			// Register value = dB + 73 + 0x30
			u8 uchValue = nValue + 73 + 0x30;

			// Select register base address (headphone vs speaker)
			u8 nRegL = Jack == JackSpeaker ? 40 : 2;  // R40/R41 for speaker, R2/R3 for headphone

			if (   Channel == ChannelLeft
			    || Channel == ChannelAll)
			{
				// Write left channel output volume with update bit
				if (!WriteReg (nRegL, 0x100 | uchValue))
				{
					return FALSE;
				}
			}

			if (   Channel == ChannelRight
			    || Channel == ChannelAll)
			{
				// Write right channel output volume with update bit
				if (!WriteReg (nRegL + 1, 0x100 | uchValue))
				{
					return FALSE;
				}
			}

			return TRUE;
		}
		break;

	case ControlALC:
		// Automatic Level Control - only works on input with both channels
		if (   IsInputJack (Jack)
		    && Channel == ChannelAll)
		{
			if (nValue)
			{
				// Enable ALC: First ensure both channels have same volume
				m_uchInVolume[1] = m_uchInVolume[0];  // Copy left volume to right
				if (!WriteReg (1, 0x100 | m_uchInVolume[1]))
				{
					return FALSE;
				}

				// R17: ALC Control 1 - Enable ALC with appropriate settings
				return WriteReg (17, 0x1FB);  // Enable ALC with standard settings
			}
			else
			{
				// Disable ALC
				return WriteReg (17, 0x00B);  // Disable ALC, return to manual gain control
			}
		}
		break;

	default:
		break;
	}

	// Control not supported or invalid parameters
	return FALSE;
}

// Low-level register write function
// Handles I2C communication protocol specific to WM8960
boolean CWM8960SoundController::WriteReg (u8 nReg, u16 nValue)
{
	// Validate register address and value ranges
	assert (nReg <= 0x7F);    // Register addresses are 7 bits (0-127)
	assert (nValue <= 0x1FF); // Register values are 9 bits (0-511)
	
	// WM8960 I2C protocol: first byte contains register address and MSB of value
	// Second byte contains lower 8 bits of value
	// Format: [REG_ADDR[6:0], DATA[8]] [DATA[7:0]]
	const u8 Cmd[] = { (u8) ((nReg << 1) | (nValue >> 8)), (u8) nValue };

	// Send I2C write command to WM8960
	assert (m_pI2CMaster);   // Ensure I2C master is initialized
	assert (m_uchI2CAddress); // Ensure I2C address is set (typically 0x1A)
	return m_pI2CMaster->Write (m_uchI2CAddress, Cmd, sizeof Cmd) == sizeof Cmd;
}