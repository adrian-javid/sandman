#pragma once

#include <cstdarg>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <fstream>
#include <string>

#include "ncurses_ui.h"

namespace Logger
{
	extern bool g_ScreenEcho;

	class Self final
	{
		static std::mutex s_Mutex;
		static std::ofstream s_File;
		static std::ostringstream s_Buffer;

		Self() = delete; ~Self() = delete;

		template <typename T, typename... ParamsT>
		[[gnu::always_inline]] static inline void Write(T const& p_FirstArg, ParamsT const&... p_Args)
		{
			s_Buffer << p_FirstArg;

			if constexpr (sizeof...(p_Args) > 0u)
			{
				Write(p_Args...);
			}
			else
			{
				auto const string(s_Buffer.str());

				if (g_ScreenEcho) NCurses::LoggingWindow::Print(string);

				s_File << string;
				s_Buffer.str("");
				s_Buffer.clear();
			}
		}

		template <typename... ParamsT>
		friend void WriteLine(ParamsT const&...);

		friend bool Initialize(char const* const);

		friend void Uninitialize();
	};

	bool Initialize(char const* const p_LogFileName);

	void Uninitialize();

	template <typename... ParamsT>
	[[gnu::always_inline]] inline void WriteLine(ParamsT const&... p_Args)
	{
		auto const l_TimePoint(std::chrono::system_clock::now());

		auto const l_ArithmeticTimeValue{ std::chrono::system_clock::to_time_t(l_TimePoint) };
		static_assert(std::is_arithmetic_v<decltype(l_ArithmeticTimeValue)>);

		std::lock_guard const l_Lock(Self::s_Mutex);
		Self::s_Buffer << std::put_time(std::localtime(&l_ArithmeticTimeValue), "%Y/%m/%d %H:%M:%S %Z");
		Self::Write(' ', p_Args..., '\n');
	}

	[[gnu::format(printf, 1, 2)]] bool FormatWriteLine(char const* p_Format, ...);
}
