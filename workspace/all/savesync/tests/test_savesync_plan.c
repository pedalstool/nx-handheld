// Host test for the newer-file rule. No device headers.
//   cc -std=gnu99 -Wall -Werror -I.. -o /tmp/test_savesync_plan ../savesync_plan.c test_savesync_plan.c && /tmp/test_savesync_plan

#include "savesync_plan.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
	assert(savesync_decide(-1, 0, -1, 0) == SAVESYNC_SKIP);
	assert(savesync_decide(10, 100, -1, 0) == SAVESYNC_PUSH);
	assert(savesync_decide(-1, 0, 10, 100) == SAVESYNC_PULL);
	assert(savesync_decide(10, 200, 10, 100) == SAVESYNC_PUSH);
	assert(savesync_decide(10, 100, 10, 200) == SAVESYNC_PULL);
	assert(savesync_decide(10, 100, 12, 100) == SAVESYNC_SKIP); // same time: leave both
	assert(savesync_decide(0, 500, 10, 100) == SAVESYNC_PULL);	// empty must not win
	assert(savesync_decide(10, 100, 0, 500) == SAVESYNC_PUSH);
	assert(savesync_decide(0, 500, -1, 0) == SAVESYNC_SKIP);
	assert(savesync_decide(-1, 0, 0, 500) == SAVESYNC_SKIP);

	assert(savesync_allow_upload(false, 10, 50));
	assert(savesync_allow_upload(true, 100, 50));  // peer still on an older copy
	assert(!savesync_allow_upload(true, 100, 150)); // peer moved past what we started with
	assert(!savesync_allow_upload(true, -1, 150));	// we had no file, peer does
	assert(savesync_allow_upload(true, 100, -1));	// peer has nothing

	assert(savesync_path_ok("GB/Tetris.srm"));
	assert(savesync_path_ok("GBA/Pokemon - Emerald.srm"));
	assert(!savesync_path_ok(""));
	assert(!savesync_path_ok("/etc/passwd"));
	assert(!savesync_path_ok("../x"));
	assert(!savesync_path_ok("GB/../../etc/passwd"));
	assert(!savesync_path_ok("GB//x.srm"));
	assert(!savesync_path_ok("GB/./x.srm"));

	char enc[128], dec[128];
	assert(savesync_encode_path("GB/Pokemon - Emerald.srm", enc, sizeof(enc)) > 0);
	assert(strcmp(enc, "GB/Pokemon%20-%20Emerald.srm") == 0);
	assert(savesync_decode_path(enc, dec, sizeof(dec)));
	assert(strcmp(dec, "GB/Pokemon - Emerald.srm") == 0);
	assert(!savesync_decode_path("GB/%2E%2E/%2E%2E/etc/passwd", dec, sizeof(dec)));
	assert(!savesync_decode_path("%2Fetc%2Fpasswd", dec, sizeof(dec)));
	assert(!savesync_decode_path("GB/%", dec, sizeof(dec)));

	long mt = 0, sz = 0;
	assert(savesync_parse_index_line("1710000000\t128\tGB/Tetris.srm\r", &mt, &sz, dec, sizeof(dec)));
	assert(mt == 1710000000 && sz == 128 && strcmp(dec, "GB/Tetris.srm") == 0);
	assert(!savesync_parse_index_line("nope", &mt, &sz, dec, sizeof(dec)));
	assert(!savesync_parse_index_line("1\t-5\tGB/x.srm", &mt, &sz, dec, sizeof(dec)));

	assert(savesync_cart_key("GBA/Golden Sun (USA, Europe).sav", dec, sizeof(dec)));
	assert(strcmp(dec, "GBA/Golden Sun (USA, Europe)") == 0);
	assert(savesync_cart_key("GBA/Golden Sun (USA, Europe).srm", dec, sizeof(dec)));
	assert(strcmp(dec, "GBA/Golden Sun (USA, Europe)") == 0);
	assert(savesync_cart_key("GBA/Golden Sun (USA, Europe).gba.sav", dec, sizeof(dec)));
	assert(strcmp(dec, "GBA/Golden Sun (USA, Europe)") == 0);
	assert(!savesync_cart_key("GBA/Golden Sun (USA, Europe).state", dec, sizeof(dec)));
	assert(savesync_cart_prefer("GBA/Golden Sun.sav", 10, 100, "GBA/Golden Sun.srm", 10, 100) == 1);
	assert(savesync_cart_prefer("GBA/Golden Sun.srm", 10, 200, "GBA/Golden Sun.sav", 10, 100) == 1);
	assert(savesync_cart_prefer("GBA/Golden Sun.srm", 0, 200, "GBA/Golden Sun.sav", 10, 100) == 0);

	printf("test_savesync_plan: all passed\n");
	return 0;
}
