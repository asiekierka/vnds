#include "wavpack.h"
#include "aac.h"
#include "as_lib9.h"
#include "sound_engine.h"
#include "tcommon/filehandle.h"

#define BUFFER_SIZE (32 * 1024)
#define READBUF_SIZE (2 * 1024)
#define FRAME_SIZE (1 * 1024)
#define BYTES_PER_OVERFLOW (2)

AACPlayer* aacPlayer;
bool playing;
int bufferReadPos;
int bufferWritePos;
int bufferStopPos;
int delay;

static char wpc_error[80];

s32 wv_read_callback(void *userdata, void *in_buffer, s32 num_bytes) {
	AACPlayer_BufferPtr *ptr = (AACPlayer_BufferPtr *) userdata;
	u8 *buffer = (u8*) in_buffer;
	s32 to_read = num_bytes;
	if (ptr->bytesLeft < num_bytes) to_read = ptr->bytesLeft;
	if (to_read > 0) {
		memcpy(buffer, ptr->readPtr, to_read);
		ptr->readPtr += to_read;
		ptr->bytesLeft -= to_read;
	}
	return to_read;
}

AACPlayer::AACPlayer() {
    buffer = new s16[BUFFER_SIZE];
    bufferReadPos = bufferWritePos = 0;
    bufferStopPos = -1;

    wpc = NULL;
	//Set up memory in a non-cache area
    out = (s32*)memalign(32, FRAME_SIZE*sizeof(s32));
    readBuf = new u8[READBUF_SIZE];

    fileHandle = NULL;
    channel = -1;
    playing = false;
}

AACPlayer::~AACPlayer() {
    StopSound();

    if (wpc) free(wpc);
    free(out);
    delete[] readBuf;
    delete[] buffer;
}

void timerOverflow() {
    if (playing) {
        if (delay > 0) {
            delay--;
        }
        if (delay <= 0) {
            int oldReadPos = bufferReadPos;

            bufferReadPos += BYTES_PER_OVERFLOW;
            if (bufferReadPos >= BUFFER_SIZE) {
                bufferReadPos -= BUFFER_SIZE;

                if (bufferStopPos >= 0) {
                    if (oldReadPos < bufferStopPos || bufferReadPos >= bufferStopPos) {
                        playing = false;
                        //printf("finished");
                    }
                }
            } else {
                if (bufferStopPos >= 0) {
                    if (oldReadPos < bufferStopPos && bufferReadPos >= bufferStopPos) {
                        playing = false;
                        //printf("finished");
                    }
                }
            }
        }
    }
}

void InitAACPlayer() {
    if (aacPlayer) {
        delete aacPlayer;
    }

    aacPlayer = new AACPlayer();
}

bool AACPlayer::PlaySound(Archive* archive, char* filename, u8 volume) {
    StopSound();

    ptr.readPtr = readBuf;
    ptr.bytesLeft = 0;
    eof = false;

    fileHandle = fhOpen(archive, filename);
    if (!fileHandle) {
        return false;
    }

    memset(buffer, 0, BUFFER_SIZE * sizeof(s16));
    bufferStopPos = -1;
    bufferReadPos = 0; //Has to be lower than writepos, or fillbuffer won't do anything
    bufferWritePos = 2; //Pos has to be a multiple of the number of bytes per sample
    //delay = WavpackGetSampleRate(wpc)/2;
    delay = 0;
    FillReadBuffer();

    if (wpc) free(wpc);
    wpc = WavpackOpenFileInput(wv_read_callback, &ptr, wpc_error);
    if (wpc == NULL) return false;
    if (WavpackGetReducedChannels(wpc) > 1) {
        // TODO: Support stereo.
        free(wpc);
        return false;
    }

    FillBuffer();
    PlayPCM(buffer, 0, BUFFER_SIZE, volume);
    swiWaitForVBlank();

    //Setup timer for AAC sound
    TIMER_DATA(2) = 0x10000 - (0x1000000 / WavpackGetSampleRate(wpc)) * 2;
    TIMER_CR(2) = TIMER_ENABLE | TIMER_IRQ_REQ | TIMER_DIV_1;
    irqEnable(IRQ_TIMER2);
    irqSet(IRQ_TIMER2, timerOverflow);

    playing = true;

	for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
		swiWaitForVBlank();
	}
    return true;
}
void AACPlayer::StopSound() {
    playing = false;
    bufferStopPos = -1;

    irqDisable(IRQ_TIMER2);
    TIMER_DATA(2) = 0;
    TIMER_CR(2) = 0;

    if (channel >= 0) {
        AS_SoundStop(channel);
        channel = -1;

        for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
        	swiWaitForVBlank();
        }
    }

    if (fileHandle) {
        fhClose(fileHandle);
        fileHandle = NULL;
    }
}

void AACPlayer::Update() {
    if (playing) {
    	//printf("d=06%d r=%06d w=%06d\n", delay, bufferReadPos, bufferWritePos);

    	if (bufferStopPos < 0) {
            FillBuffer();
        } else {
            //Fill writeable area with zeroes

            int writeBufferLeft = bufferReadPos - bufferWritePos;
            if (writeBufferLeft < 0) writeBufferLeft += BUFFER_SIZE;
            int space = BUFFER_SIZE / 4; //BUFFER_SIZE / 3;
            int b = writeBufferLeft - space;
            if (b > 0) {
                int l = MIN(BUFFER_SIZE - bufferWritePos, b);
                memset(buffer + bufferWritePos, 0, l);

                bufferWritePos += l;
                if (bufferWritePos >= BUFFER_SIZE) bufferWritePos = 0;

                if (l < b) {
                    memset(buffer + bufferWritePos, 0, b - l);
                    bufferWritePos += (b - l);
                }
                writeBufferLeft -= b;
            }
        }
    } else {
        if (bufferStopPos >= 0) {
            bufferStopPos = -1;

            irqDisable(IRQ_TIMER2);
            TIMER_DATA(2) = 0;
            TIMER_CR(2) = 0;

            if (channel >= 0) {
                AS_SoundStop(channel);
                channel = -1;
            }
            memset(buffer, 0, BUFFER_SIZE);

            for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
            	swiWaitForVBlank();
            }
        }
    }
}

void AACPlayer::FillReadBuffer() {
     int nRead = FillReadBuffer(fileHandle, readBuf, ptr.readPtr, READBUF_SIZE, ptr.bytesLeft);
     ptr.bytesLeft += nRead;
     ptr.readPtr = readBuf;
     if (nRead == 0) {
         eof = true;
     }
}

void AACPlayer::FillBuffer() {
    if (!fileHandle) return;

    int writeBufferLeft = bufferReadPos - bufferWritePos;
    if (writeBufferLeft < 0) writeBufferLeft += BUFFER_SIZE;

    //Keep some extra distance because of the inaccurate way the bufferReadPos
    //is calculated/updated.
    int space = BUFFER_SIZE / 4;//BUFFER_SIZE / 3;
    if (writeBufferLeft < FRAME_SIZE + space) {
        return;
    }

    do {
    	if (!eof && ptr.bytesLeft < READBUF_SIZE) {
            FillReadBuffer();
    	}

	int outputSamples = WavpackUnpackSamples(wpc, out, FRAME_SIZE);
        if (outputSamples <= 0) {
            bufferStopPos = bufferWritePos - space;
            if (bufferStopPos < 0) bufferStopPos += BUFFER_SIZE;
            break;
        } else {
            //Write to circle buffer
            s32 *outb = out;
            for (int i = 0; i < outputSamples; i++) {
            	buffer[bufferWritePos] = *(outb++);
            	bufferWritePos = (bufferWritePos + 1) & (BUFFER_SIZE - 1);
            }

            writeBufferLeft -= outputSamples;
            if (writeBufferLeft < FRAME_SIZE + space) {
                //Break before an overflow can occur
                break;
            }
        }
    } while (true);
}

int AACPlayer::FillReadBuffer(FileHandle* fh, u8* readBuf, u8* readPtr, int bufSize, int bytesLeft) {
	int nRead;

	/* move last, small chunk from end of buffer to start, then fill with new data */
	memmove(readBuf, readPtr, bytesLeft);
	nRead = fh->Read(readBuf + bytesLeft, bufSize - bytesLeft);

	/* zero-pad to avoid finding false sync word after last frame (from old data in readBuf) */
	if (nRead < bufSize - bytesLeft) {
		memset(readBuf + bytesLeft + nRead, 0, bufSize - bytesLeft - nRead);
    }

	return nRead;
}

void AACPlayer::PlayPCM(s16* buffer, int from, int to, u8 volume) {
    if (channel >= 0) {
        AS_SoundStop(channel);
    }
    for (int n = 0; n < SOUND_COMMAND_DELAY; n++) {
    	swiWaitForVBlank();
    }

    //Send data to AS_Lib
    SoundInfo si;
    si.data = (u8*) (buffer + from);
    si.size = to - from;
    si.format = AS_PCM_16BIT;
    si.rate = WavpackGetSampleRate(wpc);
    si.volume = volume;
    si.pan = 64;
    si.loop = 1;
    si.priority = 0;
    si.delay = 0;

    channel = AS_SoundPlay(si);
}
