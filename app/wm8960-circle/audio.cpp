#include <circle/timer.h>
#include <circle/synchronize.h>
#include <circle/memory.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <assert.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <cmath>

#include "circleFaustDSP.h"
#include "audio.hpp"
#include "kernel.h"


static const char FromAudioDevice[] = "audio_device";

CAudio::CAudio(CInterruptSystem *pInterrupt, CI2CMaster *pI2CMaster)
    : CI2SSoundBaseDevice(pInterrupt, CAudio::GetSampleRate(), CAudio::GetAudioBlockSize(), FALSE, pI2CMaster, CAudio::I2C_ADDRESS, CI2SSoundBaseDevice::DeviceModeTXRX, 2U),
      currentInputBuffer(inputBuffer_A),
      currentOutputBuffer(outputBuffer_A),
      useBufferA(true)
{
    CLogger::Get()->Write(FromAudioDevice, LogDebug, "CAudio constructor start - AUDIO_BLOCK_SIZE: %d, NUM_FRAMES: %d", CAudio::GetAudioBlockSize(), CAudio::GetNumFrames());
    
    // Initialize buffers to zero
    memset(inputBuffer_A, 0, sizeof(inputBuffer_A));
    memset(inputBuffer_B, 0, sizeof(inputBuffer_B));
    memset(outputBuffer_A, 0, sizeof(outputBuffer_A));
    memset(outputBuffer_B, 0, sizeof(outputBuffer_B));
    
    // Create Faust DSP with correct parameters
    aCircleFaustDSP = new circleFaustDSP(CAudio::GetSampleRate(), CAudio::GetNumFrames(), 2, 2);
    
    if (aCircleFaustDSP == NULL) {
        CLogger::Get()->Write(FromAudioDevice, LogError, "Failed to create circleFaustDSP");
        return;
    }
    
    // Set up DSP channel buffers using our float arrays
    aCircleFaustDSP->setDSP_ChannelBuffers(outputLeft, outputRight, inputLeft, inputRight);
    
    CLogger::Get()->Write(FromAudioDevice, LogDebug, "CAudio constructor completed successfully");
}

CAudio::~CAudio(void)
{
    CLogger::Get()->Write(FromAudioDevice, LogDebug, "CAudio destructor");
    if (aCircleFaustDSP) {
        delete aCircleFaustDSP;
        aCircleFaustDSP = nullptr;
    }
}

// Convert 24-bit packed in 32-bit to signed 24-bit
int32_t CAudio::u32_to_s24(uint32_t value) 
{
    if (value & 0x800000) { // Check if the 23rd bit (sign bit) is set
        return (int32_t)(value | 0xFF000000); // Sign-extend to 32 bits
    } else {
        return (int32_t)(value & 0x00FFFFFF); // Mask to lower 24 bits
    }
}

// Convert signed 24-bit to 32-bit format for I2S
uint32_t CAudio::s24_to_u32(int32_t value)
{
    return (uint32_t)(value & 0x00FFFFFF);
}

// Convert interleaved I2S input to separate float channels
void CAudio::convertInputToFloat()
{
    const float scale = 1.0f / 8388607.0f; // 2^23 - 1 for 24-bit
    
    for (int i = 0; i < CAudio::GetNumFrames(); i++) {
        inputLeft[i] = (float)u32_to_s24(currentInputBuffer[i * 2]) * scale;
        inputRight[i] = (float)u32_to_s24(currentInputBuffer[i * 2 + 1]) * scale;
    }
}

// Convert separate float channels to interleaved I2S output
void CAudio::convertFloatToOutput()
{
    const float scale = 8388607.0f; // 2^23 - 1 for 24-bit
    
    for (int i = 0; i < CAudio::GetNumFrames(); i++) {
        // Clamp to [-1.0, 1.0] range and convert
        float leftClamped = std::max(-1.0f, std::min(1.0f, outputLeft[i]));
        float rightClamped = std::max(-1.0f, std::min(1.0f, outputRight[i]));
        
        int32_t leftSample = (int32_t)(leftClamped * scale);
        int32_t rightSample = (int32_t)(rightClamped * scale);
        
        currentOutputBuffer[i * 2] = s24_to_u32(leftSample);
        currentOutputBuffer[i * 2 + 1] = s24_to_u32(rightSample);
    }
}
/*
void CAudio::Process()
{
    CSoundController *pController = GetController();
    if (!pController) {
        CLogger::Get()->Write(FromAudioDevice, LogDebug, "no controller");
        return;
    }
    
    CLogger::Get()->Write(FromAudioDevice, LogDebug, "got controller");

    // Set volume for default output
    CSoundController::TControlInfo Info = pController->GetControlInfo(
        CSoundController::ControlVolume, 
        CSoundController::JackDefaultOut, 
        CSoundController::ChannelAll);
    
    if (Info.Supported) {
        CLogger::Get()->Write(FromAudioDevice, LogDebug, "ControlVolume JackDefaultOut supported");
        int nVolume = 126;
        nVolume *= Info.RangeMax - Info.RangeMin;
        nVolume /= 127;
        nVolume += Info.RangeMin;
        pController->SetControl(CSoundController::ControlVolume, 
                               CSoundController::JackDefaultOut, 
                               CSoundController::ChannelAll, nVolume);
    }

    // Set volume for headphones
    Info = pController->GetControlInfo(
        CSoundController::ControlVolume, 
        CSoundController::JackHeadphone, 
        CSoundController::ChannelAll);
    
    if (Info.Supported) {
        CLogger::Get()->Write(FromAudioDevice, LogDebug, "ControlVolume JackHeadphone supported");
        int nVolume = 126;
        nVolume *= Info.RangeMax - Info.RangeMin;
        nVolume /= 127;
        nVolume += Info.RangeMin;
        pController->SetControl(CSoundController::ControlVolume, 
                               CSoundController::JackHeadphone, 
                               CSoundController::ChannelAll, nVolume);
    }

    // Enable headphone jack
    bool ret = pController->EnableJack(CSoundController::JackHeadphone);
    if (!ret) {
        CLogger::Get()->Write(FromAudioDevice, LogDebug, "EnableJack failed to enable headphones");
    } else {
        CLogger::Get()->Write(FromAudioDevice, LogDebug, "EnableJack enabled headphones");
    }
}
*/
// Replace your PutChunk implementation with this debug version

void CAudio::PutChunk(const u32 *pBuffer, unsigned nChunkSize)
{
    // Validate chunk size
    if (nChunkSize != CAudio::GetAudioBlockSize()) {
        CLogger::Get()->Write(FromAudioDevice, LogWarning, "PutChunk: unexpected chunk size %d, expected %d", nChunkSize, CAudio::GetAudioBlockSize());
        return;
    }
    
    // Copy input data to current input buffer
    if (useBufferA) {
        memcpy(inputBuffer_A, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_A;
    } else {
        memcpy(inputBuffer_B, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_B;
    }
    
    // Debug logging - analyze input data
    static int put_debug_counter = 0;
    if (put_debug_counter++ % 1000 == 0) { // Log every 1000 chunks
        u32 max_sample = 0;
        u32 min_sample = 0xFFFFFFFF;
        u32 nonzero_count = 0;
        
        for (unsigned int i = 0; i < nChunkSize; i++) {
            u32 sample = pBuffer[i];
            if (sample > max_sample) max_sample = sample;
            if (sample < min_sample) min_sample = sample;
            if (sample != 0) nonzero_count++;
        }
        
        CLogger::Get()->Write(FromAudioDevice, LogDebug, 
                             "PutChunk %d: size=%d, min=0x%08X, max=0x%08X, nonzero=%d, first=0x%08X, last=0x%08X", 
                             put_debug_counter, nChunkSize, min_sample, max_sample, nonzero_count, 
                             pBuffer[0], pBuffer[nChunkSize-1]);
    }
}

// Add this to your audio.cpp - replace the GetChunk implementation
// This version bypasses Faust completely for testing

unsigned CAudio::GetChunk(u32 *pBuffer, unsigned nChunkSize)
{
    // Validate chunk size
    if (nChunkSize != CAudio::GetAudioBlockSize()) {
        CLogger::Get()->Write(FromAudioDevice, LogWarning, "GetChunk: unexpected chunk size %d, expected %d", nChunkSize, CAudio::GetAudioBlockSize());
        return 0;
    }
    
    // === PASS-THROUGH TEST MODE ===
    // Option 1: Direct copy (should be silent if no input)
    // memcpy(pBuffer, currentInputBuffer, nChunkSize * sizeof(u32));
    
    // === UNCOMMENT ONE TEST AT A TIME ===
    
    // Option 2: Generate silence (should eliminate any noise)
    // memset(pBuffer, 0, nChunkSize * sizeof(u32));
    
    // Option 3: Generate a simple test tone at low volume
    // static unsigned int phase = 0;
    // for (unsigned int i = 0; i < nChunkSize / 2; i++) {
    //     // Simple sine wave at ~440Hz, very low amplitude
    //     float sample = 0.1f * sinf(2.0f * 3.14159f * 440.0f * phase / 48000.0f);
    //     int32_t sample24 = (int32_t)(sample * 8388607.0f);
    //     u32 sample_u32 = s24_to_u32(sample24);
        
    //     pBuffer[i * 2] = sample_u32;     // Left
    //     pBuffer[i * 2 + 1] = sample_u32; // Right
    //     phase++;
    // }
    
    // Option 4: Copy input with attenuation (reduce volume by 50%)
    for (unsigned int i = 0; i < nChunkSize; i++) {
        int32_t sample = u32_to_s24(currentInputBuffer[i]);
        sample = sample / 2; // Attenuate
        pBuffer[i] = s24_to_u32(sample);
    }
    
    // Switch buffers for next iteration
    useBufferA = !useBufferA;
    if (useBufferA) {
        currentInputBuffer = inputBuffer_A;
        currentOutputBuffer = outputBuffer_A;
    } else {
        currentInputBuffer = inputBuffer_B;
        currentOutputBuffer = outputBuffer_B;
    }
    
    // Add some debug logging (remove after testing)
    static int debug_counter = 0;
    if (debug_counter++ % 1000 == 0) { // Log every 1000 chunks (~20 seconds at 256 samples/chunk)
        CLogger::Get()->Write(FromAudioDevice, LogDebug, "GetChunk: processed chunk %d, first sample: 0x%08X", 
                             debug_counter, pBuffer[0]);
    }
    
    return nChunkSize;
}
/* old Process function for reference
// Called by I2S when input data is available
void CAudio::PutChunk(const u32 *pBuffer, unsigned nChunkSize)
{
    // Validate chunk size
    if (nChunkSize != CAudio::GetAudioBlockSize()) {
        CLogger::Get()->Write(FromAudioDevice, LogWarning, "PutChunk: unexpected chunk size %d, expected %d", nChunkSize, CAudio::GetAudioBlockSize());
        return;
    }
    
    // Copy input data to current input buffer
    if (useBufferA) {
        memcpy(inputBuffer_A, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_A;
    } else {
        memcpy(inputBuffer_B, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_B;
    }
}

// Called by I2S when output data is needed
unsigned CAudio::GetChunk(u32 *pBuffer, unsigned nChunkSize)
{
    // Validate chunk size
    if (nChunkSize != CAudio::GetAudioBlockSize()) {
        CLogger::Get()->Write(FromAudioDevice, LogWarning, "GetChunk: unexpected chunk size %d, expected %d", nChunkSize, CAudio::GetAudioBlockSize());
        return 0;
    }
    
    if (!aCircleFaustDSP) {
        CLogger::Get()->Write(FromAudioDevice, LogError, "GetChunk: aCircleFaustDSP is NULL");
        memset(pBuffer, 0, nChunkSize * sizeof(u32));
        return nChunkSize;
    }
    
    // Set current output buffer based on double buffering
    if (useBufferA) {
        currentOutputBuffer = outputBuffer_A;
    } else {
        currentOutputBuffer = outputBuffer_B;
    }
    
    // Convert input from I2S format to float
    convertInputToFloat();
    
    // Process audio through Faust DSP
    aCircleFaustDSP->processAudioCallback();
    
    // Convert output from float to I2S format
    convertFloatToOutput();
    
    // Copy processed data to output buffer
    memcpy(pBuffer, currentOutputBuffer, nChunkSize * sizeof(u32));
    
    // Switch buffers for next iteration
    useBufferA = !useBufferA;
    
    return nChunkSize;
}*/