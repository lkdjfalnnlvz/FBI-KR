#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "action.h"
#include "../resources.h"
#include "../task/uitask.h"
#include "../../core/core.h"

typedef struct {
    linked_list* items;
    list_item* selected;

    linked_list contents;
    bool all;

    data_op_data deleteInfo;
} delete_pending_titles_data;

static void action_delete_pending_titles_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    delete_pending_titles_data* deleteData = (delete_pending_titles_data*) data;

    u32 index = deleteData->deleteInfo.processed;
    if(index < deleteData->deleteInfo.total) {
        task_draw_pending_title_info(view, (pending_title_info*) ((list_item*) linked_list_get(&deleteData->contents, index))->data, x1, y1, x2, y2);
    }
}

static Result action_delete_pending_titles_delete(void* data, u32 index) {
    delete_pending_titles_data* deleteData = (delete_pending_titles_data*) data;

    list_item* item = (list_item*) linked_list_get(&deleteData->contents, index);
    pending_title_info* info = (pending_title_info*) item->data;

    Result res = 0;

    if(R_SUCCEEDED(res = AM_DeletePendingTitle(info->mediaType, info->titleId))) {
        linked_list_iter iter;
        linked_list_iterate(deleteData->items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            list_item* currItem = (list_item*) linked_list_iter_next(&iter);
            pending_title_info* currInfo = (pending_title_info*) currItem->data;

            if(currInfo->titleId == info->titleId && currInfo->mediaType == info->mediaType) {
                linked_list_iter_remove(&iter);
                task_free_pending_title(currItem);
            }
        }
    }

    return res;
}

static Result action_delete_pending_titles_suspend(void* data, u32 index) {
    return 0;
}

static Result action_delete_pending_titles_restore(void* data, u32 index) {
    return 0;
}

static bool action_delete_pending_titles_error(void* data, u32 index, Result res, ui_view** errorView) {
    *errorView = error_display_res(data, action_delete_pending_titles_draw_top, res, "보류 중인 타이틀을 삭제하는 데 실패했습니다.");
    return true;
}

static void action_delete_pending_titles_free_data(delete_pending_titles_data* data) {
    if(data->all) {
        task_clear_pending_titles(&data->contents);
    }

    linked_list_destroy(&data->contents);
    free(data);
}

static void action_delete_pending_titles_update(ui_view* view, void* data, float* progress, char* text) {
    delete_pending_titles_data* deleteData = (delete_pending_titles_data*) data;

    if(deleteData->deleteInfo.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(deleteData->deleteInfo.result)) {
            prompt_display_notify("성공", "보류 중인 타이틀 삭제됨.", COLOR_TEXT, NULL, NULL, NULL);
        }

        action_delete_pending_titles_free_data(deleteData);

        return;
    }

    if((hidKeysDown() & KEY_B) && !deleteData->deleteInfo.finished) {
        svcSignalEvent(deleteData->deleteInfo.cancelEvent);
    }

    *progress = deleteData->deleteInfo.total > 0 ? (float) deleteData->deleteInfo.processed / (float) deleteData->deleteInfo.total : 0;
    snprintf(text, PROGRESS_TEXT_MAX, "%lu / %lu", deleteData->deleteInfo.processed, deleteData->deleteInfo.total);
}

static void action_delete_pending_titles_onresponse(ui_view* view, void* data, u32 response) {
    delete_pending_titles_data* deleteData = (delete_pending_titles_data*) data;

    if(response == PROMPT_YES) {
        Result res = task_data_op(&deleteData->deleteInfo);
        if(R_SUCCEEDED(res)) {
            info_display("보류 중인 타이틀 삭제 중", "B를 눌러 취소하세요.", true, data, action_delete_pending_titles_update, action_delete_pending_titles_draw_top);
        } else {
            error_display_res(NULL, NULL, res, "삭제 작업을 시작하는 데 실패했습니다.");

            action_delete_pending_titles_free_data(deleteData);
        }
    } else {
        action_delete_pending_titles_free_data(deleteData);
    }
}

typedef struct {
    delete_pending_titles_data* deleteData;

    const char* message;

    populate_pending_titles_data popData;
} delete_pending_titles_loading_data;

static void action_delete_pending_titles_loading_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    action_delete_pending_titles_draw_top(view, ((delete_pending_titles_loading_data*) data)->deleteData, x1, y1, x2, y2);
}

static void action_delete_pending_titles_loading_update(ui_view* view, void* data, float* progress, char* text)  {
    delete_pending_titles_loading_data* loadingData = (delete_pending_titles_loading_data*) data;

    if(loadingData->popData.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(loadingData->popData.result)) {
            loadingData->deleteData->deleteInfo.total = linked_list_size(&loadingData->deleteData->contents);
            loadingData->deleteData->deleteInfo.processed = loadingData->deleteData->deleteInfo.total;

            prompt_display_yes_no("Confirmation", loadingData->message, COLOR_TEXT, loadingData->deleteData, action_delete_pending_titles_draw_top, action_delete_pending_titles_onresponse);
        } else {
            error_display_res(NULL, NULL, loadingData->popData.result, "보류 중인 타이틀 목록을 채우는 데 실패했습니다.");

            action_delete_pending_titles_free_data(loadingData->deleteData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "보류 중인 타이틀 목록을 가져오는 중...");
}

void action_delete_pending_titles(linked_list* items, list_item* selected, const char* message, bool all) {
    delete_pending_titles_data* data = (delete_pending_titles_data*) calloc(1, sizeof(delete_pending_titles_data));
    if(data == NULL) {
        error_display(NULL, NULL, "Failed to allocate delete pending titles data.");

        return;
    }

    data->items = items;
    data->selected = selected;

    data->all = all;

    data->deleteInfo.data = data;

    data->deleteInfo.op = DATAOP_DELETE;

    data->deleteInfo.delete = action_delete_pending_titles_delete;

    data->deleteInfo.suspend = action_delete_pending_titles_suspend;
    data->deleteInfo.restore = action_delete_pending_titles_restore;

    data->deleteInfo.error = action_delete_pending_titles_error;

    data->deleteInfo.finished = true;

    linked_list_init(&data->contents);

    if(all) {
        delete_pending_titles_loading_data* loadingData = (delete_pending_titles_loading_data*) calloc(1, sizeof(delete_pending_titles_loading_data));
        if(loadingData == NULL) {
            error_display(NULL, NULL, "로딩 데이터를 할당하는 데 실패했습니다..");

            action_delete_pending_titles_free_data(data);
            return;
        }

        loadingData->deleteData = data;
        loadingData->message = message;

        loadingData->popData.items = &data->contents;

        Result listRes = task_populate_pending_titles(&loadingData->popData);
        if(R_FAILED(listRes)) {
            error_display_res(NULL, NULL, listRes, "보류 중인 타이틀 목록 채우기에 실패했습니다.");

            free(loadingData);
            action_delete_pending_titles_free_data(data);
            return;
        }

        info_display("로딩 중", "B를 눌러 취소하세요.", false, loadingData, action_delete_pending_titles_loading_update, action_delete_pending_titles_loading_draw_top);
    } else {
        linked_list_add(&data->contents, selected);

        data->deleteInfo.total = 1;
        data->deleteInfo.processed = data->deleteInfo.total;

        prompt_display_yes_no("Confirmation", message, COLOR_TEXT, data, action_delete_pending_titles_draw_top, action_delete_pending_titles_onresponse);
    }
}

void action_delete_pending_title(linked_list* items, list_item* selected) {
    action_delete_pending_titles(items, selected, "선택한 보류 중인 타이틀을 삭제할까요?", false);
}

void action_delete_all_pending_titles(linked_list* items, list_item* selected) {
    action_delete_pending_titles(items, selected, "모든 보류 중인 타이틀을 삭제할까요?", true);
}
