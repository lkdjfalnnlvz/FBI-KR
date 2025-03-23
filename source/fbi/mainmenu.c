#include <malloc.h>
#include <stdio.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "../core/core.h"

static list_item sd = {"SD", COLOR_TEXT, files_open_sd};
static list_item ctr_nand = {"CTR 낸드", COLOR_TEXT, files_open_ctr_nand};
static list_item twl_nand = {"TWL 낸드", COLOR_TEXT, files_open_twl_nand};
static list_item twl_photo = {"TWL 포토", COLOR_TEXT, files_open_twl_photo};
static list_item twl_sound = {"TWL 사운드", COLOR_TEXT, files_open_twl_sound};
static list_item dump_nand = {"낸드 덤프", COLOR_TEXT, dumpnand_open};
static list_item titles = {"타이틀", COLOR_TEXT, titles_open};
static list_item pending_titles = {"보류 중인 타이틀", COLOR_TEXT, pendingtitles_open};
static list_item tickets = {"티켓", COLOR_TEXT, tickets_open};
static list_item ext_save_data = {"외부 세이브 데이터", COLOR_TEXT, extsavedata_open};
static list_item system_save_data = {"시스템 세이브 데이터", COLOR_TEXT, systemsavedata_open};
static list_item remote_install = {"원격 설치", COLOR_TEXT, remoteinstall_open};
static list_item update = {"업데이트", COLOR_TEXT, update_open};

static void mainmenu_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    u32 logoWidth;
    u32 logoHeight;
    screen_get_texture_size(&logoWidth, &logoHeight, TEXTURE_LOGO);

    float logoX = x1 + (x2 - x1 - logoWidth) / 2;
    float logoY = y1 + (y2 - y1 - logoHeight) / 2;
    screen_draw_texture(TEXTURE_LOGO, logoX, logoY, logoWidth, logoHeight);
}

static void mainmenu_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    if(hidKeysDown() & KEY_START) {
        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && (selectedTouched || hidKeysDown() & KEY_A) && selected->data != NULL) {
        ((void(*)()) selected->data)();
        return;
    }

    if(linked_list_size(items) == 0) {
        linked_list_add(items, &sd);
        linked_list_add(items, &ctr_nand);
        linked_list_add(items, &twl_nand);
        linked_list_add(items, &twl_photo);
        linked_list_add(items, &twl_sound);
        linked_list_add(items, &dump_nand);
        linked_list_add(items, &titles);
        linked_list_add(items, &pending_titles);
        linked_list_add(items, &tickets);
        linked_list_add(items, &ext_save_data);
        linked_list_add(items, &system_save_data);
        linked_list_add(items, &remote_install);
        linked_list_add(items, &update);
    }
}

void mainmenu_open() {
    resources_load();

    list_display("메인 메뉴", "A: 선택, START: 나가기", NULL, mainmenu_update, mainmenu_draw_top);
}
