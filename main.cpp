#define LOGGING false


#include <windows.h>
#pragma comment(lib, "version.lib")

#if LOGGING
#include <iostream>
#endif

#include <DetourHookNoreg.hpp>
#include <Module.hpp>
#include <pattern_macros.hpp>
#include <string.hpp>
#include <unicode.hpp>
#include <Uri.hpp>

using namespace soup;


static std::string server_host = "127.0.0.1";
static uint16_t http_port = 80;

static char build_version[16] = { 0 };


// U7-U18
struct GameString
{
	char* ptr;
	size_t len;
	size_t ownership;

	void setUnownedData(const char* data, size_t len) noexcept
	{
		this->ptr = (char*)data;
		this->len = len;
		this->ownership = -1;
	}
};

static DetourHookNoreg parse_arguments_hook;
static bool injected_args = false;

static void __thiscall parse_arguments_detour(void* a1, GameString* str, void* a3)
{
	if (!injected_args)
	{
		injected_args = true;

		//char args_to_inject[] = "-fullscreen:0 -cluster:public -webserver:http://localhost/api/"; // U7-U8
		char args_to_inject[] = "-fullscreen:0 -cluster:public";

		GameString tmp;
		tmp.setUnownedData(args_to_inject, sizeof(args_to_inject) - 1);
		reinterpret_cast<decltype(&parse_arguments_detour)>(parse_arguments_hook.original)(a1, &tmp, a3);
	}

	reinterpret_cast<decltype(&parse_arguments_detour)>(parse_arguments_hook.original)(a1, str, a3);
}


static DetourHookNoreg init_cache_fetching_hook;

static void __cdecl init_cache_fetching_detour(void* a1, bool a2, bool is_stripped, bool a4, bool a5, bool a6, uint8_t a7)
{
	is_stripped = false;
	reinterpret_cast<decltype(&init_cache_fetching_detour)>(init_cache_fetching_hook.original)(a1, a2, is_stripped, a4, a5, a6, a7);
}


static DetourHookNoreg name_lookup_hook;

static bool __cdecl name_lookup_detour(void* out, GameString* name, bool a3)
{
	std::string_view sv(name->ptr, name->len);
#if LOGGING
	//std::cout << "name_lookup: " << sv << std::endl;
#endif
	std::string override;
	if (sv.find("warframe.com") != std::string::npos)
	{
		override = server_host;
		if (const char* sep = strchr(name->ptr, ':'))
		{
			override.append(sep);
		}
		name->setUnownedData(override.data(), override.size());
	}
	// TODO: Request tunables for correct NRS/IRC addressses
	return reinterpret_cast<decltype(&name_lookup_detour)>(name_lookup_hook.original)(out, name, a3);
}


static DetourHookNoreg game_http_request_hook;

static void* __thiscall game_http_request_detour(void* a1, uintptr_t request)
{
	GameString& request_url = *reinterpret_cast<GameString*>(request + 0x00);
	GameString& request_body = *reinterpret_cast<GameString*>(request + 0x2C); // Offset for 2014.04.23.18.00. Very likely wrong for other versions.

#if LOGGING
	std::cout << "game_http_request for " << std::string_view(request_url.ptr, request_url.len) << std::endl;
	/*if (request_body.len)
	{
		std::cout << std::string_view(request_body.ptr, request_body.len) << std::endl;
	}*/
#endif

	Uri uri((const char*)request_url.ptr);
	uri.scheme = "http";
	uri.host = server_host;
	uri.port = http_port;

	if (uri.path == "/api/login.php" || uri.path == "/dynamic/worldState.php")
	{
		if (build_version[0])
		{
			if (!uri.query.empty())
			{
				uri.query.push_back('&');
			}
			uri.query.append("buildLabel=");
			uri.query.append(build_version, 16);
			uri.query.push_back('/');
		}
	}

	std::string url_buf = uri.toString();
	request_url.setUnownedData(url_buf.data(), url_buf.size());

#if true // Censor process list
	std::string body_buf;
	{
		std::string_view body(request_body.ptr, request_body.len);
		if (auto pos = body.find(R"("processes":")"); pos != std::string::npos)
		{
			if (auto epos = body.find('"', pos + 13); epos != std::string::npos)
			{
				body_buf = body.substr(0, pos + 17);
				body_buf.append("W0RFXVN0ZXZlIGxpa2VzIGJpZyBidXR0cw");
				body_buf.append(body.substr(epos));
				request_body.setUnownedData(body_buf.data(), body_buf.size());
			}
		}
	}
#endif

	return reinterpret_cast<decltype(&game_http_request_detour)>(game_http_request_hook.original)(a1, request);
}


/*static DetourHookNoreg substitute_keywords_hook;

static void substitute_keywords_detour(GameString* result, void* substitutions)
{
	std::cout << "substitute_keywords: " << std::string(result->ptr, result->len) << std::endl;
	return reinterpret_cast<decltype(&substitute_keywords_detour)>(substitute_keywords_hook.original)(result, substitutions);
}*/


#define EXE_NAME "Warframe.exe"

BOOL DllMain(HMODULE hmod, DWORD reason, PVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
#if LOGGING
		AllocConsole();
		SetConsoleTitleA("32-Bitstrapper");
		{
			FILE* f;
			freopen_s(&f, "CONIN$", "r", stdin);
			freopen_s(&f, "CONOUT$", "w", stderr);
			freopen_s(&f, "CONOUT$", "w", stdout);
		}
		SetConsoleCP(CP_UTF8);
		SetConsoleOutputCP(CP_UTF8);
#endif

		{
			DWORD dwHandle;
			DWORD version_info_size = GetFileVersionInfoSizeA(EXE_NAME, &dwHandle);

			void* data = malloc(version_info_size);
			GetFileVersionInfoA(EXE_NAME, 0, version_info_size, data);

			LPVOID value_data;
			UINT value_size;
			VerQueryValueA(data, "\\StringFileInfo\\040904B0\\ProductVersion", &value_data, &value_size);
			memcpy(build_version, value_data, 16);

			free(data);
		}
#if LOGGING
		std::cout << "build_version = " << std::string(build_version, 16) << std::endl;
#endif

		{
			std::vector<std::string> args{};
			{
				int argc;
				wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
				for (int i = 0; i != argc; ++i)
				{
					args.emplace_back(unicode::utf16_to_utf8<std::wstring>(argv[i]));
				}
			}
			for (const auto& arg : args)
			{
				if (arg.size() > 15 && arg.substr(0, 15) == "-owfServerHost:")
				{
					server_host = arg.substr(15);
				}
				else if (arg.size() > 13 && arg.substr(0, 13) == "-owfHttpPort:")
				{
					string::toIntOpt<uint16_t>(arg.substr(13)).consume(http_port);
				}
			}
		}

		{
			SIG_INST("89 4C 24 ? 52 B9 ? ? ? ? 89 44 24 ? E8"); // 2014.04.23.18.00, 2013.09.24.17.38, 2013.08.14.11.28, 2013.07.15.20.46
			auto parse_arguments_callsite = Module(nullptr).range.scan(sig_inst);
#if LOGGING
			std::cout << "parse_arguments_callsite = " << parse_arguments_callsite.as<void*>() << std::endl;
#endif
			if (parse_arguments_callsite)
			{
				auto parse_arguments = parse_arguments_callsite.add(15).rip().as<void*>();

				parse_arguments_hook.detour = reinterpret_cast<void*>(&parse_arguments_detour);
				parse_arguments_hook.target = parse_arguments;
				parse_arguments_hook.create();
				parse_arguments_hook.enable();
			}
		}

		// 2014.04.23.18.00 needs this to even boot
		{
			//SIG_INST("83 EC 14 55 56 57 33 FF 33 ED"); // 2013.09.24.17.38, 2013.08.14.11.28, 2013.07.15.20.46 (Fails in 2012.12.31.16.20 but is also not needed there)
			SIG_INST("83 EC 14 53 56 57 33 FF B9"); // 2014.04.23.18.00
			const auto init_cache_fetching = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			std::cout << "init_cache_fetching = " << init_cache_fetching << std::endl;
#endif
			if (init_cache_fetching)
			{
				init_cache_fetching_hook.detour = reinterpret_cast<void*>(&init_cache_fetching_detour);
				init_cache_fetching_hook.target = init_cache_fetching;
				init_cache_fetching_hook.create();
				init_cache_fetching_hook.enable();
			}
		}

		// To avoid wasted time on NRS name resolution in 2014.04.23.18.00
		{
			SIG_INST("81 EC 64 01 00 00 A1 ? ? ? ? 33 C4 89 84 24 60 01 00 00 8B 84 24 6C 01 00 00 53 33 DB"); // 2014.04.23.18.00
			const auto name_lookup = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			std::cout << "name_lookup = " << name_lookup << std::endl;
#endif
			if (name_lookup)
			{
				name_lookup_hook.detour = reinterpret_cast<void*>(&name_lookup_detour);
				name_lookup_hook.target = name_lookup;
				name_lookup_hook.create();
				name_lookup_hook.enable();
			}
		}

		// To redirect requests to our custom webserver for U8 and beyond
		{
			SIG_INST("81 EC 8C 0A 00 00 A1 ? ? ? ? 33 C4 89 84 24 88 0A 00 00"); // 2014.04.23.18.00, 2013.09.24.17.38, 2013.08.14.11.28, 2013.07.15.20.46
			auto game_http_request = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			std::cout << "game_http_request = " << game_http_request << std::endl;
#endif
			//if (game_http_request) // Mandatory hook :)
			{
				game_http_request_hook.detour = reinterpret_cast<void*>(&game_http_request_detour);
				game_http_request_hook.target = game_http_request;
				game_http_request_hook.create();
				game_http_request_hook.enable();
			}
		}

		// Does not seem to get called :(
		/*{
			SIG_INST("8B 44 24 64 53 33 DB 89 44 24 04");
			auto substitute_keywords = Module(nullptr).range.scan(sig_inst).as<void*>();
#if LOGGING
			std::cout << "substitute_keywords = " << substitute_keywords << std::endl;
#endif
			if (substitute_keywords)
			{
				substitute_keywords_hook.detour = reinterpret_cast<void*>(&substitute_keywords_detour);
				substitute_keywords_hook.target = substitute_keywords;
				substitute_keywords_hook.create();
				substitute_keywords_hook.enable();
			}
		}*/
	}
	return TRUE;
}
