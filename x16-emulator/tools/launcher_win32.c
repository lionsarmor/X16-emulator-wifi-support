// Generic "double-click to play" Windows launcher stub for X16 app bundles
// produced by tools/bundle-x16-app.sh.
//
// Compiled once (GUI subsystem: no console window flash) and copied into
// every bundle as <AppName>.exe. At runtime it:
//   1. Finds its own directory (wherever the user put the bundle).
//   2. Reads launcher.cfg next to it for the PRG filename to boot, if any.
//   3. Launches x16emu.exe from that same directory with -wifi and the
//      right -fsroot/-prg/-run flags pointing at the bundled "app" folder.
//   4. Reports a clear message box on failure instead of silently doing
//      nothing, since there's no console to print errors to.
//
// This file is intentionally app-agnostic: nothing here is specific to any
// one X16 program. Per-app behavior comes entirely from launcher.cfg.

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>

static void FatalBox(const wchar_t *msg)
{
	MessageBoxW(NULL, msg, L"X16 Launcher", MB_OK | MB_ICONERROR);
}

// Reads a "KEY=value" line for the given key out of launcher.cfg.
// Returns 1 and fills 'out' (size 'outCap' wide chars) on success, else 0.
static int ReadConfigValue(const wchar_t *dir, const wchar_t *key, wchar_t *out, size_t outCap)
{
	wchar_t path[MAX_PATH];
	_snwprintf(path, MAX_PATH, L"%s\\launcher.cfg", dir);

	FILE *f = _wfopen(path, L"r, ccs=UTF-8");
	if (!f) {
		return 0;
	}

	wchar_t line[1024];
	int found = 0;
	size_t keyLen = wcslen(key);
	while (fgetws(line, 1024, f)) {
		if (wcsncmp(line, key, keyLen) == 0 && line[keyLen] == L'=') {
			wchar_t *value = line + keyLen + 1;
			// Strip trailing newline/carriage return.
			size_t len = wcslen(value);
			while (len > 0 && (value[len - 1] == L'\n' || value[len - 1] == L'\r')) {
				value[--len] = L'\0';
			}
			wcsncpy(out, value, outCap - 1);
			out[outCap - 1] = L'\0';
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	(void)hInstance; (void)hPrevInstance; (void)pCmdLine; (void)nCmdShow;

	wchar_t exePath[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
		FatalBox(L"Could not determine the launcher's own location.");
		return 1;
	}

	wchar_t dir[MAX_PATH];
	wcsncpy(dir, exePath, MAX_PATH);
	wchar_t *lastSlash = wcsrchr(dir, L'\\');
	if (lastSlash) {
		*lastSlash = L'\0';
	}

	wchar_t emuPath[MAX_PATH];
	_snwprintf(emuPath, MAX_PATH, L"%s\\x16emu.exe", dir);
	if (GetFileAttributesW(emuPath) == INVALID_FILE_ATTRIBUTES) {
		wchar_t msg[MAX_PATH + 128];
		_snwprintf(msg, MAX_PATH + 128,
			L"x16emu.exe is missing from this bundle:\n%s\n\nThe bundle may be incomplete or moved apart from its files.",
			emuPath);
		FatalBox(msg);
		return 1;
	}

	wchar_t romPath[MAX_PATH];
	_snwprintf(romPath, MAX_PATH, L"%s\\rom.bin", dir);

	wchar_t appDir[MAX_PATH];
	_snwprintf(appDir, MAX_PATH, L"%s\\app", dir);

	wchar_t prgName[512];
	int hasPrg = ReadConfigValue(dir, L"PRG", prgName, 512);

	// Build the x16emu.exe command line. Quote every path since bundles may
	// live under directories with spaces (e.g. "DESK COMMANDER").
	wchar_t cmdLine[4096];
	if (hasPrg && prgName[0] != L'\0') {
		_snwprintf(cmdLine, 4096,
			L"\"%s\" -rom \"%s\" -wifi -fsroot \"%s\" -startin \"%s\" -prg \"%s\\%s\" -run -rtc -scale 2",
			emuPath, romPath, appDir, appDir, appDir, prgName);
	} else {
		// No PRG configured: rely on the app's own AUTOBOOT.X16 (if any) to
		// self-start once the KERNAL boots from the bundled app folder.
		_snwprintf(cmdLine, 4096,
			L"\"%s\" -rom \"%s\" -wifi -fsroot \"%s\" -startin \"%s\" -rtc -scale 2",
			emuPath, romPath, appDir, appDir);
	}

	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	BOOL ok = CreateProcessW(
		emuPath,
		cmdLine,
		NULL, NULL, FALSE,
		0,
		NULL,
		dir,
		&si, &pi
	);

	if (!ok) {
		wchar_t msg[MAX_PATH + 128];
		_snwprintf(msg, MAX_PATH + 128, L"Failed to launch x16emu.exe (error code %lu).", GetLastError());
		FatalBox(msg);
		return 1;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return 0;
}
