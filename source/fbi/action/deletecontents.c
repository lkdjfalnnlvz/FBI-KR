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

    list_item* targetItem;
    file_info* target;

    linked_list contents;

    data_op_data deleteInfo;
} delete_data;

static void action_delete_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    delete_data* deleteData = (delete_data*) data;

    u32 curr = deleteData->deleteInfo.processed;
    if(curr < deleteData->deleteInfo.total) {
        task_draw_file_info(view, ((list_item*) linked_list_get(&deleteData->contents, linked_list_size(&deleteData->contents) - curr - 1))->data, x1, y1, x2, y2);
    } else {
        task_draw_file_info(view, deleteData->target, x1, y1, x2, y2);
    }
}

static Result action_delete_delete(void* data, u32 index) {
    delete_data* deleteData = (delete_data*) data;

    Result res = 0;

    file_info* info = (file_info*) ((list_item*) linked_list_get(&deleteData->contents, linked_list_size(&deleteData->contents) - index - 1))->data;

    FS_Path* fsPath = fs_make_path_utf8(info->path);
    if(fsPath != NULL) {
        if(fs_is_dir(deleteData->target->archive, info->path)) {
            res = FSUSER_DeleteDirectory(deleteData->target->archive, *fsPath);
        } else {
            res = FSUSER_DeleteFile(deleteData->target->archive, *fsPath);
        }

        fs_free_path_utf8(fsPath);
    } else {
        res = R_APP_OUT_OF_MEMORY;
    }

    if(R_SUCCEEDED(res)) {
        linked_list_iter iter;
        linked_list_iterate(deleteData->items, &iter);

        while(linked_list_iter_has_next(&iter)) {
            list_item* item = (list_item*) linked_list_iter_next(&iter);
            file_info* currInfo = (file_info*) item->data;

            if(strncmp(currInfo->path, info->path, FILE_PATH_MAX) == 0) {
                linked_list_iter_remove(&iter);
                task_free_file(item);
            }
        }
    }

    return res;
}

static Result action_delete_suspend(void* data, u32 index) {
    return 0;
}

static Result action_delete_restore(void* data, u32 index) {
    return 0;
}

static bool action_delete_error(void* data, u32 index, Result res, ui_view** errorView) {
    *errorView = error_display_res(data, action_delete_draw_top, res, "콘텐츠를 삭제하는 데 실패했습니다.");
    return true;
}

static void action_delete_free_data(delete_data* data) {
    task_clear_files(&data->contents);
    linked_list_destroy(&data->contents);

    if(data->targetItem != NULL) {
        task_free_file(data->targetItem);
        data->targetItem = NULL;
        data->target = NULL;
    }

    free(data);
}

static void action_delete_update(ui_view* view, void* data, float* progress, char* text) {
    delete_data* deleteData = (delete_data*) data;

    if(deleteData->deleteInfo.finished) {
        FSUSER_ControlArchive(deleteData->target->archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);

        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(deleteData->deleteInfo.result)) {
            prompt_display_notify("성공", "제거됨.", COLOR_TEXT, NULL, NULL, NULL);
        }

        action_delete_free_data(deleteData);

        return;
    }

    if((hidKeysDown() & KEY_B) && !deleteData->deleteInfo.finished) {
        svcSignalEvent(deleteData->deleteInfo.cancelEvent);
    }

    *progress = deleteData->deleteInfo.total > 0 ? (float) deleteData->deleteInfo.processed / (float) deleteData->deleteInfo.total : 0;
    snprintf(text, PROGRESS_TEXT_MAX, "%lu / %lu", deleteData->deleteInfo.processed, deleteData->deleteInfo.total);
}

static void action_delete_onresponse(ui_view* view, void* data, u32 response) {
    delete_data* deleteData = (delete_data*) data;

    if(response == PROMPT_YES) {
        Result res = task_data_op(&deleteData->deleteInfo);
        if(R_SUCCEEDED(res)) {
            info_display("삭제 중", "B를 눌러 튀소하세요.", true, data, action_delete_update, action_delete_draw_top);
        } else {
            error_display_res(NULL, NULL, res, "삭제 설정을 사작하는 데 실패했습니다.");

            action_delete_free_data(deleteData);
        }
    } else {
        action_delete_free_data(deleteData);
    }
}

typedef struct {
    delete_data* deleteData;

    const char* message;

    populate_files_data popData;
} delete_loading_data;

static void action_delete_loading_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    action_delete_draw_top(view, ((delete_loading_data*) data)->deleteData, x1, y1, x2, y2);
}

static void action_delete_loading_update(ui_view* view, void* data, float* progress, char* text)  {
    delete_loading_data* loadingData = (delete_loading_data*) data;

    if(loadingData->popData.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(loadingData->popData.result)) {
            loadingData->deleteData->deleteInfo.total = linked_list_size(&loadingData->deleteData->contents);
            loadingData->deleteData->deleteInfo.processed = loadingData->deleteData->deleteInfo.total;

            prompt_display_yes_no("Confirmation", loadingData->message, COLOR_TEXT, loadingData->deleteData, action_delete_draw_top, action_delete_onresponse);
        } else {
            error_display_res(NULL, NULL, loadingData->popData.result, "콘텐츠 목록을 채우는 데 실패했습니다.");

            action_delete_free_data(loadingData->deleteData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "콘탠츠 목록 가져오는 중...");
}

static void action_delete_internal(linked_list* items, list_item* selected, const char* message, bool recursive, bool includeBase, bool ciasOnly, bool ticketsOnly) {
    delete_data* data = (delete_data*) calloc(1, sizeof(delete_data));
    if(data == NULL) {
        error_display(NULL, NULL, "삭제 데이터를 할당하는 데 실패했습니다.");

        return;
    }

    data->items = items;

    file_info* targetInfo = (file_info*) selected->data;
    Result targetCreateRes = task_create_file_item(&data->targetItem, targetInfo->archive, targetInfo->path, targetInfo->attributes, false);
    if(R_FAILED(targetCreateRes)) {
        error_display_res(NULL, NULL, targetCreateRes, "대상 파일 항목을 생성하지 못했습니다.");

        action_delete_free_data(data);
        return;
    }

    data->target = (file_info*) data->targetItem->data;

    data->deleteInfo.data = data;

    data->deleteInfo.op = DATAOP_DELETE;

    data->deleteInfo.delete = action_delete_delete;

    data->deleteInfo.suspend = action_delete_suspend;
    data->deleteInfo.restore = action_delete_restore;

    data->deleteInfo.error = action_delete_error;

    data->deleteInfo.finished = false;

    linked_list_init(&data->contents);

    delete_loading_data* loadingData = (delete_loading_data*) calloc(1, sizeof(delete_loading_data));
    if(loadingData == NULL) {
        error_display(NULL, NULL, "로딩 데이터를 할당하는 데 실패했습니다.");

        action_delete_free_data(data);
        return;
    }

    loadingData->deleteData = data;
    loadingData->message = message;

    loadingData->popData.items = &data->contents;
    loadingData->popData.archive = data->target->archive;
    string_copy(loadingData->popData.path, data->target->path, FILE_PATH_MAX);
    loadingData->popData.recursive = recursive;
    loadingData->popData.includeBase = includeBase;
    loadingData->popData.meta = false;
    loadingData->popData.filter = ciasOnly ? fs_filter_cias : ticketsOnly ? fs_filter_tickets : NULL;
    loadingData->popData.filterData = NULL;

    Result listRes = task_populate_files(&loadingData->popData);
    if(R_FAILED(listRes)) {
        error_display_res(NULL, NULL, listRes, "콘텐츠 목록 채우기를 시작하지 못했습니다.");

        free(loadingData);
        action_delete_free_data(data);
        return;
    }

    info_display("로딩 중", "B를 눌러 취소하세요.", false, loadingData, action_delete_loading_update, action_delete_loading_draw_top);
}

void action_delete_file(linked_list* items, list_item* selected) {
    action_delete_internal(items, selected, "선택한 파일을 삭제할까요?", false, true, false, false);
}

void action_delete_dir(linked_list* items, list_item* selected) {
    action_delete_internal(items, selected, "현재 폴더를 삭제할까요?", true, true, false, false);
}

void action_delete_dir_contents(linked_list* items, list_item* selected) {
    action_delete_internal(items, selected, "현재 폴더의 모든 콘텐츠를 삭제할까요?", true, false, false, false);
}

void action_delete_dir_cias(linked_list* items, list_item* selected) {
    action_delete_internal(items, selected, "현재 폴더의 모든 CIA 파일을 삭제할까요?", false, false, true, false);
}

void action_delete_dir_tickets(linked_list* items, list_item* selected) {
    action_delete_internal(items, selected, "현재 폴더의 모든 티켓을 삭제할까요?", false, false, false, true);
}
