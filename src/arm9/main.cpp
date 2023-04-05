#include <nds.h>
#include <fat.h>
#include <unistd.h>
#include <sys/dir.h>

#include "common.h"
#include "aac.h"
#include "as_lib9.h"
#include "vnds_types.h"
#include "vnds.h"
#include "select/vnselect.h"
#include "tcommon/text.h"
#include "tcommon/xml_parser.h"

void epicFail() {
	lcdMainOnBottom();
	consoleDemoInit();

    printf("\x1b[0;0H");
    BG_PALETTE_SUB[0]   = RGB15( 0,  0, 31) | BIT(15);
    BG_PALETTE_SUB[255] = RGB15(31, 31, 31) | BIT(15);
}

void libfatFail() {
    epicFail();

    const char* warning =
    "--------------------------------"
    "         libfat failure         "
    "--------------------------------"
    "                                "
    " An error is preventing the     "
    " game from accessing external   "
    " files.                         "
    "                                "
    " If you're using an emulator,   "
    " make sure it supports DLDI     "
    " and that it's activated.       "
    "                                "
    " In case you're seeing this     "
    " screen on a real DS, make sure "
    " you've applied the correct     "
    " DLDI patch (most modern        "
    " flashcards do this             "
    " automatically)                 "
    "                                "
    " Note: Some cards only          "
    " autopatch .nds files stored in "
    " the root folder of the card.   "
    "                                "
    "--------------------------------";
    printf(warning);

    while (true) {
        swiWaitForVBlank();
    }
}

void installFail() {
    epicFail();

    const char* warning =
    "--------------------------------"
    "      Invalid Installation      "
    "--------------------------------"
    "                                "
    " Please make sure the game data "
    " is stored in /vnds/            "
    "                                "
    "--------------------------------";
    printf(warning);

    while (true) {
        swiWaitForVBlank();
    }
}
void checkInstall() {
	if (!fexists(DEFAULT_FONT_PATH)) {
        installFail();
	}
}

int main(int argc, char** argv) {
    powerOn(POWER_ALL);
    defaultExceptionHandler();

    if (!fifoSetValue32Handler(TCOMMON_FIFO_CHANNEL_ARM9, &tcommonFIFOCallback, NULL)) {
    	return 1; //Fatal Error
    }

    //Init filesystem
    if (!fatInitDefault()) {
        libfatFail();
        return 1;
    } else {
        chdir("/vnds");
    }

    //Check install
    checkInstall();

    //Init default font
    createDefaultFontCache(DEFAULT_FONT_PATH);

    //Timer0 + Timer1 are used by as_lib
    //Timer2 is for AAC
    //Timer3 is for Wifi
    InitAACPlayer();

    //Init ASLib
    AS_Init(AS_MODE_MP3);
    AS_SetDefaultSettings(AS_ADPCM, 22050, 0);

    //Create log
    vnInitLog(0);

    NovelInfo* selectedNovelInfo = new NovelInfo();
    fadeBlack(1);
	while (true) {
		if (!preferences->Load("config.ini")) {
			skin->Load("skins/default");
		}
		strcpy(baseSkinPath, skinPath);

		VNSelect* vnselect = new VNSelect();
		vnselect->Run();
		memcpy(selectedNovelInfo, vnselect->selectedNovelInfo, sizeof(NovelInfo));
		delete vnselect;

		{
			char path[MAXPATHLEN];
			sprintf(path, "%s/skin", selectedNovelInfo->GetPath());
			skin->Load(path);
		}

		VNDS* vnds = new VNDS(selectedNovelInfo);
		vnds->Run();
		delete vnds;
	}
	delete selectedNovelInfo;

    vnDestroyLog();
    destroyDefaultFontCache();
    return 0;
}
