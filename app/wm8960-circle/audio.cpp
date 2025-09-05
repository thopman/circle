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

// Constants for 24-bit conversion (left-justified format)
static constexpr float kScale24ToFloat = 1.0f / 8388608.0f;  // 2^-23
static constexpr float kScaleFloatTo24 = 8388608.0f;        // 2^23
static constexpr int32_t kMin24 = -(1 << 23);               // -8388608
static constexpr int32_t kMax24 = (1 << 23) - 1;            // +8388607

CAudio::CAudio(CInterruptSystem *pInterrupt, CI2CMaster *pI2CMaster)
    : CI2SSoundBaseDevice(pInterrupt, SAMPLE_RATE, AUDIO_BLOCK_SIZE, FALSE, pI2CMaster, I2C_ADDRESS, CI2SSoundBaseDevice::DeviceModeTXRX, 2U),
      aCircleFaustDSP(nullptr),
      currentInputBuffer(inputBuffer_A),
      currentOutputBuffer(outputBuffer_A),
      useBufferA(true)
{
    // Initialize all buffers to zero
    memset(inputBuffer_A, 0, sizeof(inputBuffer_A));
    memset(inputBuffer_B, 0, sizeof(inputBuffer_B));
    memset(outputBuffer_A, 0, sizeof(outputBuffer_A));
    memset(outputBuffer_B, 0, sizeof(outputBuffer_B));
    memset(inputLeft, 0, sizeof(inputLeft));
    memset(inputRight, 0, sizeof(inputRight));
    memset(outputLeft, 0, sizeof(outputLeft));
    memset(outputRight, 0, sizeof(outputRight));
    
    // Create Faust DSP
    aCircleFaustDSP = new circleFaustDSP(SAMPLE_RATE, NUM_FRAMES, 2, 2);
    
    if (aCircleFaustDSP) {
        // Set up DSP channel buffers - Faust will process these directly
        aCircleFaustDSP->setDSP_ChannelBuffers(outputLeft, outputRight, inputLeft, inputRight);
    }
}

CAudio::~CAudio(void)
{
    if (aCircleFaustDSP) {
        delete aCircleFaustDSP;
        aCircleFaustDSP = nullptr;
    }
}

// Convert interleaved I2S input (left-justified 24-bit) to separate float channels
void CAudio::convertAndDeinterleave()
{
    for (unsigned int i = 0; i < NUM_FRAMES; i++) {
        // Left channel: extract from even indices
        // 24-bit sample is in bits 31-8, so arithmetic right shift by 8
        int32_t leftSample = (int32_t)currentInputBuffer[i * 2] >> 8;
        inputLeft[i] = (float)leftSample * kScale24ToFloat;
        
        // Right channel: extract from odd indices
        int32_t rightSample = (int32_t)currentInputBuffer[i * 2 + 1] >> 8;
        inputRight[i] = (float)rightSample * kScale24ToFloat;
    }
}

void CAudio::convertAndInterleave()
{
    for (unsigned int i = 0; i < NUM_FRAMES; i++) {
        // Left channel: clamp and convert using std::max/std::min
        float leftClamped = std::max(-1.0f, std::min(outputLeft[i], 0.999999f));
        int32_t leftSample = (int32_t)(leftClamped * kScaleFloatTo24);
        leftSample = std::max(kMin24, std::min(leftSample, kMax24));
        currentOutputBuffer[i * 2] = (uint32_t)leftSample << 8;  // Left-justify to bits 31-8
        
        // Right channel: clamp and convert using std::max/std::min
        float rightClamped = std::max(-1.0f, std::min(outputRight[i], 0.999999f));
        int32_t rightSample = (int32_t)(rightClamped * kScaleFloatTo24);
        rightSample = std::max(kMin24, std::min(rightSample, kMax24));
        currentOutputBuffer[i * 2 + 1] = (uint32_t)rightSample << 8;  // Left-justify to bits 31-8
    }
}

// PutChunk: Receive I2S input and convert to float
void CAudio::PutChunk(const u32 *pBuffer, unsigned nChunkSize)
{
    if (nChunkSize != AUDIO_BLOCK_SIZE) {
        return; // Silent error handling in audio callback
    }
    
    // Copy I2S input to our buffer
    if (useBufferA) {
        memcpy(inputBuffer_A, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_A;
    } else {
        memcpy(inputBuffer_B, pBuffer, nChunkSize * sizeof(u32));
        currentInputBuffer = inputBuffer_B;
    }
    
    // Convert interleaved I2S input to separate float arrays
    convertAndDeinterleave();
}

// GetChunk: Process Faust DSP and send I2S output
unsigned CAudio::GetChunk(u32 *pBuffer, unsigned nChunkSize)
{
    if (nChunkSize != AUDIO_BLOCK_SIZE) {
        return 0; // Silent error handling in audio callback
    }
    
    // Set current output buffer for conversion
    if (useBufferA) {
        currentOutputBuffer = outputBuffer_A;
    } else {
        currentOutputBuffer = outputBuffer_B;
    }
    
    // Process audio through Faust DSP
    if (aCircleFaustDSP) {
        aCircleFaustDSP->processAudioCallback();
    } else {
        // Fallback: copy input to output if no DSP (for testing)
        memcpy(outputLeft, inputLeft, sizeof(inputLeft));
        memcpy(outputRight, inputRight, sizeof(inputRight));
    }
    
    // Convert separate float arrays back to interleaved I2S output
    convertAndInterleave();
    
    // Copy to I2S output buffer
    memcpy(pBuffer, currentOutputBuffer, nChunkSize * sizeof(u32));
    
    // Switch buffers for next iteration
    useBufferA = !useBufferA;
    
    return nChunkSize;
}

void CAudio::Process()
{
    // This method can be used for setting up volume or jack detection if needed
}