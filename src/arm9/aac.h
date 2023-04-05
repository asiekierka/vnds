#ifndef AAC_H
#define AAC_H

#include <nds.h>
#include "wavpack.h"
#include "common.h"

typedef struct {
	u8* readPtr;
	s32 bytesLeft;
} AACPlayer_BufferPtr;

class AACPlayer {
    private:
        s16* buffer;
        int channel;

        WavpackContext* wpc;

        FileHandle* fileHandle;
        s32* out;
        u8* readBuf;
        AACPlayer_BufferPtr ptr;
        bool eof;

        void FillBuffer();
        void FillReadBuffer();
        int FillReadBuffer(FileHandle* fh, u8* readBuf, u8* readPtr, int bufSize, int bytesLeft);
        void PlayPCM(s16* buffer, int from, int to, u8 volume=127);

    public:
        AACPlayer();
        ~AACPlayer();

        void Update();

        bool PlaySound(Archive* archive, char* filename, u8 volume=127);
        void StopSound();
};

extern AACPlayer* aacPlayer;

void InitAACPlayer();

#endif
