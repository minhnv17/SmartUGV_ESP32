#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/queue.h"
#include "freertos/task.h"
#include "speaker.h"

// A failing regression must exit on the terminal, not open a Windows CRT dialog.
#undef assert
#define assert(condition) do { if (!(condition)) { fprintf(stderr, "Check failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

struct host_queue { unsigned size, capacity, head, count; uint8_t *data; };
static TickType_t clock_ms;
static jmp_buf finished;
static void (*task)(void *);
static bool amplifier;
static unsigned writes, old_music, new_music, voices;
static int scenario;
static bool resumed;
static TickType_t first_voice, resumed_music;
static unsigned underruns;
static TickType_t dma_end;

static void fill_music(int16_t level, unsigned frames, uint32_t epoch)
{
    int16_t pcm[480];
    for (unsigned i=0; i<480; ++i) pcm[i]=level;
    for (unsigned i=0; i<frames; ++i) assert(speaker_music_queue(pcm,scenario==6 ? 160 : 480,epoch));
    speaker_music_enable(true);
}

static void advance(TickType_t ticks)
{
    clock_ms += ticks;
    if (scenario == 2 && !resumed && clock_ms >= 1200) { resumed=true; speaker_music_enable(true); }
    if (clock_ms > 4000) longjmp(finished,1);
}

QueueHandle_t xQueueCreateStatic(unsigned length, unsigned size, uint8_t *memory, StaticQueue_t *state)
{
    struct host_queue *q=calloc(1,sizeof(*q));
    assert(q); q->size=size; q->capacity=length; q->data=memory; return q;
}
BaseType_t xQueueSend(QueueHandle_t q,const void *item,TickType_t wait)
{
    if(q->count==q->capacity) { advance(wait); return pdFALSE; }
    memcpy(q->data+((q->head+q->count)%q->capacity)*q->size,item,q->size); ++q->count; return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t wait)
{
    if(!q->count) { advance(wait); return pdFALSE; }
    memcpy(item,q->data+q->head*q->size,q->size); q->head=(q->head+1)%q->capacity; --q->count; return pdTRUE;
}
unsigned uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
void vQueueDelete(QueueHandle_t q) { free(q); }
TickType_t xTaskGetTickCount(void) { return clock_ms; }
void vTaskDelay(TickType_t ticks) { advance(ticks); }
BaseType_t xTaskCreate(void (*function)(void *),const char *name,unsigned stack,void *argument,unsigned priority,void *handle)
{ task=function; return pdPASS; }
void speaker_enable(bool enabled) { amplifier=enabled; }

esp_err_t speaker_write_pcm16(const int16_t *pcm,size_t samples,uint32_t timeout)
{
    assert(amplifier && samples);
    ++writes;
    // Constant music is scaled by 70%; settled voice passes the existing FIR.
    int level=pcm[samples>100 ? 100 : 0];
    if(scenario==7) {
        ++old_music;
        if(writes==1) assert(pcm[0]==700 && pcm[samples-1]==700);
        else if(writes==2) {
            assert(pcm[0]>0 && pcm[0]<700 && pcm[samples-1]==0);
            for(size_t i=1;i<samples;++i) assert(pcm[i]<=pcm[i-1]);
        } else for(size_t i=0;i<samples;++i) assert(pcm[i]==0);
        speaker_music_snapshot_t snapshot;
        speaker_music_snapshot(&snapshot);
        assert(snapshot.played_samples==(writes-1)*480);
    } else if(level==700) {
        ++old_music;
        if(voices && !resumed_music) resumed_music=clock_ms;
    } else if(level==2100) ++new_music;
    else { assert(level>=1995 && level<=2005); ++voices; first_voice=clock_ms; }
    if(scenario==6) {
        // Real TX writes return immediately while the 90ms DMA has room.
        // Injecting a 20ms voice-queue wait per 10ms PCM packet drains it.
        if(writes>1 && clock_ms>dma_end) ++underruns;
        if(dma_end<clock_ms) dma_end=clock_ms;
        TickType_t duration=(TickType_t)(samples*1000/16000);
        if(dma_end+duration>clock_ms+90) advance(dma_end+duration-clock_ms-90);
        dma_end+=duration;
    } else advance((TickType_t)(samples*1000/16000));
    if(writes==1) {
        if(scenario==1) {
            int16_t voice[480]; for(unsigned i=0;i<480;++i) voice[i]=2000;
            speaker_set_voice_pending(true);
            speaker_queue_pcm24(voice,480);
            speaker_end_turn();
            speaker_set_voice_pending(false);
        } else if(scenario==2) speaker_music_enable(false);
        else if(scenario==3) fill_music(3000,24,speaker_music_reset());
        else if(scenario==4) speaker_flush();
        else if(scenario==7) speaker_music_set_volume(0);
    }
    return ESP_OK;
}

int main(int argc,char **argv)
{
    assert(argc==2); scenario=atoi(argv[1]);
    assert(speaker_start()==ESP_OK && task);
    fill_music(1000,scenario==5 ? 4 : scenario==6 ? 60 : 24,speaker_music_reset());
    if(scenario==5) speaker_music_end();
    if(!setjmp(finished)) task(NULL);
    if(scenario==1) { assert(old_music==24 && voices==1 && resumed_music>=first_voice+600); }
    if(scenario==2) { assert(old_music==24 && resumed); }
    if(scenario==3) { assert(old_music==1 && new_music==24); }
    if(scenario==4) { assert(old_music==24); }
    if(scenario==5) { assert(old_music==4); }
    if(scenario==6) { printf("DMA underruns: %u\n",underruns); assert(old_music==60 && underruns==0); }
    if(scenario==7) {
        speaker_music_snapshot_t snapshot;
        speaker_music_snapshot(&snapshot);
        assert(old_music==24 && snapshot.played_samples==24*480 && snapshot.volume_percent==0);
        for(unsigned i=0;i<SPEAKER_MUSIC_LEVELS;++i) assert(snapshot.levels[i]==0);
        speaker_music_set_volume(101);
        assert(speaker_music_get_volume()==0);
    }
    assert(!speaker_music_failed());
    printf("Speaker scenario %d passed (%u writes).\n",scenario,writes);
    return 0;
}
