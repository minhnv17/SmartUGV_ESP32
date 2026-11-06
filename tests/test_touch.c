#include <assert.h>
#include <stdio.h>
#include "touch_ui.h"

static void release(touch_gesture_t *g, touch_view_t view)
{
    assert(touch_gesture_step(g, view, true, 0, 0, 0) == TOUCH_ACTION_NONE);
    assert(touch_gesture_step(g, view, true, 0, 0, 0) == TOUCH_ACTION_NONE);
}

static void press(touch_gesture_t *g, touch_view_t view, int x, int y)
{
    assert(touch_gesture_step(g, view, true, 1, x, y) == TOUCH_ACTION_NONE);
    assert(touch_gesture_step(g, view, true, 1, x, y) == TOUCH_ACTION_NONE);
}

static void check_coordinates(void)
{
    unsigned contacts;
    int x, y;
    const unsigned corners[][2] = {{0,0},{239,0},{0,319},{239,319},{22,81}};
    for (unsigned i=0; i<5; ++i) {
        unsigned raw_x=corners[i][0],raw_y=corners[i][1];
        uint8_t data[5] = {1, 0x80 | (raw_x >> 8), raw_x & 255, raw_y >> 8, raw_y & 255};
        assert(touch_decode(data,false,&contacts,&x,&y));
        assert(contacts==1 && x==(int)raw_y && y==239-(int)raw_x);
        assert(touch_decode(data,true,&contacts,&x,&y));
        assert(contacts==1 && x==319-(int)raw_y && y==(int)raw_x);
    }
    uint8_t tap[5] = {1,0,22,0,81};
    assert(touch_decode(tap,false,&contacts,&x,&y) && touch_hit(TOUCH_VIEW_ACTIVE,x,y)==TOUCH_ACTION_PAUSE);
    uint8_t flipped_tap[5] = {1,0,217,0,238};
    assert(touch_decode(flipped_tap,true,&contacts,&x,&y) && touch_hit(TOUCH_VIEW_ACTIVE,x,y)==TOUCH_ACTION_PAUSE);
    uint8_t invalid_x[5]={1,0,240,0,1};
    uint8_t invalid_y[5]={1,0,1,1,64};
    assert(!touch_decode(invalid_x,false,&contacts,&x,&y));
    assert(!touch_decode(invalid_y,false,&contacts,&x,&y));
    uint8_t up[5]={1,0x40,0,0,0};
    assert(touch_decode(up,false,&contacts,&x,&y) && contacts==0);
    uint8_t multi[5]={2,0xff,0xff,0xff,0xff};
    assert(touch_decode(multi,false,&contacts,&x,&y) && contacts==2);
    multi[0]=3;
    assert(!touch_decode(multi,false,&contacts,&x,&y));
    multi[0]=1;
    assert(!touch_decode(multi,false,&contacts,&x,&y));
}

static void check_buttons(void)
{
    assert(touch_hit(TOUCH_VIEW_ACTIVE,8,198)==TOUCH_ACTION_PAUSE);
    assert(touch_hit(TOUCH_VIEW_PAUSED,153,235)==TOUCH_ACTION_RESUME);
    assert(touch_hit(TOUCH_VIEW_ACTIVE,166,198)==TOUCH_ACTION_STOP);
    assert(touch_hit(TOUCH_VIEW_PAUSED,311,235)==TOUCH_ACTION_STOP);
    const int misses[][2]={{7,198},{154,198},{165,198},{312,198},{80,197},{80,236},{-1,220},{80,-1}};
    for(unsigned i=0;i<sizeof(misses)/sizeof(misses[0]);++i)
        assert(touch_hit(TOUCH_VIEW_ACTIVE,misses[i][0],misses[i][1])==TOUCH_ACTION_NONE);
    assert(touch_hit(TOUCH_VIEW_NONE,80,220)==TOUCH_ACTION_NONE);
    assert(touch_hit(TOUCH_VIEW_ACTIVE,20,170)==TOUCH_ACTION_VOLUME_DOWN);
    assert(touch_hit(TOUCH_VIEW_PAUSED,300,170)==TOUCH_ACTION_VOLUME_UP);
    assert(touch_hit(TOUCH_VIEW_ACTIVE,54,170)==TOUCH_ACTION_VOLUME_SET);
    assert(touch_hit(TOUCH_VIEW_ACTIVE,265,170)==TOUCH_ACTION_VOLUME_SET);
    assert(touch_hit(TOUCH_VIEW_NONE,160,170)==TOUCH_ACTION_NONE);
}

static void check_gestures(void)
{
    touch_gesture_t g={0};
    // A finger already on screen at startup never issues a command.
    press(&g,TOUCH_VIEW_ACTIVE,80,220);
    release(&g,TOUCH_VIEW_ACTIVE);
    press(&g,TOUCH_VIEW_ACTIVE,150,170);
    touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,1,319,170);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_NONE);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_VOLUME_SET);
    assert(g.volume_percent==100);
    press(&g,TOUCH_VIEW_ACTIVE,150,170);
    touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,1,0,170);
    touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_VOLUME_SET && g.volume_percent==0);
    press(&g,TOUCH_VIEW_ACTIVE,20,170);
    touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_VOLUME_DOWN);
    press(&g,TOUCH_VIEW_ACTIVE,80,220);
    for(unsigned i=0;i<20;++i)
        assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,1,80,220)==TOUCH_ACTION_NONE);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_NONE);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_PAUSE);
    release(&g,TOUCH_VIEW_PAUSED);
    press(&g,TOUCH_VIEW_PAUSED,80,220);
    assert(touch_gesture_step(&g,TOUCH_VIEW_PAUSED,true,0,0,0)==TOUCH_ACTION_NONE);
    assert(touch_gesture_step(&g,TOUCH_VIEW_PAUSED,true,0,0,0)==TOUCH_ACTION_RESUME);
    release(&g,TOUCH_VIEW_ACTIVE);
    press(&g,TOUCH_VIEW_ACTIVE,220,220);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_NONE);
    assert(touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,0,0,0)==TOUCH_ACTION_STOP);

    // Dragging, UI changes, I2C failure and multitouch cancel the entire tap.
    for(unsigned scenario=0;scenario<5;++scenario) {
        release(&g,TOUCH_VIEW_ACTIVE);
        press(&g,TOUCH_VIEW_ACTIVE,80,220);
        if(scenario==0) touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,1,220,220);
        if(scenario==1) touch_gesture_step(&g,TOUCH_VIEW_PAUSED,true,1,80,220);
        if(scenario==2) touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,false,0,0,0);
        if(scenario==3) touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,2,80,220);
        if(scenario==4) touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,1,80,100);
        press(&g,TOUCH_VIEW_ACTIVE,80,220);
        release(&g,TOUCH_VIEW_ACTIVE);
    }
    release(&g,TOUCH_VIEW_ACTIVE);
    touch_gesture_step(&g,TOUCH_VIEW_ACTIVE,true,1,80,220); // One-sample noise.
    release(&g,TOUCH_VIEW_ACTIVE);
    release(&g,TOUCH_VIEW_ACTIVE);
    press(&g,TOUCH_VIEW_NONE,80,220);
    press(&g,TOUCH_VIEW_ACTIVE,80,220);
    release(&g,TOUCH_VIEW_ACTIVE);
}

int main(void)
{
    check_coordinates();
    check_buttons();
    check_gestures();
    puts("Touch rotation, button bounds, debounce, single-action release and cancelled gestures passed.");
    return 0;
}
