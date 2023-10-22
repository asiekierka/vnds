#include <nds.h>
#include "as_lib7.h"
#include "../common/fifo.h"

#ifdef SUPPORT_WIFI
#include <dswifi7.h>
#endif

u8 isOldDS = 0;
u8 backlight = 1;

void vBlankHandler() {
	inputGetAndSend();
   	AS_SoundVBL(); // Update AS_Lib

#ifdef SUPPORT_WIFI
   	Wifi_Update(); //Update WIFI
#endif
}

static u8 backlightGetMaxLevel() {
	if (isDSiMode()) return 4;
	else if (isOldDS) return 1;
	else return 3;
}

static u8 backlightReadLevel() {
	if (isDSiMode()) {
		return i2cReadRegister(I2C_PM, I2CREGPM_BACKLIGHT);
	} else if (isOldDS) {
		return (readPowerManagement(PM_CONTROL_REG) & (PM_BACKLIGHT_TOP|PM_BACKLIGHT_BOTTOM)) ? 1 : 0;
	} else {
		return (readPowerManagement(PM_BACKLIGHT_LEVEL) & 0x03);
	}
}

static void backlightWriteLevel(u8 value) {
	if (value > backlightGetMaxLevel()) {
		value = backlightGetMaxLevel();
	}

	if (isDSiMode()) {
		i2cWriteRegister(I2C_PM, I2CREGPM_BACKLIGHT, value);
	} else if (isOldDS) {
		u8 oldPMValue = readPowerManagement(PM_CONTROL_REG);
		u8 backlightOn = oldPMValue & (PM_BACKLIGHT_TOP|PM_BACKLIGHT_BOTTOM);
		if (backlightOn != value) {
			if (value) {
				writePowerManagement(PM_CONTROL_REG, oldPMValue | (PM_BACKLIGHT_TOP|PM_BACKLIGHT_BOTTOM));
			} else {
				writePowerManagement(PM_CONTROL_REG, oldPMValue & ~(PM_BACKLIGHT_TOP|PM_BACKLIGHT_BOTTOM));
			}
		}
	} else {
		writePowerManagement(PM_BACKLIGHT_LEVEL, (readPowerManagement(PM_BACKLIGHT_LEVEL) & 0xFC) | value);
	}
}

int main(int argc, char** argv) {
        enableSound();

        readUserSettings();
        ledBlink(0);

        touchInit();
        irqInit();
        fifoInit();

#ifdef SUPPORT_WIFI
	installWifiFIFO();
#endif
	installSoundFIFO();
	installSystemFIFO();

	irqSet(IRQ_VBLANK, vBlankHandler);
	irqEnable(IRQ_VBLANK);

        initClockIRQTimer(3);

        // Send backlight state
	isOldDS = !(readPowerManagement(4) & BIT(6));
	fifoSendValue32(TCOMMON_FIFO_CHANNEL_ARM9, MSG_TOGGLE_BACKLIGHT);
	fifoSendValue32(TCOMMON_FIFO_CHANNEL_ARM9, backlightReadLevel());

	//Enable sound
	REG_SOUNDCNT = SOUND_ENABLE | SOUND_VOL(0x7F);
	swiWaitForVBlank();

	do {
    	if (fifoCheckValue32(TCOMMON_FIFO_CHANNEL_ARM7)) {
        	u32 value = fifoGetValue32(TCOMMON_FIFO_CHANNEL_ARM7);

        	switch (value) {
        	case MSG_TOGGLE_BACKLIGHT: {
			u8 backlight = backlightReadLevel();
			if (backlight >= backlightGetMaxLevel()) {
				backlight = 0;
			} else {
				backlight++;
			}
			backlightWriteLevel(backlight);
        	    	fifoSendValue32(TCOMMON_FIFO_CHANNEL_ARM9, MSG_TOGGLE_BACKLIGHT);
	            	fifoSendValue32(TCOMMON_FIFO_CHANNEL_ARM9, backlight);
		} break;
        	case MSG_INIT_SOUND_ARM7:
        	    while (!fifoCheckValue32(TCOMMON_FIFO_CHANNEL_ARM7));
        	    int v = fifoGetValue32(TCOMMON_FIFO_CHANNEL_ARM7);
        	   	AS_Init((IPC_SoundSystem*)v);
        	   	break;
        	}
        }

        if (!(REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) {
            break;
        }

        swiWaitForVBlank();
        AS_MP3Engine();
    } while(1);
}
