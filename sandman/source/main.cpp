#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>

#include <ncurses.h>
#include "wiringPi.h"

#include "config.h"
#include "control.h"
#include "logger.h"
#include "sound.h"
#include "speech_recognizer.h"
#include "timer.h"

#define DATADIR		AM_DATADIR
#define CONFIGDIR	AM_CONFIGDIR
#define TEMPDIR		AM_TEMPDIR

// Types
//

// Types of command tokens.
enum CommandTokenTypes
{
	COMMAND_TOKEN_INVALID = -1,

	COMMAND_TOKEN_SAND,
	COMMAND_TOKEN_MAN,
	COMMAND_TOKEN_HEAD,
	COMMAND_TOKEN_KNEE,
	COMMAND_TOKEN_ELEVATION,
	COMMAND_TOKEN_UP,
	COMMAND_TOKEN_DOWN,
	COMMAND_TOKEN_STOP,

	NUM_COMMAND_TOKEN_TYPES,
};

// Types of controls.
enum ControlTypes
{
	CONTROL_HEAD = 0,
	CONTROL_KNEE,
	CONTROL_ELEVATION,

	NUM_CONTROL_TYPES,
};

// Locals
//

// Names for each command token.
static char const* const s_CommandTokenNames[] = 
{
	"sand",			// COMMAND_TOKEN_SAND
	"man",			// COMMAND_TOKEN_MAN
	"head",			// COMMAND_TOKEN_HEAD
	"knee",			// COMMAND_TOKEN_KNEE
	"elevation",	// COMMAND_TOKEN_ELEVATION
	"up",			// COMMAND_TOKEN_UP
	"down",			// COMMAND_TOKEN_DOWN
	"stop",			// COMMAND_TOKEN_STOP
};

// The name for each control.
static char const* const s_ControlNames[] =
{
	"head",		// CONTROL_HEAD
	"knee",		// CONTROL_KNEE
	"elev",		// CONTROL_ELEVATION
};

// The controls.
static Control s_Controls[NUM_CONTROL_TYPES];

// Whether controls have been initialized.
static bool s_ControlsInitialized = false;

// The speech recognizer.
static SpeechRecognizer s_Recognizer;

// Whether to start as a daemon or terminal program.
static bool s_DaemonMode = false;

// Functions
//

template<class T>
T const& Min(T const& p_A, T const& p_B)
{
	return (p_A < p_B) ? p_A : p_B;
}

// Converts a string to lowercase.
//
// p_String:	The string to convert.
//
static void ConvertStringToLowercase(char* p_String)
{
	// Sanity check.
	if (p_String == NULL)
	{
		return;
	}

	char* l_CurrentLetter = p_String;
	while (*l_CurrentLetter != '\0')
	{
		// Convert this letter.
		*l_CurrentLetter = static_cast<char>(tolower(static_cast<unsigned int>(*l_CurrentLetter)));

		// Next letter.
		l_CurrentLetter++;
	}
}

// Initialize program components.
//
// returns:		True for success, false otherwise.
//
static bool Initialize()
{
	// Read the config.
	Config l_Config;
	if (l_Config.ReadFromFile(CONFIGDIR "sandman.conf") == false)
	{
		return false;
	}
	
	if (s_DaemonMode == true)
	{
		// Fork a child off of the parent process.
		pid_t l_ProcessID = fork();
		
		// Legitimate failure.
		if (l_ProcessID < 0)
		{
			return false;
		}
		
		// The parent gets the ID of the child and exits.
		if (l_ProcessID > 0)
		{
			return false;
		}
		
		// The child gets 0 and continues.
		
		// Allow file access.
		umask(0);
		
		// Initialize logging.
		if (LoggerInitialize(TEMPDIR "sandman.log", (s_DaemonMode == false)) == false)
		{
			return false;
		}

		// Need a new session ID.
		pid_t l_SessionID = setsid();
		
		if (l_SessionID < 0)
		{
			LoggerAddMessage("Failed to get new session ID for daemon.");
			return false;
		}
		
		// Change the current working directory.
		if (chdir(TEMPDIR) < 0)
		{
			LoggerAddMessage("Failed to change working directory to \"%s\" ID for daemon.", TEMPDIR);
			return false;
		}
		
		// Close stdin, stdou, stderr.
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		
		// Redirect stdin, stdout, stderr to /dev/null (this relies on them mapping to
		// the lowest numbered file descriptors).
		open("dev/null", O_RDWR);
		open("dev/null", O_RDWR);
		open("dev/null", O_RDWR);
	}
	else
	{
		// Initialize ncurses.
		initscr();

		// Don't wait for newlines, make getch non-blocking, and don't display input.
		cbreak();
		nodelay(stdscr, true);
		noecho();
		
		// Allow the window to scroll.
		scrollok(stdscr, true);
		idlok(stdscr, true);

		// Allow new-lines in the input.
		nonl();
			
		// Initialize logging.
		if (LoggerInitialize(TEMPDIR "sandman.log", (s_DaemonMode == false)) == false)
		{
			return false;
		}
	}

	// Initialize speech recognition.
	if (s_Recognizer.Initialize(l_Config.GetInputDeviceName(), l_Config.GetInputSampleRate(), 
		DATADIR "hmm/en_US/hub4wsj_sc_8k", DATADIR "lm/en_US/sandman.lm", DATADIR "dict/en_US/sandman.dic", 
		TEMPDIR "recognizer.log", l_Config.GetPostSpeechDelaySec()) == false)
	{
		return false;
	}

	LoggerAddMessage("Initializing GPIO support...");
	
	if (wiringPiSetup() == -1)
	{
		LoggerAddMessage("\tfailed");
		return false;
	}

	LoggerAddMessage("\tsucceeded");
	LoggerAddMessage("");
			
	// Initialize sound.
	if (SoundInitialize() == false)
	{
		return false;
	}

	// Initialize controls.
	for (unsigned int l_ControlIndex = 0; l_ControlIndex < NUM_CONTROL_TYPES; l_ControlIndex++)
	{
		s_Controls[l_ControlIndex].Initialize(s_ControlNames[l_ControlIndex], 2 * l_ControlIndex, 
			2 * l_ControlIndex + 1);
	}

	// Set control durations.
	Control::SetDurations(l_Config.GetControlMovingDurationMS(), l_Config.GetControlCoolDownDurationMS());
	
	// Enable all controls.
	Control::Enable(true);
	
	// Controls have been initialized.
	s_ControlsInitialized = true;
	
	// Play initialization speech.
	SoundAddToQueue(DATADIR "audio/initialized.wav");

	return true;
}

// Uninitialize program components.
//
static void Uninitialize()
{
	// Uninitialize the speech recognizer.
	s_Recognizer.Uninitialize();

	// Uninitialize sound.
	SoundUninitialize();
	
	if (s_ControlsInitialized == true)
	{
		// Disable all controls.
		Control::Enable(false);
	
		// Uninitialize controls.
		for (unsigned int l_ControlIndex = 0; l_ControlIndex < NUM_CONTROL_TYPES; l_ControlIndex++)
		{
			s_Controls[l_ControlIndex].Uninitialize();
		}
	}
	
	// Uninitialize logging.
	LoggerUninitialize();

	if (s_DaemonMode == false)
	{
		// Uninitialize ncurses.
		endwin();
	}
}

// Take a command string and turn it into a list of tokens.
//
// p_CommandTokenBufferSize:		(Output) How man tokens got put in the buffer.
// p_CommandTokenBuffer:			(Output) A buffer to hold the tokens.
// p_CommandTokenBufferCapacity:	The maximum the command token buffer can hold.
// p_CommandString:					The command string to tokenize.
//
static void TokenizeCommandString(unsigned int& p_CommandTokenBufferSize, CommandTokenTypes* p_CommandTokenBuffer,
	unsigned int const p_CommandTokenBufferCapacity, char const* p_CommandString)
{
	// Store token strings here.
	unsigned int const l_TokenStringBufferCapacity = 32;
	char l_TokenStringBuffer[l_TokenStringBufferCapacity];

	// Get the first token string start.
	char const* l_NextTokenStringStart = p_CommandString;

	while (l_NextTokenStringStart != NULL)
	{
		// Get the next token string end.
		char const* l_NextTokenStringEnd = strchr(l_NextTokenStringStart, ' ');

		// Get the token string length.
		unsigned int l_TokenStringLength = 0;
		if (l_NextTokenStringEnd != NULL)
		{
			l_TokenStringLength = l_NextTokenStringEnd - l_NextTokenStringStart;
		}
		else 
		{
			l_TokenStringLength = strlen(l_NextTokenStringStart);
		}

		// Copy the token string.
		unsigned int l_AmountToCopy = Min(l_TokenStringBufferCapacity - 1, l_TokenStringLength);
		strncpy(l_TokenStringBuffer, l_NextTokenStringStart, l_AmountToCopy);
		l_TokenStringBuffer[l_AmountToCopy] = '\0';

		// Make sure the token string is lowercase.
		ConvertStringToLowercase(l_TokenStringBuffer);

		// Match the token string to a token if possible.
		CommandTokenTypes l_Token = COMMAND_TOKEN_INVALID;

		for (unsigned int l_TokenType = 0; l_TokenType < NUM_COMMAND_TOKEN_TYPES; l_TokenType++)
		{
			// Compare the token string to its name.
			if (strcmp(l_TokenStringBuffer, s_CommandTokenNames[l_TokenType]) != 0)
			{
				continue;
			}

			// Found a match.
			l_Token = static_cast<CommandTokenTypes>(l_TokenType);
			break;
		}

		// Add the token to the buffer.
		if (p_CommandTokenBufferSize < p_CommandTokenBufferCapacity)
		{
			p_CommandTokenBuffer[p_CommandTokenBufferSize] = l_Token;
			p_CommandTokenBufferSize++;
		} 
		else
		{
			LoggerAddMessage("Voice command too long, tail will be ignored.");
		}


		// Get the next token string start (skip delimiter).
		if (l_NextTokenStringEnd != NULL)
		{
			l_NextTokenStringStart = l_NextTokenStringEnd + 1;
		}
		else
		{
			l_NextTokenStringStart = NULL;
		}
	}
}

// Parse the command tokens into commands.
//
// p_CommandTokenBufferSize:		(Input/Output) How man tokens got put in the buffer.
// p_CommandTokenBuffer:			(Input/Output) A buffer to hold the tokens.
//
void ParseCommandTokens(unsigned int& p_CommandTokenBufferSize, CommandTokenTypes* p_CommandTokenBuffer)
{
	// Parse command tokens.
	for (unsigned int l_TokenIndex = 0; l_TokenIndex < p_CommandTokenBufferSize; l_TokenIndex++)
	{
		// Look for the prefix.
		if (p_CommandTokenBuffer[l_TokenIndex] != COMMAND_TOKEN_SAND)
		{
			continue;
		}

		// Next token.
		l_TokenIndex++;
		if (l_TokenIndex >= p_CommandTokenBufferSize)
		{
			break;
		}

		// Look for the rest of the prefix.
		if (p_CommandTokenBuffer[l_TokenIndex] != COMMAND_TOKEN_MAN)
		{
			continue;
		}

		// Next token.
		l_TokenIndex++;
		if (l_TokenIndex >= p_CommandTokenBufferSize)
		{
			break;
		}

		// Parse commands.
		if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_HEAD)
		{
			// Next token.
			l_TokenIndex++;
			if (l_TokenIndex >= p_CommandTokenBufferSize)
			{
				break;
			}

			if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_UP)
			{
				s_Controls[CONTROL_HEAD].SetDesiredAction(Control::ACTION_MOVING_UP);
			}
			else if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_DOWN)
			{
				s_Controls[CONTROL_HEAD].SetDesiredAction(Control::ACTION_MOVING_DOWN);
			}
		}
		else if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_KNEE)
		{
			// Next token.
			l_TokenIndex++;
			if (l_TokenIndex >= p_CommandTokenBufferSize)
			{
				break;
			}

			if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_UP)
			{
				s_Controls[CONTROL_KNEE].SetDesiredAction(Control::ACTION_MOVING_UP);
			}
			else if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_DOWN)
			{
				s_Controls[CONTROL_KNEE].SetDesiredAction(Control::ACTION_MOVING_DOWN);
			}
		}
		else if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_ELEVATION)
		{
			// Next token.
			l_TokenIndex++;
			if (l_TokenIndex >= p_CommandTokenBufferSize)
			{
				break;
			}

			if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_UP)
			{
				s_Controls[CONTROL_ELEVATION].SetDesiredAction(Control::ACTION_MOVING_UP);
			}
			else if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_DOWN)
			{
				s_Controls[CONTROL_ELEVATION].SetDesiredAction(Control::ACTION_MOVING_DOWN);
			}
		}
		else if (p_CommandTokenBuffer[l_TokenIndex] == COMMAND_TOKEN_STOP)
		{
			// Stop controls.
			for (unsigned int l_ControlIndex = 0; l_ControlIndex < NUM_CONTROL_TYPES; l_ControlIndex++)
			{
				s_Controls[l_ControlIndex].SetDesiredAction(Control::ACTION_STOPPED);
			}
		}
	}

	// All tokens parsed.
	p_CommandTokenBufferSize = 0;
}

// Get keyboard input.
//
// p_KeyboardInputBuffer:			(input/output) The input buffer.
// p_KeyboardInputBufferSize:		(input/output) How much of the input buffer is in use.
// p_KeyboardInputBufferCapacity:	The capacity of the input buffer.
//
// returns:		True if the quit command was processed, false otherwise.
//
static bool ProcessKeyboardInput(char* p_KeyboardInputBuffer, unsigned int& p_KeyboardInputBufferSize, 
	unsigned int const p_KeyboardInputBufferCapacity)
{
	// Try to get keyboard commands.
	int l_InputKey = getch();
	if ((l_InputKey == ERR) || (isascii(l_InputKey) == false))
	{
		return false;
	}
	
	// Get the character.
	char l_NextChar = static_cast<char>(l_InputKey);

	// Accumulate characters until we get a terminating character or we run out of space.
	if ((l_NextChar != '\r') && (p_KeyboardInputBufferSize < (p_KeyboardInputBufferCapacity - 1)))
	{
		p_KeyboardInputBuffer[p_KeyboardInputBufferSize] = l_NextChar;
		p_KeyboardInputBufferSize++;
		return false;
	}

	// Terminate the command.
	p_KeyboardInputBuffer[p_KeyboardInputBufferSize] = '\0';

	// Echo the command back.
	LoggerAddMessage("Keyboard command input: \"%s\"", p_KeyboardInputBuffer);

	// Parse a command.

	// Store command tokens here.
	unsigned int const l_CommandTokenBufferCapacity = 32;
	CommandTokenTypes l_CommandTokenBuffer[l_CommandTokenBufferCapacity];
	unsigned int l_CommandTokenBufferSize = 0;

	// Tokenize the speech.
	TokenizeCommandString(l_CommandTokenBufferSize, l_CommandTokenBuffer, l_CommandTokenBufferCapacity,
		p_KeyboardInputBuffer);

	// Parse command tokens.
	ParseCommandTokens(l_CommandTokenBufferSize, l_CommandTokenBuffer);

	// Prepare for a new command.
	p_KeyboardInputBufferSize = 0;

	if (strcmp(p_KeyboardInputBuffer, "quit") == 0)
	{
		 return true;
	}
	
	return false;
}

int main(int argc, char** argv)
{
	// Deal with command line arguments.
	for (unsigned int l_ArgumentIndex = 0; l_ArgumentIndex < argc; l_ArgumentIndex++)
	{
		// Start as a daemon?
		if (strcmp(argv[l_ArgumentIndex], "--daemon") == 0)
		{
			s_DaemonMode = true;
		}
	}
	
	// Initialization.
	if (Initialize() == false)
	{
		Uninitialize();
		return 0;
	}

	// Store a keyboard input here.
	unsigned int const l_KeyboardInputBufferCapacity = 128;
	char l_KeyboardInputBuffer[l_KeyboardInputBufferCapacity];
	unsigned int l_KeyboardInputBufferSize = 0;

	bool l_Done = false;
	while (l_Done == false)
	{
		// We're gonna track the framerate.
		Time l_FrameStartTime;
		TimerGetCurrent(l_FrameStartTime);
		
		if (s_DaemonMode == false)
		{
			// Process keyboard input.
			l_Done = ProcessKeyboardInput(l_KeyboardInputBuffer, l_KeyboardInputBufferSize, 
				l_KeyboardInputBufferCapacity);
		}

		// Process speech recognition.
		char const* l_RecognizedSpeech = NULL;
		if (s_Recognizer.Process(l_RecognizedSpeech) == false)
		{
			LoggerAddMessage("Error during speech recognition.");
			l_Done = true;
		}

		if (l_RecognizedSpeech != NULL)
		{
			// Parse a command.

			// Store command tokens here.
			unsigned int const l_CommandTokenBufferCapacity = 32;
			CommandTokenTypes l_CommandTokenBuffer[l_CommandTokenBufferCapacity];
			unsigned int l_CommandTokenBufferSize = 0;

			// Tokenize the speech.
			TokenizeCommandString(l_CommandTokenBufferSize, l_CommandTokenBuffer, l_CommandTokenBufferCapacity,
				l_RecognizedSpeech);

			// Parse command tokens.
			ParseCommandTokens(l_CommandTokenBufferSize, l_CommandTokenBuffer);
		}

		// Process controls.
		for (unsigned int l_ControlIndex = 0; l_ControlIndex < NUM_CONTROL_TYPES; l_ControlIndex++)
		{
			s_Controls[l_ControlIndex].Process();
		}

		// Process sound.
		SoundProcess();
		
		// Get the duration of the frame in nanoseconds.
		Time l_FrameEndTime;
		TimerGetCurrent(l_FrameEndTime);
		
		float const l_FrameDurationMS = TimerGetElapsedMilliseconds(l_FrameStartTime, l_FrameEndTime);
		unsigned long const l_FrameDurationNS = static_cast<unsigned long>(l_FrameDurationMS * 1.0e6f);
		
		// If the frame is shorter than the duration corresponding to the desired framerate, sleep the
		// difference off.
		unsigned long const l_TargetFrameDurationNS = 1000000000 / 60;
		
		if (l_FrameDurationNS < l_TargetFrameDurationNS)
		{
			timespec l_SleepTime;
			l_SleepTime.tv_sec = 0;
			l_SleepTime.tv_nsec = l_TargetFrameDurationNS - l_FrameDurationNS;
			
			timespec l_RemainingTime;
			nanosleep(&l_SleepTime, &l_RemainingTime);
		}
	}

	// Cleanup.
	Uninitialize();
	return 0;
}
