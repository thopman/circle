//
// performancemonitor.cpp
//
// Performance monitoring implementation using Circle framework
//

#include "performancemonitor.h"
#include <circle/util.h>
#include <circle/new.h>
#include <circle/synchronize.h>
#include <assert.h>

static const char FromPerformanceMonitor[] = "perfmon";

CPerformanceMonitor::CPerformanceMonitor(const char* name, unsigned maxSamples)
    : m_pName(name),
      m_nSampleRate(48000),        // Default sample rate
      m_nBufferSize(256),          // Default buffer size
      m_nMaxSamples(maxSamples),
      m_nStartTicks(0),
      m_nEndTicks(0),
      m_bTimingActive(false),
      m_pClockCycles(nullptr),
      m_pProcessingTimes(nullptr),
      m_pCPUUsages(nullptr),
      m_nCurrentIndex(0),
      m_nValidSamples(0)
{
    // Allocate storage arrays
    m_pClockCycles = new unsigned[m_nMaxSamples];
    m_pProcessingTimes = new float[m_nMaxSamples];
    m_pCPUUsages = new float[m_nMaxSamples];
    
    // Initialize arrays
    for (unsigned i = 0; i < m_nMaxSamples; i++) {
        m_pClockCycles[i] = 0;
        m_pProcessingTimes[i] = 0.0f;
        m_pCPUUsages[i] = 0.0f;
    }
    
    // Initialize last result
    m_LastResult.clockCycles = 0;
    m_LastResult.processingTimeUs = 0.0f;
    m_LastResult.cpuUsagePercent = 0.0f;
}

CPerformanceMonitor::~CPerformanceMonitor()
{
    delete[] m_pClockCycles;
    delete[] m_pProcessingTimes;
    delete[] m_pCPUUsages;
}

void CPerformanceMonitor::StartTiming()
{
    // Memory barrier to ensure timing accuracy
    DataSyncBarrier();
    
    // Get current timer tick count
    m_nStartTicks = CTimer::GetClockTicks();
    m_bTimingActive = true;
    
    DataSyncBarrier();
}

void CPerformanceMonitor::EndTiming()
{
    EndTiming(m_nBufferSize);
}

void CPerformanceMonitor::EndTiming(unsigned numSamplesProcessed)
{
    if (!m_bTimingActive) {
        return;
    }
    
    // Memory barrier to ensure timing accuracy
    DataSyncBarrier();
    
    // Get end time
    m_nEndTicks = CTimer::GetClockTicks();
    m_bTimingActive = false;
    
    DataSyncBarrier();
    
    // Calculate metrics
    unsigned clockCycles = m_nEndTicks - m_nStartTicks;
    float processingTimeUs = ClockTicksToMicroseconds(clockCycles);
    float cpuUsagePercent = CalculateCPUUsage(processingTimeUs, numSamplesProcessed);
    
    // Store in circular buffer
    m_pClockCycles[m_nCurrentIndex] = clockCycles;
    m_pProcessingTimes[m_nCurrentIndex] = processingTimeUs;
    m_pCPUUsages[m_nCurrentIndex] = cpuUsagePercent;
    
    // Update last result (for real-time access)
    m_LastResult.clockCycles = clockCycles;
    m_LastResult.processingTimeUs = processingTimeUs;
    m_LastResult.cpuUsagePercent = cpuUsagePercent;
    
    // Update indices
    m_nCurrentIndex = (m_nCurrentIndex + 1) % m_nMaxSamples;
    if (m_nValidSamples < m_nMaxSamples) {
        m_nValidSamples++;
    }
}

float CPerformanceMonitor::GetAverageCPUUsagePercent() const
{
    if (m_nValidSamples == 0) return 0.0f;
    return CalculateAverage(m_pCPUUsages, m_nValidSamples);
}

float CPerformanceMonitor::GetMaxCPUUsagePercent() const
{
    if (m_nValidSamples == 0) return 0.0f;
    return CalculateMax(m_pCPUUsages, m_nValidSamples);
}

unsigned CPerformanceMonitor::GetAverageClockCycles() const
{
    if (m_nValidSamples == 0) return 0;
    return CalculateAverage(m_pClockCycles, m_nValidSamples);
}

unsigned CPerformanceMonitor::GetMaxClockCycles() const
{
    if (m_nValidSamples == 0) return 0;
    return CalculateMax(m_pClockCycles, m_nValidSamples);
}

float CPerformanceMonitor::GetAverageProcessingTimeUs() const
{
    if (m_nValidSamples == 0) return 0.0f;
    return CalculateAverage(m_pProcessingTimes, m_nValidSamples);
}

float CPerformanceMonitor::GetMaxProcessingTimeUs() const
{
    if (m_nValidSamples == 0) return 0.0f;
    return CalculateMax(m_pProcessingTimes, m_nValidSamples);
}

void CPerformanceMonitor::LogStatistics() const
{
    if (m_nValidSamples == 0) {
        CLogger::Get()->Write(FromPerformanceMonitor, LogNotice, 
                             "%s: No timing data available", m_pName);
        return;
    }
    
    CLogger::Get()->Write(FromPerformanceMonitor, LogNotice,
        "%s Performance Statistics (%u samples):", m_pName, m_nValidSamples);
    
    CLogger::Get()->Write(FromPerformanceMonitor, LogNotice,
        "  CPU Usage: %.2f%% avg, %.2f%% max",
        GetAverageCPUUsagePercent(), GetMaxCPUUsagePercent());
    
    CLogger::Get()->Write(FromPerformanceMonitor, LogNotice,
        "  Processing Time: %.2f μs avg, %.2f μs max",
        GetAverageProcessingTimeUs(), GetMaxProcessingTimeUs());
    
    CLogger::Get()->Write(FromPerformanceMonitor, LogNotice,
        "  Clock Cycles: %u avg, %u max",
        GetAverageClockCycles(), GetMaxClockCycles());
    
    // Calculate buffer time
    float bufferTimeUs = (float)m_nBufferSize * 1000000.0f / (float)m_nSampleRate;
    CLogger::Get()->Write(FromPerformanceMonitor, LogNotice,
        "  Buffer time: %.2f μs (%u samples @ %u Hz)",
        bufferTimeUs, m_nBufferSize, m_nSampleRate);
}

void CPerformanceMonitor::ResetStatistics()
{
    m_nCurrentIndex = 0;
    m_nValidSamples = 0;
    
    // Clear arrays
    for (unsigned i = 0; i < m_nMaxSamples; i++) {
        m_pClockCycles[i] = 0;
        m_pProcessingTimes[i] = 0.0f;
        m_pCPUUsages[i] = 0.0f;
    }
    
    // Reset last result
    m_LastResult.clockCycles = 0;
    m_LastResult.processingTimeUs = 0.0f;
    m_LastResult.cpuUsagePercent = 0.0f;
}

// Private helper methods

float CPerformanceMonitor::CalculateAverage(const float* data, unsigned count) const
{
    if (count == 0) return 0.0f;
    
    float sum = 0.0f;
    for (unsigned i = 0; i < count; i++) {
        sum += data[i];
    }
    return sum / (float)count;
}

float CPerformanceMonitor::CalculateMax(const float* data, unsigned count) const
{
    if (count == 0) return 0.0f;
    
    float maxVal = data[0];
    for (unsigned i = 1; i < count; i++) {
        if (data[i] > maxVal) {
            maxVal = data[i];
        }
    }
    return maxVal;
}

unsigned CPerformanceMonitor::CalculateAverage(const unsigned* data, unsigned count) const
{
    if (count == 0) return 0;
    
    unsigned long long sum = 0;
    for (unsigned i = 0; i < count; i++) {
        sum += data[i];
    }
    return (unsigned)(sum / count);
}

unsigned CPerformanceMonitor::CalculateMax(const unsigned* data, unsigned count) const
{
    if (count == 0) return 0;
    
    unsigned maxVal = data[0];
    for (unsigned i = 1; i < count; i++) {
        if (data[i] > maxVal) {
            maxVal = data[i];
        }
    }
    return maxVal;
}

float CPerformanceMonitor::ClockTicksToMicroseconds(unsigned ticks) const
{
    // Circle's CTimer::GetClockTicks() returns ticks at 1MHz (1 tick = 1 microsecond)
    // This is true for Pi 1, Pi Zero, and all other models when using the system timer
    return (float)ticks;
}

float CPerformanceMonitor::CalculateCPUUsage(float processingTimeUs, unsigned numSamples) const
{
    if (m_nSampleRate == 0 || numSamples == 0) {
        return 0.0f;
    }
    
    // Calculate the time available for processing this many samples
    float availableTimeUs = (float)numSamples * 1000000.0f / (float)m_nSampleRate;
    
    // CPU usage as percentage
    return (processingTimeUs / availableTimeUs) * 100.0f;
}