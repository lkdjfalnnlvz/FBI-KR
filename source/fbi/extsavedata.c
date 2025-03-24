#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"

static list_item browse_user_save_data = {"유저 저장 데이터 둘러보기", COLOR_TEXT, action_browse_user_ext_save_data};
static list_item browse_spotpass_save_data = {"Spotpass 저장 데이터 둘러보기", COLOR_TEXT, action_browse_boss_ext_save_data};
static list_item delete_save_data = {"저장 데이터 삭제", COLOR_TEXT, action_delete_ext_save_data};

typedef struct {
    populate_ext_save_data_data populateData;

    bool showSD;
    bool showNAND;
    bool sortById;
    bool sortByName;

    bool populated;
} extsavedata_data;

typedef struct {
    linked_list* items;
    list_item* selected;
} extsavedata_action_data;

static void extsavedata_action_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    task_draw_ext_save_data_info(view, ((extsavedata_action_data*) data)->selected->data, x1, y1, x2, y2);
}

static void extsavedata_action_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    extsavedata_action_data* actionData = (extsavedata_action_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);

        free(data);

        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        void(*action)(linked_list*, list_item*) = (void(*)(linked_list*, list_item*)) selected->data;

      장 데이터 작업", "A: 선택, B: 취소(뒤로 가기)", data, extsavedata_action_update, extsavedata_action_draw_top);
}

static void extsavedata_options_add_entry(linked_list* items, const char* name, bool* val) {
    list_item* item = (list_item*) calloc(1, sizeof(list_item));
    if(item != NULL) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s", name);
        item->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
        item->data = val;

        linked_list_add(items, item);
    }
}

static void extsavedata_options_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    extsavedata_data* listData = (extsavedata_data*) data;

    if(hidKeysDown() & KEY_B) {
        linked_list_iter iter;
        linked_list_iterate(items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            free(linked_list_iter_next(&iter));
            linked_list_iter_remove(&iter);
        }

        ui_pop();
        list_destroy(view);

        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        bool* val = (bool*) selected->data;
        *val = !(*val);

        if(*val && (val == &listData->sortById || val == &listData->sortByName)) {
            if(val == &listData->sortById) {
                listData->sortByName = false;
            } else if(val == &listData->sortByName) {
                listData->sortById = false;
            }

            linked_list_iter iter;
            linked_list_iterate(items, &iter);
            while(linked_list_iter_has_next(&iter)) {
                list_item* item = (list_item*) linked_list_iter_next(&iter);

                item->color = *(bool*) item->data ? COLOR_ENABLED : COLOR_DISABLED;
            }
        } else {
            selected->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
        }

        listData->populated = false;
    }

    if(linked_list_size(items) == 0) {
        extsavedata_options_add_entry(items, "SD 내 파일 표시", &listData->showSD);
        extsavedata_options_add_entry(items, "낸드 내 파일 표시", &listData->showNAND);
        extsavedata_options_add_entry(items, "ID로 정렬", &listData->sortById);
        extsavedata_options_add_entry(items, "이름으로 정렬", &listData->sortByName);
    }
}

static void extsavedata_options_open(extsavedata_data* data) {
    list_display("설정", "A: 토글, B: 뒤로 가기", data, extsavedata_options_update, NULL);
}

static void extsavedata_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    if(selected != NULL && selected->data != NULL) {
        task_draw_ext_save_data_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void extsavedata_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    extsavedata_data* listData = (extsavedata_data*) data;

    if(hidKeysDown() & KEY_B) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) {
                svcSleepThread(1000000);
            }
        }

        ui_pop();

        task_clear_ext_save_data(items);
        list_destroy(view);

        free(listData);
        return;
    }

    if(hidKeysDown() & KEY_SELECT) {
        extsavedata_options_open(listData);
        return;
    }

    if(!listData->populated || (hidKeysDown() & KEY_X)) {
        if(!listData->populateData.finished) {
            svcSignalEvent(listData->populateData.cancelEvent);
            while(!listData->populateData.finished) {
                svcSleepThread(1000000);
            }
        }

        listData->populateData.items = items;
        Result res = task_populate_ext_save_data(&listData->populateData);
        if(R_FAILED(res)) {
            error_display_res(NULL, NULL, res, "추가 저장 데이터 목록 채우기를 시작하지 못했습니다.");
        }

        listData->populated = true;
    }

    if(listData->populateData.finished && R_FAILED(listData->populateData.result)) {
        error_display_res(NULL, NULL, listData->populateData.result, "추가 저장 데이터 목록 채우기에 실패했습니다..");

        listData->populateData.result = 0;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        extsavedata_action_open(items, selected);
        return;
    }
}

static bool extsavedata_filter(void* data, u64 titleId, FS_MediaType mediaType) {
    extsavedata_data* listData = (extsavedata_data*) data;

    if(mediaType == MEDIATYPE_SD) {
        return listData->showSD;
    } else {
        return listData->showNAND;
    }
}

static int extsavedata_compare(void* data, const void* p1, const void* p2) {
    extsavedata_data* listData = (extsavedata_data*) data;

    list_item* info1 = (list_item*) p1;
    list_item* info2 = (list_item*) p2;

    ext_save_data_info* data1 = (ext_save_data_info*) info1->data;
    ext_save_data_info* data2 = (ext_save_data_info*) info2->data;

    if(data1->mediaType > data2->mediaType) {
        return -1;
    } else if(data1->mediaType < data2->mediaType) {
        return 1;
    } else {
        if(listData->sortById) {
            u64 id1 = data1->extSaveDataId;
            u64 id2 = data2->extSaveDataId;

            return id1 > id2 ? 1 : id1 < id2 ? -1 : 0;
        } else if(listData->sortByName) {
            bool title1HasName = data1->hasMeta && !string_is_empty(data1->meta.shortDescription);
            bool title2HasName = data2->hasMeta && !string_is_empty(data2->meta.shortDescription);

            if(title1HasName && !title2HasName) {
                return -1;
            } else if(!title1HasName && title2HasName) {
                return 1;
            } else {
                return strncasecmp(info1->name, info2->name, sizeof(info1->name));
            }
        } else {
            return 0;
        }
    }
}

void extsavedata_open() {
    extsavedata_data* data = (extsavedata_data*) calloc(1, sizeof(extsavedata_data));
    if(data == NULL) {
        error_display(NULL, NULL, "추가 저장 데이터를 할당하지 못했습니다.");

        return;
    }

    data->populateData.userData = data;
    data->populateData.filter = extsavedata_filter;
    data->populateData.compare = extsavedata_compare;

    data->populateData.finished = true;

    data->showSD = true;
    data->showNAND = true;
    data->sortById = false;
    data->sortByName = true;

    list_display("추가 저장 데이터", "A: 선택, B: 뒤로 가기, X: 새로고침, Select: 설정", data, extsavedata_update, extsavedata_draw_top);
}
