#include <malloc.h>
#include <stdio.h>

#include <3ds.h>

#include "error.h"
#include "info.h"
#include "ui.h"
#include "../screen.h"
#include "../../fbi/resources.h"

typedef struct {
    bool bar;
    void* data;
    float progress;
    char text[PROGRESS_TEXT_MAX];
    void (*update)(ui_view* view, void* data, float* progress, char* text);
    void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2);
} info_data;

static void info_update(ui_view* view, void* data, float bx1, float by1, float bx2, float by2) {
    info_data* infoData = (info_data*) data;

    if(infoData->update != NULL) {
        infoData->update(view, infoData->data, &infoData->progress, infoData->text);
    }
}

static void info_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    info_data* infoData = (info_data*) data;

    if(infoData->drawTop != NULL) {
        infoData->drawTop(view, infoData->data, x1, y1, x2, y2);
    }
}

static void info_draw_bottom(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    info_data* infoData = (info_data*) data;

    float textWidth;
    float textHeight;
    screen_get_string_size(&textWidth, &textHeight, infoData->text, 0.5f, 0.5f);

    float textX = x1 + (x2 - x1 - textWidth) / 2;
    float textY = y1 + (y2 - y1 - textHeight) / 2;

    if(infoData->bar) {
        u32 progressBarBgWidth;
        u32 progressBarBgHeight;
        screen_get_texture_size(&progressBarBgWidth, &progressBarBgHeight, TEXTURE_PROGRESS_BAR_BG);

        float progressBarBgX = x1 + (x2 - x1 - progressBarBgWidth) / 2;
        float progressBarBgY = y1 + (y2 - y1 - progressBarBgHeight) / 2;
        screen_draw_texture(TEXTURE_PROGRESS_BAR_BG, progressBarBgX, progressBarBgY, progressBarBgWidth, progressBarBgHeight);

        u32 progressBarContentWidth;
        u32 progressBarContentHeight;
        screen_get_texture_size(&progressBarContentWidth, &progressBarContentHeight, TEXTURE_PROGRESS_BAR_CONTENT);

        float progressBarContentX = x1 + (x2 - x1 - progressBarContentWidth) / 2;
        float progressBarContentY = y1 + (y2 - y1 - progressBarContentHeight) / 2;
        screen_draw_texture_crop(TEXTURE_PROGRESS_BAR_CONTENT, progressBarContentX, progressBarContentY, progressBarContentWidth * infoData->progress, progressBarContentHeight);

        textX = x1 + (x2 - x1 - textWidth) / 2;
        textY = progressBarBgY + progressBarBgHeight + 10;
    }

    screen_draw_string(infoData->text, textX, textY, 0.5f, 0.5f, COLOR_TEXT, true);
}

ui_view* info_display(const char* name, const char* info, bool bar, void* data, void (*update)(ui_view* view, void* data, float* progress, char* text),
                                                                                void (*drawTop)(ui_view* view, void* data, float x1, float y1, float x2, float y2)) {
    info_data* infoData = (info_data*) calloc(1, sizeof(info_data));
    if(infoData == NULL) {
        error_display(NULL, NULL, "정보 데이터를 할당하는 데 실패했습니다.");

        return NULL;
    }

    infoData->bar = bar;
    infoData->data = data;
    infoData->progress = 0;
    snprintf(infoData->text, PROGRESS_TEXT_MAX, "잠시 기다려 주세요...");
    infoData->update = update;
    infoData->drawTop = drawTop;

    ui_view* view = ui_create();
    view->name = name;
    view->info = info;
    view->data = infoData;
    view->update = info_update;
    view->drawTop = info_draw_top;
    view->drawBottom = info_draw_bottom;
    ui_push(view);

    return view;
}

void info_destroy(ui_view* view) {
    if(view != NULL) {
        free(view->data);
        ui_destroy(view);
    }
}
