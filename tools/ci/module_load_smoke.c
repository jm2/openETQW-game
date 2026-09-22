/* SPDX-License-Identifier: MIT */
/*
 * Sanitizer load smoke for the openETQW-game module.
 *
 * Build this host with the same -fsanitize flags as the module so the
 * ASan/UBSan runtime lives in the executable, then:
 *
 *     module_load_smoke build/game_x64.so
 *
 * It dlopen()s the module (running every static initialiser under the
 * sanitizers), resolves GetGameAPI without calling it (calling it needs a
 * populated engine import table), then dlclose()s it (running the module's
 * static destructors).  No game data is involved.  Exit 0 on success.
 */
#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv) {
	void *module;
	void *entry;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <game module>\n", argv[0]);
		return 2;
	}
	module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
	if (module == NULL) {
		fprintf(stderr, "dlopen(%s) failed: %s\n", argv[1], dlerror());
		return 1;
	}
	entry = dlsym(module, "GetGameAPI");
	if (entry == NULL) {
		fprintf(stderr, "dlsym(GetGameAPI) failed: %s\n", dlerror());
		dlclose(module);
		return 1;
	}
	printf("GetGameAPI resolved at %p\n", entry);
	if (dlclose(module) != 0) {
		fprintf(stderr, "dlclose failed: %s\n", dlerror());
		return 1;
	}
	printf("module unloaded cleanly\n");
	return 0;
}
