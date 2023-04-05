/*

  Advanced Sound Library (ASlib)
  ------------------------------

  file        : sound7.c
  author      : Lasorsa Yohan (Noda)
  description : ARM7 sound functions

  history :

    02/12/2007 - v1.0
      = Original release

    21/12/2007 - v1.1
      = corrected arm7/amr9 initialization (fix M3S/R4 problems)
      = fixed stereo detection problem (thanks to ThomasS)


  credits :
    - IPC sound system and streaming is based on cold_as_ice streaming example
    - audio stream filling / mp3 decoding based on ThomasS mp3 decoding example
    - ASM stereo desinterleave function was made by Thoduv

*/

#include <nds.h>
#include <string.h>

#include "wavpack.h"
#include "as_lib7.h"

// internal functions
void AS_InitMP3();
void AS_MP3Stop();
void AS_SetTimer(int freq);
void AS_RegenStream();
void AS_RegenStreamCallback(s16 *stream, u32 numsamples);
void AS_StereoDesinterleave(s16 *input, s16 *outputL, s16 *outputR, u32 samples);

IPC_SoundSystem* ipcSound = 0;

// variables for the wavpack player
extern WavpackContext wavpackContext;
char wpc_error[80];
u8 *readPtr;
int bytesLeft;

#define BUFFER_SIZE 1024

s32 audioBuf[BUFFER_SIZE];
int nAudioBufStart;
int nAudioBuf;
u8 nChans;

s32 wv_read_callback(void *userdata, void *in_buffer, s32 num_bytes) {
	u8 *buffer = (u8*) in_buffer;
	s32 to_read = num_bytes;
	if (bytesLeft < num_bytes) to_read = bytesLeft;
	if (to_read > 0) {
		memcpy(buffer, readPtr, to_read);
		readPtr += to_read;
		bytesLeft -= to_read;
	}
	return to_read;
}

// the sound engine, must be called each VBlank
void AS_SoundVBL() {
	if (!ipcSound) return;

    int i;

    // adjust master volume
    if (ipcSound->chan[0].cmd & SNDCMD_SETMASTERVOLUME) {
    	REG_MASTER_VOLUME = SOUND_VOL(ipcSound->volume & 127);
        ipcSound->chan[0].cmd &= ~SNDCMD_SETMASTERVOLUME;
    }

    // manage sounds
    for(i = 0; i < 16; i++) {

        if(ipcSound->chan[i].cmd & SNDCMD_DELAY)
        {
            if(ipcSound->chan[i].snd.delay == 0) {
                ipcSound->chan[i].cmd &= ~SNDCMD_DELAY;
                ipcSound->chan[i].cmd |= SNDCMD_PLAY;
            } else {
                ipcSound->chan[i].snd.delay -= 1;
            }
        }

        if(ipcSound->chan[i].cmd & SNDCMD_STOP)
        {
            SCHANNEL_CR(i) = 0;
            ipcSound->chan[i].cmd &= ~SNDCMD_STOP;
        }

        if(ipcSound->chan[i].cmd & SNDCMD_PLAY)
        {
            SCHANNEL_TIMER(i) = 0x10000 - (0x1000000 / ipcSound->chan[i].snd.rate);
            SCHANNEL_SOURCE(i) = (u32)ipcSound->chan[i].snd.data;
            SCHANNEL_REPEAT_POINT(i) = 0;
            SCHANNEL_LENGTH(i) = ipcSound->chan[i].snd.size >> 2 ;
            SCHANNEL_CR(i) = SCHANNEL_ENABLE | ((SOUND_ONE_SHOT) >> (ipcSound->chan[i].snd.loop)) | SOUND_VOL(ipcSound->chan[i].volume & 127) | SOUND_PAN(ipcSound->chan[i].pan & 127) | ((ipcSound->chan[i].snd.format) << 29);
            ipcSound->chan[i].cmd &= ~SNDCMD_PLAY;
        }

        if(ipcSound->chan[i].cmd & SNDCMD_SETVOLUME)
        {
            SCHANNEL_VOL(i) = ipcSound->chan[i].volume & 127;
            ipcSound->chan[i].cmd &= ~SNDCMD_SETVOLUME;
        }

        if(ipcSound->chan[i].cmd & SNDCMD_SETPAN)
        {
            SCHANNEL_PAN(i) = ipcSound->chan[i].pan & 127;
            ipcSound->chan[i].cmd &= ~SNDCMD_SETPAN;
        }

        if(ipcSound->chan[i].cmd & SNDCMD_SETRATE)
        {
            SCHANNEL_TIMER(i) = 0x10000 - (0x1000000 / ipcSound->chan[i].snd.rate);
            ipcSound->chan[i].cmd &= ~SNDCMD_SETRATE;
        }

        ipcSound->chan[i].busy = SCHANNEL_CR(i) >> 31;
    }

    // manage mp3
    if (ipcSound->mp3.cmd & MP3CMD_INIT) {
        AS_InitMP3();
        ipcSound->mp3.cmd &= ~MP3CMD_INIT;
    }

#ifndef SHRINK_AS_LIB
    if (ipcSound->mp3.cmd & MP3CMD_SETRATE)
    {
        ipcSound->mp3.cmd &= ~MP3CMD_SETRATE;
        AS_SetTimer(ipcSound->mp3.rate);
        SCHANNEL_TIMER(ipcSound->mp3.channelL) = 0x10000 - (0x1000000 / ipcSound->mp3.rate);
        SCHANNEL_TIMER(ipcSound->mp3.channelR) = 0x10000 - (0x1000000 / ipcSound->mp3.rate);
    }

    if (ipcSound->mp3.cmd & MP3CMD_PAUSE)
    {
        ipcSound->mp3.cmd &= ~MP3CMD_PAUSE;

        // disable mp3 channels
        SCHANNEL_CR(ipcSound->mp3.channelL) = 0;
        SCHANNEL_CR(ipcSound->mp3.channelR) = 0;
        AS_SetTimer(0);

        // then wait for the restart
        ipcSound->mp3.cmd |= MP3CMD_WAITING;
        ipcSound->mp3.state = MP3ST_PAUSED;

    }
#endif

}

// the mp3 decoding engine, must be called on a regular basis (like after VBlank)
void AS_MP3Engine() {
    if (!ipcSound) return;

    MP3Player* mp3 = &ipcSound->mp3;

    // time-varying mp3 functions are placed oustide the VBL function
    if (mp3->cmd & MP3CMD_STOP)  {
        mp3->cmd &= ~MP3CMD_STOP;
        AS_MP3Stop();
    }
    if (mp3->cmd & MP3CMD_PLAY) {
        mp3->cmd &= ~MP3CMD_PLAY;

#ifndef SHRINK_AS_LIB
        if (mp3->state == MP3ST_PAUSED) {
            //Restart on a fresh basis
        	mp3->prevtimer = 0;
            AS_RegenStreamCallback((s16*)mp3->mixbuffer, mp3->buffersize >> 1);
            mp3->soundcursor = mp3->buffersize >> 1;
            AS_SetTimer(mp3->rate);

            mp3->cmd |= MP3CMD_MIX;
        } else {
#endif

            // set variables
            mp3->prevtimer = 0;
            mp3->numsamples = 0;
            readPtr = mp3->mp3buffer;
            bytesLeft = mp3->mp3filesize;
            nAudioBuf = 0;
            nAudioBufStart = 0;

            // initialize MP3 engine
            WavpackOpenFileInput(wv_read_callback, NULL, wpc_error);
            // gather information about the format
            nChans = WavpackGetReducedChannels(&wavpackContext);
            mp3->rate = WavpackGetSampleRate(&wavpackContext);

            // fill the half of the buffer
            AS_RegenStreamCallback((s16*)mp3->mixbuffer, mp3->buffersize >> 1);
            mp3->soundcursor = mp3->buffersize >> 1;

            AS_SetTimer(mp3->rate);

            // start playing
            mp3->cmd |= MP3CMD_MIX;
#ifndef SHRINK_AS_LIB
        }
#endif
        mp3->state = MP3ST_PLAYING;
    }

    // do the decoding
    if (mp3->cmd & MP3CMD_MIXING) {
        s32 curtimer = TIMER1_DATA;

        if (mp3->cmd & MP3CMD_WAITING) {
        	mp3->cmd &= ~MP3CMD_WAITING;
        } else {
            s32 numsamples = curtimer - mp3->prevtimer;
            if (numsamples < 0) numsamples += 65536;
            mp3->numsamples = numsamples;
        }

        mp3->prevtimer = curtimer;
        AS_RegenStream();

        mp3->soundcursor += mp3->numsamples;
        if (mp3->soundcursor > mp3->buffersize) {
            mp3->soundcursor -= mp3->buffersize;
        }
    } else if (mp3->cmd & MP3CMD_MIX) {
    	SoundChannel* left = &ipcSound->chan[mp3->channelL];
    	SoundChannel* right = &ipcSound->chan[mp3->channelR];
    	SoundInfo* leftSnd = &left->snd;
    	SoundInfo* rightSnd = &right->snd;

        leftSnd->data = (u8*)mp3->mixbuffer;
        rightSnd->data = (nChans >= 2 ? (u8*)mp3->mixbuffer + (mp3->buffersize << 1) : (u8*)mp3->mixbuffer);

        leftSnd->size = rightSnd->size = mp3->buffersize << 1;
        leftSnd->format = rightSnd->format = AS_PCM_16BIT;
        leftSnd->rate = rightSnd->rate = mp3->rate;
        leftSnd->loop = rightSnd->loop = true;

        leftSnd->delay = 0;
        rightSnd->delay = mp3->delay;

        left->cmd |= SNDCMD_PLAY;
        right->cmd |= SNDCMD_DELAY;

        mp3->cmd &= ~MP3CMD_MIX;
        mp3->cmd |= MP3CMD_MIXING;
    }
}

// set timer to the given period
void AS_SetTimer(int freq)
{
    if(freq) {
        TIMER0_DATA = 0x10000 - (0x1000000 / freq) * 2;
        TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1;
        TIMER1_DATA = 0;
        TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
    } else {
        TIMER0_DATA = 0;
        TIMER0_CR = 0;
        TIMER1_DATA = 0;
        TIMER1_CR = 0;
    }
}

__attribute__((optimize("-O3")))
static void AS_CopyAudioData(s16 *stream, int outSample, int minSamples) {
	if(nChans >= 2) {
	   s32 *in = audioBuf + (nAudioBufStart << 1);
	   s16 *outL = stream + outSample;
	   s16 *outR = stream + outSample + ipcSound->mp3.buffersize;
	   for (int i = 0; i < minSamples; i++) {
	       *(outL++) = *(in++);
	       *(outR++) = *(in++);
	   }
	} else {
	   s32 *in = audioBuf + nAudioBufStart;
	   s16 *outL = stream + outSample;
	   for (int i = 0; i < minSamples; i++) {
	       *(outL++) = *(in++);
	   }
	}
}

// fill the given stream buffer with numsamples samples. (based on ThomasS code)
void AS_RegenStreamCallback(s16 *stream, u32 numsamples)
{
    int outSample, restSample, minSamples;
    outSample = 0;

    // fill buffered data into stream
    minSamples = nAudioBuf;
    if (minSamples > numsamples) minSamples = numsamples;
    if (minSamples > 0) {

    // copy data from audioBuf to stream
        numsamples -= minSamples;

	AS_CopyAudioData(stream, outSample, minSamples);

        outSample += minSamples;

        nAudioBufStart += minSamples;
        nAudioBuf -= minSamples;

        if (nAudioBuf <= 0) {
            nAudioBufStart = 0;
            nAudioBuf = 0;
        }
    }

    // if more data is still needed then decode some mp3 frames
    if (numsamples > 0)  {
         // if mp3 is set to loop indefinitely, don't bother with how many data is left
         if((bytesLeft < 2*BUFFER_SIZE) && ipcSound->mp3.loop && ipcSound->mp3.stream)
             bytesLeft += ipcSound->mp3.mp3filesize;

        // decode a mp3 frame to outBuf
        do {
            int outputSamples = WavpackUnpackSamples(&wavpackContext, audioBuf, BUFFER_SIZE / nChans);
            if (!outputSamples) {
                if (ipcSound->mp3.loop && !ipcSound->mp3.stream) {
                    readPtr = ipcSound->mp3.mp3buffer;
                    bytesLeft = ipcSound->mp3.mp3filesize;
                } else {
                    AS_MP3Stop();
                    ipcSound->mp3.state = MP3ST_DECODE_ERROR;
                }
                return;
            }

            // copy the decoded data to the stream
            minSamples = outputSamples;
            if (minSamples > numsamples) minSamples = numsamples;
            restSample = outputSamples - numsamples;
            numsamples -= minSamples;

	AS_CopyAudioData(stream, outSample, minSamples);

            outSample += minSamples;

        // if still more data is needed, then decode the next frame
        } while (numsamples > 0);

        // set the rest of the decoded data to be used for the next frame
        nAudioBufStart = minSamples;
        nAudioBuf = restSample;
    }

    // check if we moved onto the 2nd file data buffer, if so move it to the 1st one and request a refill
    if(ipcSound->mp3.stream && (readPtr >= ipcSound->mp3.mp3buffer + ipcSound->mp3.mp3buffersize )) {
        memcpy(ipcSound->mp3.mp3buffer, ipcSound->mp3.mp3buffer + ipcSound->mp3.mp3buffersize, ipcSound->mp3.mp3buffersize);
        readPtr = readPtr - ipcSound->mp3.mp3buffersize;
        ipcSound->mp3.needdata = true;
    }

}

// regenerate the sound stream into the ring buffer.
void AS_RegenStream()
{
    int remain;

    // decode data to the ring buffer
    if((ipcSound->mp3.soundcursor + ipcSound->mp3.numsamples) >= ipcSound->mp3.buffersize) {
        AS_RegenStreamCallback((s16*)&ipcSound->mp3.mixbuffer[ipcSound->mp3.soundcursor << 1], ipcSound->mp3.buffersize - ipcSound->mp3.soundcursor);
        remain = ipcSound->mp3.numsamples - (ipcSound->mp3.buffersize - ipcSound->mp3.soundcursor);
        AS_RegenStreamCallback((s16*)ipcSound->mp3.mixbuffer, remain);
    } else {
        AS_RegenStreamCallback((s16*)&ipcSound->mp3.mixbuffer[ipcSound->mp3.soundcursor << 1], ipcSound->mp3.numsamples);
    }
}

// stop playing the mp3
void AS_MP3Stop()
{
    SCHANNEL_CR(ipcSound->mp3.channelL) = 0;
    SCHANNEL_CR(ipcSound->mp3.channelR) = 0;
    AS_SetTimer(0);
    ipcSound->mp3.rate = 0;
    ipcSound->mp3.cmd = MP3CMD_NONE;
    ipcSound->mp3.state = MP3ST_STOPPED;
}

void AS_InitMP3() {
    AS_SetTimer(0);
}

void AS_Init(IPC_SoundSystem* ipc) {
	REG_MASTER_VOLUME = SOUND_VOL(ipc->volume = 127);

    ipc->chan[0].cmd |= SNDCMD_ARM7READY;
    //while (ipc->chan[0].cmd & SNDCMD_ARM7READY);

    ipcSound = ipc;
}

// desinterleave a stereo source (thanks to Thoduv for the code)
/* asm (
"@--------------------------------------------------------------------------------------\n"
"    .text                                                                              \n"
"    .arm                                                                               \n"
"                                                                                       \n"
"@ desinterleave an mp3 stereo source                                                   \n"
"@ r0 = interleaved data, r1 = left, r2 = right, r3 = len                               \n"
"                                                                                       \n"
"    .global AS_StereoDesinterleave                                                     \n"
"                                                                                       \n"
"AS_StereoDesinterleave:                                                                \n"
"    stmfd sp!, {r4-r11}                                                                \n"
"                                                                                       \n"
"_loop:                                                                                 \n"
"                                                                                       \n"
"    ldmia r0!, {r4-r12}                                                                \n"
"                                                                                       \n"
"    strh r4, [r1], #2                                                                  \n"
"    mov r4, r4, lsr #16                                                                \n"
"    strh  r4, [r2], #2                                                                 \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r5, [r1], #2                                                                  \n"
"    mov r5, r5, lsr #16                                                                \n"
"    strh  r5, [r2], #2                                                                 \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r6, [r1], #2                                                                  \n"
"    mov r6, r6, lsr #16                                                                \n"
"    strh  r6, [r2], #2                                                                 \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r7, [r1], #2                                                                  \n"
"    mov r7, r7, lsr #16                                                                \n"
"    strh  r7, [r2], #2                                                                 \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r8, [r1], #2                                                                  \n"
"    mov r8, r8, lsr #16                                                                \n"
"    strh  r8, [r2], #2                                                                 \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r9, [r1], #2                                                                  \n"
"    mov r9, r9, lsr #16                                                                \n"
"    strh  r9, [r2], #2                                                                 \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r10, [r1], #2                                                                 \n"
"    mov r10, r10, lsr #16                                                              \n"
"    strh  r10, [r2], #2                                                                \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r11, [r1], #2                                                                 \n"
"    mov r11, r11, lsr #16                                                              \n"
"    strh  r11, [r2], #2                                                                \n"
"    subs r3, #1                                                                        \n"
"    beq _done                                                                          \n"
"                                                                                       \n"
"    strh r12, [r1], #2                                                                 \n"
"    mov r12, r12, lsr #16                                                              \n"
"    strh  r12, [r2], #2                                                                \n"
"    subs r3, #1                                                                        \n"
"    bne _loop                                                                          \n"
"_done:                                                                                 \n"
"                                                                                       \n"
"    ldmia sp!, {r4-r11}                                                                \n"
"    bx lr                                                                              \n"
"@--------------------------------------------------------------------------------------\n"
); */
