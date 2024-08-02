#pragma once

#include <cstdarg>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#include "ncurses_ui.h"

class Logger
{

private:

	std::ostringstream m_Buffer;
	std::ostream& m_OutputStream;
	bool m_ScreenEcho{ false };

public:

	[[nodiscard]] Logger(std::ostream& outputStream): m_Buffer(), m_OutputStream(outputStream) {}

	template <typename T, typename... ParamsT>
	[[gnu::always_inline]] inline void Write(T const& firstArg, ParamsT const&... args);

	static constexpr char kInterpolationIndicator{'$'}, kEscapeIndicator{'\\'};

	void InterpolateWrite(std::string_view const formatString);

	template <typename T, typename... ParamsT>
	void InterpolateWrite(std::string_view formatString, T const& firstArg, ParamsT const&... args);

private:

	static std::mutex ms_Mutex;
	static std::ofstream ms_File;
	static Logger ms_GlobalLogger;

public:

	[[gnu::always_inline]] [[nodiscard]] inline static bool& GetScreenEchoFlag()
	{
		return ms_GlobalLogger.m_ScreenEcho;
	}

	[[nodiscard]] static bool Initialize(char const* const logFileName);

	static void Uninitialize();

	template <typename... ParamsT>
	[[gnu::always_inline]] inline static void WriteLine(ParamsT const&... args);

	template <NCurses::ColorIndex kColor = NCurses::ColorIndex::None,
				 std::size_t kLogStringBufferCapacity = 2048u>
	[[gnu::format(printf, 1, 0)]] static bool FormatWriteLine(char const* format,
																				 std::va_list argumentList);

	template <NCurses::ColorIndex kColor = NCurses::ColorIndex::None,
				 std::size_t kLogStringBufferCapacity = 2048u>
	[[gnu::format(printf, 1, 2)]] static bool FormatWriteLine(char const* format, ...);

	template <typename... ParamsT>
	static void InterpolateWriteLine(std::string_view const formatString, ParamsT const&... args);
};

#include "logger.inl"
#include "logger_global.inl"
