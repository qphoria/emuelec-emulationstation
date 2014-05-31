//EmulationStation, a graphical front-end for ROM browsing. Created by Alec "Aloshi" Lofquist.
//http://www.aloshi.com

#include <SDL.h>
#include <iostream>
#include <iomanip>
#include "Renderer.h"
#include "views/ViewController.h"
#include "SystemData.h"
#include <boost/filesystem.hpp>
#include "guis/GuiDetectDevice.h"
#include "guis/GuiMsgBox.h"
#include "AudioManager.h"
#include "platform.h"
#include "Log.h"
#include "Window.h"
#include "EmulationStation.h"
#include "Settings.h"
#include "ScraperCmdLine.h"
#include <sstream>

namespace fs = boost::filesystem;

bool scrape_cmdline = false;

// we do this separately from the other args because the other args might call Settings::getInstance()
// which will load the file getHomePath() + "/.emulationstation/es_settings.cfg", and getHomePath() needs to be accurate by then
bool parseArgsForHomeDir(int argc, char* argv[])
{
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--home-path") == 0)
		{
			if(i >= argc - 1)
			{
				std::cerr << "No home path specified!\n";
				return false;
			}
			setHomePathOverride(argv[i + 1]);
		}
	}

	return true;
}

bool parseArgs(int argc, char* argv[], unsigned int* width, unsigned int* height)
{
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--resolution") == 0)
		{
			if(i >= argc - 2)
			{
				std::cerr << "Invalid resolution supplied.";
				return false;
			}

			*width = atoi(argv[i + 1]);
			*height = atoi(argv[i + 2]);
			i += 2; // skip the argument value
		}else if(strcmp(argv[i], "--gamelist-only") == 0)
		{
			Settings::getInstance()->setBool("ParseGamelistOnly", true);
		}else if(strcmp(argv[i], "--ignore-gamelist") == 0)
		{
			Settings::getInstance()->setBool("IgnoreGamelist", true);
		}else if(strcmp(argv[i], "--draw-framerate") == 0)
		{
			Settings::getInstance()->setBool("DrawFramerate", true);
		}else if(strcmp(argv[i], "--no-exit") == 0)
		{
			Settings::getInstance()->setBool("ShowExit", false);
		}else if(strcmp(argv[i], "--debug") == 0)
		{
			Settings::getInstance()->setBool("Debug", true);
			Log::setReportingLevel(LogDebug);
		}else if(strcmp(argv[i], "--windowed") == 0)
		{
			Settings::getInstance()->setBool("Windowed", true);
		}else if(strcmp(argv[i], "--scrape") == 0)
		{
			scrape_cmdline = true;
		}else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
			std::cout << "EmulationStation, a graphical front-end for ROM browsing.\n";
			std::cout << "Written by Alec \"Aloshi\" Lofquist.\n";
			std::cout << "Command line arguments:\n";
			std::cout << "--resolution [width] [height]	try and force a particular resolution\n";
			std::cout << "--gamelist-only			skip automatic game detection, only read from gamelist.xml\n";
			std::cout << "--ignore-gamelist		ignore the gamelist (useful for troubleshooting)\n";
			std::cout << "--draw-framerate		display the framerate\n";
			std::cout << "--no-exit			don't show the exit option in the menu\n";
			std::cout << "--debug				even more logging\n";
			std::cout << "--scrape			scrape using command line interface\n";
			std::cout << "--windowed			not fullscreen, should be used in conjunction with --resolution\n";
			std::cout << "--home-path [path]		use [path] instead of the \"home\" environment variable (for portable installations)\n";
			std::cout << "--help, -h			summon a sentient, angry tuba\n\n";
			std::cout << "More information available in README.md.\n";
			return false; //exit after printing help
		}
	}

	return true;
}

bool verifyHomeFolderExists()
{
	//make sure the config directory exists
	std::string home = getHomePath();
	std::string configDir = home + "/.emulationstation";
	if(!fs::exists(configDir))
	{
		std::cout << "Creating config directory \"" << configDir << "\"\n";
		fs::create_directory(configDir);
		if(!fs::exists(configDir))
		{
			std::cerr << "Config directory could not be created!\n";
			return false;
		}
	}

	return true;
}

// Returns true if everything is OK, 
bool loadSystemConfigFile(const char** errorString)
{
	*errorString = NULL;

	if(!SystemData::loadConfig())
	{
		LOG(LogError) << "Error while parsing systems configuration file!";
		*errorString = "IT LOOKS LIKE YOUR SYSTEMS CONFIGURATION FILE HAS NOT BEEN SET UP OR IS INVALID. YOU'LL NEED TO DO THIS BY HAND, UNFORTUNATELY.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.";
		return false;
	}

	if(SystemData::sSystemVector.size() == 0)
	{
		LOG(LogError) << "No systems found! Does at least one system have a game present? (check that extensions match!)\n(Also, make sure you've updated your es_systems.cfg for XML!)";
		*errorString = "WE CAN'T FIND ANY SYSTEMS!\n"
			"CHECK THAT YOUR PATHS ARE CORRECT IN THE SYSTEMS CONFIGURATION FILE, "
			"AND YOUR GAME DIRECTORY HAS AT LEAST ONE GAME WITH THE CORRECT EXTENSION.\n\n"
			"VISIT EMULATIONSTATION.ORG FOR MORE INFORMATION.";
		return false;
	}

	return true;
}

//called on exit, assuming we get far enough to have the log initialized
void onExit()
{
	Log::close();
}

int main(int argc, char* argv[])
{
	unsigned int width = 0;
	unsigned int height = 0;

	if(!parseArgsForHomeDir(argc, argv)) // returns false if an invalid path was specified
		return 1;

	if(!parseArgs(argc, argv, &width, &height))
		return 0;

	//if ~/.emulationstation doesn't exist and cannot be created, bail
	if(!verifyHomeFolderExists())
		return 1;

	//start the logger
	Log::open();
	LOG(LogInfo) << "EmulationStation - v" << PROGRAM_VERSION_STRING << ", built " << PROGRAM_BUILT_STRING;

	//always close the log on exit
	atexit(&onExit);

	Window window;
	if(!scrape_cmdline)
	{
		if(!window.init(width, height))
		{
			LOG(LogError) << "Window failed to initialize!";
			return 1;
		}

		window.renderLoadingScreen();
	}

	const char* errorMsg = NULL;
	if(!loadSystemConfigFile(&errorMsg))
	{
		// something went terribly wrong
		if(errorMsg == NULL)
		{
			LOG(LogError) << "Unknown error occured while parsing system config file.";
			if(!scrape_cmdline)
				Renderer::deinit();
			return 1;
		}

		// we can't handle es_systems.cfg file problems inside ES itself, so display the error message then quit
		window.pushGui(new GuiMsgBox(&window,
			errorMsg,
			"QUIT", [] { 
				SDL_Event* quit = new SDL_Event();
				quit->type = SDL_QUIT;
				SDL_PushEvent(quit);
			}));
	}

	//run the command line scraper then quit
	if(scrape_cmdline)
	{
		return run_scraper_cmdline();
	}

	//dont generate joystick events while we're loading (hopefully fixes "automatically started emulator" bug)
	SDL_JoystickEventState(SDL_DISABLE);

	// preload what we can right away instead of waiting for the user to select it
	// this makes for no delays when accessing content, but a longer startup time
	window.getViewController()->preload();

	//choose which GUI to open depending on if an input configuration already exists
	if(errorMsg == NULL)
	{
		if(fs::exists(InputManager::getConfigPath()) && InputManager::getInstance()->getNumConfiguredDevices() > 0)
		{
			window.getViewController()->goToStart();
		}
		else{
			window.pushGui(new GuiDetectDevice(&window, true));
		}
	}

	//generate joystick events since we're done loading
	SDL_JoystickEventState(SDL_ENABLE);

	bool sleeping = false;
	unsigned int timeSinceLastEvent = 0;
	int lastTime = SDL_GetTicks();
	bool running = true;

	while(running)
	{
		SDL_Event event;
		while(SDL_PollEvent(&event))
		{
			switch(event.type)
			{
				case SDL_JOYHATMOTION:
				case SDL_JOYBUTTONDOWN:
				case SDL_JOYBUTTONUP:
				case SDL_KEYDOWN:
				case SDL_KEYUP:
				case SDL_JOYAXISMOTION:
				case SDL_TEXTINPUT:
				case SDL_TEXTEDITING:
				case SDL_JOYDEVICEADDED:
				case SDL_JOYDEVICEREMOVED:
					if(InputManager::getInstance()->parseEvent(event, &window))
					{
						sleeping = false;
						timeSinceLastEvent = 0;
					}
					break;
				case SDL_QUIT:
					running = false;
					break;
			}
		}

		if(sleeping)
		{
			lastTime = SDL_GetTicks();
			SDL_Delay(1); //this doesn't need to be accurate
			continue;
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;
		lastTime = curTime;

		//cap deltaTime at 1000
		if(deltaTime > 1000 || deltaTime < 0)
			deltaTime = 1000;

		window.update(deltaTime);
		window.render();
		
		//sleep if we're past our threshold
		//sleeping entails setting a flag to start skipping frames
		//and initially drawing a black semi-transparent rect to dim the screen
		timeSinceLastEvent += deltaTime;
		if(timeSinceLastEvent >= (unsigned int)Settings::getInstance()->getInt("ScreenSaverTime") && Settings::getInstance()->getInt("ScreenSaverTime") != 0 && window.getAllowSleep())
		{
			sleeping = true;
			timeSinceLastEvent = 0;
			Renderer::setMatrix(Eigen::Affine3f::Identity());

			unsigned char opacity = Settings::getInstance()->getString("ScreenSaverBehavior") == "dim" ? 0xA0 : 0xFF;
			Renderer::drawRect(0, 0, Renderer::getScreenWidth(), Renderer::getScreenHeight(), 0x00000000 | opacity);
		}

		Renderer::swapBuffers();

		Log::flush();
	}

	while(window.peekGui() != window.getViewController())
		delete window.peekGui();
	window.deinit();

	SystemData::deleteSystems();

	LOG(LogInfo) << "EmulationStation cleanly shutting down.";

	return 0;
}
