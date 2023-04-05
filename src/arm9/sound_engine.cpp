#include "sound_engine.h"

#include <sys/stat.h>
#include "aac.h"
#include "as_lib9.h"
#include "tcommon/filehandle.h"

SoundEngine::SoundEngine(Archive* soundArchive) {
	this->soundArchive = soundArchive;

	mute = false;

	musicPath[0] = '\0';

	soundType = ST_none;
	soundPath[0] = '\0';
	soundChannel = -1;
	soundBufferL = 0;

	SetMusicVolume(preferences->GetMusicVolume());
}
SoundEngine::~SoundEngine() {
	StopMusic();
	StopSound();
}

void SoundEngine::Reset() {
	StopMusic();
	StopSound();

	musicPath[0] = '\0';

	soundType = ST_none;
	soundPath[0] = '\0';
	soundChannel = -1;
	soundBufferL = 0;
}
void SoundEngine::Update() {
    aacPlayer->Update();
    AS_SoundVBL();
}

static void change_extension(char *path, const char *new_ext) {
	char *end = path + strlen(path);
	while (path < end && end[0] != '.') end--;
	if (path < end && end[0] == '.') {
		strcpy(end, new_ext);
	}
}

void SoundEngine::SetMusic(const char* in_path) {
	char path[PATH_MAX+1];
	strncpy(path, in_path, PATH_MAX);
	path[PATH_MAX] = 0;
	change_extension(path, ".wv");

	if (strcmp(path, "sound/~") == 0) {
		if (strlen(musicPath) > 0) {
			vnLog(EL_verbose, COM_SOUND, "Stopping music: %s", musicPath);

			AS_MP3Stop();
			musicPath[0] = '\0';
		}
	} else {
		if (strlen(musicPath) > 0) {
			StopMusic(); //Stop music that's playing first
		}

		strcpy(musicPath, path);

		FileHandle* fh = fhOpen(soundArchive, musicPath, true);
		//FILE* fh = fopen(musicPath, "rb");
		if (fh) {
			AS_SetMP3Loop(1);
			AS_MP3StreamPlay(fh);
			vnLog(EL_verbose, COM_SOUND, "Starting music: %s", musicPath);
		} else {
			vnLog(EL_missing, COM_SOUND, "Can't find music file: %s", musicPath);
		}
	}

	for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
		swiWaitForVBlank();
	}
}
void SoundEngine::StopMusic() {
	SetMusic("sound/~");
}
void SoundEngine::PlaySound(const char* in_path, int times) {
	if (times == 0) {
		times = 1;
	}

	char path[PATH_MAX+1];
	strncpy(path, in_path, PATH_MAX);
	path[PATH_MAX] = 0;

	if (strcmp(path, "sound/~") == 0) {
		if (soundType == ST_adpcm) {
			if (soundChannel >= 0) {
				AS_SoundStop(soundChannel);
				soundChannel = -1;

				for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
					swiWaitForVBlank();
				}
			}
		} else if (soundType == ST_aac) {
			aacPlayer->StopSound();
		}
	} else {
		if (mute) {
			soundType = ST_none;
			return;
		}

		char* periodChar = strrchr(path, '.');
	    if (periodChar && (!strcasecmp(periodChar+1, "wv") || !strcasecmp(periodChar+1, "aac") || !strcasecmp(periodChar+1, "mp4") || !strcasecmp(periodChar+1, "mp3"))) {
	    	// wavpack
	    	soundType = ST_aac;
		change_extension(path, ".wv");
		strcpy(soundPath, path);

			if (aacPlayer->PlaySound(soundArchive, soundPath, preferences->GetSoundVolume())) {
				vnLog(EL_verbose, COM_SOUND, "Playing sound: %s %d", soundPath, times);
			} else {
				vnLog(EL_missing, COM_SOUND, "Can't find sound file: %s", soundPath);
			}
	    } else {
	    	//.*
	    	soundType = ST_adpcm;
		strcpy(soundPath, path);

	    	FileHandle* fh = fhOpen(soundArchive, soundPath);
			if (fh) {
				soundBufferL = fh->Read(soundBuffer, SOUND_BUFFER_LENGTH);
				fhClose(fh);

				soundChannel = AS_SoundDefaultPlay(soundBuffer, soundBufferL,
						preferences->GetSoundVolume(), 64, times < 0, 1);
				vnLog(EL_verbose, COM_SOUND, "Playing sound: %s %d", soundPath, times);

				for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
					swiWaitForVBlank();
				}
			} else {
				vnLog(EL_missing, COM_SOUND, "Can't find sound file: %s", soundPath);
			}
	    }
	}
}
void SoundEngine::StopSound() {
	PlaySound("sound/~");
}
void SoundEngine::ReplaySound() {
	StopSound();
	SetMuted(false);

	if (soundType == ST_adpcm) {
		soundChannel = AS_SoundDefaultPlay(soundBuffer, soundBufferL, 127, 64, 0, 1);

		for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
			swiWaitForVBlank();
		}
	} else if (soundType == ST_aac) {
		aacPlayer->PlaySound(soundArchive, soundPath);
	}
}

void SoundEngine::SetMuted(bool m) {
	mute = m;
	AS_SetMasterVolume(m ? 0 : 127);
}

bool SoundEngine::IsMuted() {
	return mute;
}
const char* SoundEngine::GetMusicPath() {
	if (musicPath[0] == '\0') return NULL;
	return musicPath;
}

void SoundEngine::SetMusicVolume(u8 v) {
	AS_SetMP3Volume(v);
}
