#pragma once

#include <stdint.h>

// Types
//

// Represents a point in time useful for elapsed time.
struct Time
{
	// The portion of the time in seconds.
	uint64_t m_seconds = 0;
	
	// The portion of the time in nanoseconds.
	uint64_t m_nanoseconds = 0;	

	friend auto operator<(const Time& left, const Time& right)
	{
		if (left.m_seconds != right.m_seconds)
		{
			return left.m_seconds < right.m_seconds;
		}

		return left.m_nanoseconds < right.m_nanoseconds;
	}

	friend auto operator>(const Time& left, const Time& right)
	{
		return right < left;
	}

};

// Functions
//

// Get the current time.
//
// time:	(Output) The current time.
//
void TimerGetCurrent(Time& time);

// Get the elapsed time in milliseconds between two times.
// Note: Will return -1 if the end time is less than the start time.
//
// startTime:	Start time.
// endTime:		End time.
//
float TimerGetElapsedMilliseconds(Time const& startTime, Time const& endTime);
