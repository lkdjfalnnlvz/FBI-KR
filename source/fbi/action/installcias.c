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

    bool delete;

    volatile bool n3dsContinue;

    data_op_data installInfo;
} install_cias_data;

static void action_install_cias_n3ds_onresponse(ui_view* view, void* data, u32 response) {
    ((install_cias_data*) data)->n3dsContinue = response == PROMPT_YES;
}

static void action_install_cias_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    install_cias_data* installData = (install_cias_data*) data;

    u32 curr = installData->installInfo.processed;
    if(curr < installData->installInfo.total) {
        task_draw_file_info(view, ((list_item*) linked_list_get(&installData->contents, curr))->data, x1, y1, x2, y2);
    } else {
        task_draw_file_info(view, installData->target, x1, y1, x2, y2);
    }
}

static Result action_install_cias_is_src_directory(void* data, u32 index, bool* isDirectory) {
    *isDirectory = false;
    return 0;
}

static Result action_install_cias_make_dst_directory(void* data, u32 index) {
    return 0;
}

static Result action_install_cias_open_src(void* data, u32 index, u32* handle) {
    install_cias_data* installData = (install_cias_data*) data;

    file_info* info = (file_info*) ((list_item*) linked_list_get(&installData->contents, index))->data;

    Result res = 0;

    FS_Path* fsPath = fs_make_path_utf8(info->path);
    if(fsPath != NULL) {
        res = FSUSER_OpenFile(handle, info->archive, *fsPath, FS_OPEN_READ, 0);

        fs_free_path_utf8(fsPath);
    } else {
        res = R_APP_OUT_OF_MEMORY;
    }

    return res;
}

static Result action_install_cias_close_src(void* data, u32 index, bool succeeded, u32 handle) {
    install_cias_data* installData = (install_cias_data*) data;

    file_info* info = (file_info*) ((list_item*) linked_list_get(&installData->contents, index))->data;

    Result res = 0;

    if(R_SUCCEEDED(res = FSFILE_Close(handle)) && installData->delete && succeeded) {
        FS_Path* fsPath = fs_make_path_utf8(info->path);
        if(fsPath != NULL) {
            if(R_SUCCEEDED(FSUSER_DeleteFile(info->archive, *fsPath))) {
                linked_list_iter iter;
                linked_list_iterate(installData->items, &iter);

                while(linked_list_iter_has_next(&iter)) {
                    list_item* item = (list_item*) linked_list_iter_next(&iter);
                    file_info* currInfo = (file_info*) item->data;

                    if(strncmp(currInfo->path, info->path, FILE_PATH_MAX) == 0) {
                        linked_list_iter_remove(&iter);
                        task_free_file(item);
                    }
                }
            }

            fs_free_path_utf8(fsPath);
        } else {
            res = R_APP_OUT_OF_MEMORY;
        }
    }

    return res;
}

static Result action_install_cias_get_src_size(void* data, u32 handle, u64* size) {
    return FSFILE_GetSize(handle, size);
}

static Result action_install_cias_read_src(void* data, u32 handle, u32* bytesRead, void* buffer, u64 offset, u32 size) {
    return FSFILE_Read(handle, bytesRead, offset, buffer, size);
}

static Result action_install_cias_open_dst(void* data, u32 index, void* initialReadBlock, u64 size, u32* handle) {
    install_cias_data* installData = (install_cias_data*) data;

    installData->n3dsContinue = false;

    file_info* info = (file_info*) ((list_item*) linked_list_get(&installData->contents, index))->data;

    FS_MediaType dest = fs_get_title_destination(info->ciaInfo.titleId);

    bool n3ds = false;
    if(R_SUCCEEDED(APT_CheckNew3DS(&n3ds)) && !n3ds && ((info->ciaInfo.titleId >> 28) & 0xF) == 2) {
        ui_view* view = prompt_display_yes_no("확인", "타이틀이 New 3DS 전용입니다.\n계속할까요?", COLOR_TEXT, data, action_install_cias_draw_top, action_install_cias_n3ds_onresponse);
        if(view != NULL) {
            svcWaitSynchronization(view->active, U64_MAX);
        }

        if(!installData->n3dsContinue) {
            return R_APP_SKIPPED;
        }
    }

    // Deleting FBI before it reinstalls itself causes issues.
    u64 currTitleId = 0;
    FS_MediaType currMediaType = MEDIATYPE_NAND;

    if(envIsHomebrew() || R_FAILED(APT_GetAppletInfo((NS_APPID) envGetAptAppId(), &currTitleId, (u8*) &currMediaType, NULL, NULL, NULL)) || info->ciaInfo.titleId != currTitleId || dest != currMediaType) {
        AM_DeleteTitle(dest, info->ciaInfo.titleId);
        AM_DeleteTicket(info->ciaInfo.titleId);

        if(dest == MEDIATYPE_SD) {
            AM_QueryAvailableExternalTitleDatabase(NULL);
        }
    }

    return AM_StartCiaInstall(dest, handle);
}

static Result action_install_cias_close_dst(void* data, u32 index, bool succeeded, u32 handle) {
    if(succeeded) {
        install_cias_data* installData = (install_cias_data*) data;

        file_info* info = (file_info*) ((list_item*) linked_list_get(&installData->contents, index))->data;

        Result res = 0;
        if(R_SUCCEEDED(res = AM_FinishCiaInstall(handle))) {
            http_download_seed(info->ciaInfo.titleId);

            if((info->ciaInfo.titleId & 0xFFFFFFF) == 0x0000002) {
                res = AM_InstallFirm(info->ciaInfo.titleId);
            }
        }

        return res;
    } else {
        return AM_CancelCIAInstall(handle);
    }
}

static Result action_install_cias_write_dst(void* data, u32 handle, u32* bytesWritten, void* buffer, u64 offset, u32 size) {
    return FSFILE_Write(handle, bytesWritten, offset, buffer, size, 0);
}

static Result action_install_cias_suspend(void* data, u32 index) {
    return 0;
}

static Result action_install_cias_restore(void* data, u32 index) {
    return 0;
}

bool action_install_cias_error(void* data, u32 index, Result res, ui_view** errorView) {
    *errorView = error_display_res(data, action_install_cias_draw_top, res, "CIA 파일 설치에 실패했습니다.");
    return true;
}

static void action_install_cias_free_data(install_cias_data* data) {
    task_clear_files(&data->contents);
    linked_list_destroy(&data->contents);

    if(data->targetItem != NULL) {
        task_free_file(data->targetItem);
        data->targetItem = NULL;
        data->target = NULL;
    }

    free(data);
}

static void action_install_cias_update(ui_view* view, void* data, float* progress, char* text) {
    install_cias_data* installData = (install_cias_data*) data;

    if(installData->installInfo.finished) {
        if(installData->delete) {
            FSUSER_ControlArchive(installData->target->archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
        }

        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(installData->installInfo.result)) {
            prompt_display_notify("성공", "설치 완료.", COLOR_TEXT, NULL, NULL, NULL);
        }

        action_install_cias_free_data(installData);

        return;
    }

    if((hidKeysDown() & KEY_B) && !installData->installInfo.finished) {
        svcSignalEvent(installData->installInfo.cancelEvent);
    }

    *progress = installData->installInfo.currTotal != 0 ? (float) ((double) installData->installInfo.currProcessed / (double) installData->installInfo.currTotal) : 0;
    snprintf(text, PROGRESS_TEXT_MAX, "%lu / %lu\n%.2f %s / %.2f %s\n%.2f %s/s, 남은 시간 %s", installData->installInfo.processed, installData->installInfo.total,
             ui_get_display_size(installData->installInfo.currProcessed),
             ui_get_display_size_units(installData->installInfo.currProcessed),
             ui_get_display_size(installData->installInfo.currTotal),
             ui_get_display_size_units(installData->installInfo.currTotal),
             ui_get_display_size(installData->installInfo.bytesPerSecond),
             ui_get_display_size_units(installData->installInfo.bytesPerSecond),
             ui_get_display_eta(installData->installInfo.estimatedRemainingSeconds));
}

static void action_install_cias_onresponse(ui_view* view, void* data, u32 response) {
    install_cias_data* installData = (install_cias_data*) data;

    if(response == PROMPT_YES) {
        Result res = task_data_op(&installData->installInfo);
        if(R_SUCCEEDED(res)) {
            info_display("CIA 설치 중", "B를 눌러 취소허세요.", true, data, action_install_cias_update, action_install_cias_draw_top);
        } else {
            error_display_res(NULL, NULL, res, "CIA 설치를 시작하는 데 실패했습니다.");

            action_install_cias_free_data(installData);
        }
    } else {
        action_install_cias_free_data(installData);
    }
}

typedef struct {
    install_cias_data* installData;

    const char* message;

    fs_filter_data filterData;
    populate_files_data popData;
} install_cias_loading_data;

static void action_install_cias_loading_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2) {
    action_install_cias_draw_top(view, ((install_cias_loading_data*) data)->installData, x1, y1, x2, y2);
}

static void action_install_cias_loading_update(ui_view* view, void* data, float* progress, char* text)  {
    install_cias_loading_data* loadingData = (install_cias_loading_data*) data;

    if(loadingData->popData.finished) {
        ui_pop();
        info_destroy(view);

        if(R_SUCCEEDED(loadingData->popData.result)) {
            loadingData->installData->installInfo.total = linked_list_size(&loadingData->installData->contents);
            loadingData->installData->installInfo.processed = loadingData->installData->installInfo.total;

            prompt_display_yes_no("확인", loadingData->message, COLOR_TEXT, loadingData->installData, action_install_cias_draw_top, action_install_cias_onresponse);
        } else {
            error_display_res(NULL, NULL, loadingData->popData.result, "CIA 목록 채우기에 실패했습니다.");

            action_install_cias_free_data(loadingData->installData);
        }

        free(loadingData);
        return;
    }

    if((hidKeysDown() & KEY_B) && !loadingData->popData.finished) {
        svcSignalEvent(loadingData->popData.cancelEvent);
    }

    snprintf(text, PROGRESS_TEXT_MAX, "CIA 목록 가져오는 중...");
}

static void action_install_cias_internal(linked_list* items, list_item* selected, bool (*filter)(void* data, const char* name, u32 attributes), void* filterData, const char* message, bool delete) {
    install_cias_data* data = (install_cias_data*) calloc(1, sizeof(install_cias_data));
    if(data == NULL) {
        error_display(NULL, NULL, "CIA 설치 데이터를 할당하는 데 실패했습니다.");

        return;
    }

    data->items = items;

    file_info* targetInfo = (file_info*) selected->data;
    Result targetCreateRes = task_create_file_item(&data->targetItem, targetInfo->archive, targetInfo->path, targetInfo->attributes, true);
    if(R_FAILED(targetCreateRes)) {
        error_display_res(NULL, NULL, targetCreateRes, "대상 파일 항목을 만드는 데 실패했습니다.");

        action_install_cias_free_data(data);
        return;
    }

    data->target = (file_info*) data->targetItem->data;

    data->delete = delete;

    data->n3dsContinue = false;

    data->installInfo.data = data;

    data->installInfo.op = DATAOP_COPY;

    data->installInfo.bufferSize = 256 * 1024;
    data->installInfo.copyEmpty = false;

    data->installInfo.isSrcDirectory = action_install_cias_is_src_directory;
    data->installInfo.makeDstDirectory = action_install_cias_make_dst_directory;

    data->installInfo.openSrc = action_install_cias_open_src;
    data->installInfo.closeSrc = action_install_cias_close_src;
    data->installInfo.getSrcSize = action_install_cias_get_src_size;
    data->installInfo.readSrc = action_install_cias_read_src;

    data->installInfo.openDst = action_install_cias_open_dst;
    data->installInfo.closeDst = action_install_cias_close_dst;
    data->installInfo.writeDst = action_install_cias_write_dst;

    data->installInfo.suspend = action_install_cias_suspend;
    data->installInfo.restore = action_install_cias_restore;

    data->installInfo.error = action_install_cias_error;

    data->installInfo.finished = true;

    linked_list_init(&data->contents);

    install_cias_loading_data* loadingData = (install_cias_loading_data*) calloc(1, sizeof(install_cias_loading_data));
    if(loadingData == NULL) {
        error_display(NULL, NULL, "로딩 데이터를 할당하는 데 실패했습니다.");

        action_install_cias_free_data(data);
        return;
    }

    loadingData->installData = data;
    loadingData->message = message;

    loadingData->filterData.parentFilter = filter;
    loadingData->filterData.parentFilterData = filterData;

    loadingData->popData.items = &data->contents;
    loadingData->popData.archive = data->target->archive;
    string_copy(loadingData->popData.path, data->target->path, FILE_PATH_MAX);
    loadingData->popData.recursive = false;
    loadingData->popData.includeBase = !(data->target->attributes & FS_ATTRIBUTE_DIRECTORY);
    loadingData->popData.meta = true;
    loadingData->popData.filter = fs_filter_cias;
    loadingData->popData.filterData = &loadingData->filterData;

    Result listRes = task_populate_files(&loadingData->popData);
    if(R_FAILED(listRes)) {
        error_display_res(NULL, NULL, listRes, "CIA 목록 채우기를 시작하지 못했습니다.");

        free(loadingData);
        action_install_cias_free_data(data);
        return;
    }

    info_display("로딩 중", "B를 눌러 취소하세요..", false, loadingData, action_install_cias_loading_update, action_install_cias_loading_draw_top);
}

void action_install_cia(linked_list* items, list_item* selected) {
    action_install_cias_internal(items, selected, NULL, NULL, "선택한 CIA 파일을 설치할까요?", false);
}

void action_install_cia_delete(linked_list* items, list_item* selected) {
    action_install_cias_internal(items, selected, NULL, NULL, "선택한 CIA 파일을 설치하고 파일을 삭제할까요?", true);
}

void action_install_cias(linked_list* items, list_item* selected, bool (*filter)(void* data, const char* name, u32 attributes), void* filterData) {
    action_install_cias_internal(items, selected, filter, filterData, "현재 폴더의 모든 CIA 파일을 설치할까요?", false);
}

void action_install_cias_delete(linked_list* items, list_item* selected, bool (*filter)(void* data, const char* name, u32 attributes), void* filterData) {
    action_install_cias_internal(items, selected, filter, filterData, "현재 폴더의 모든 CIA 파일을 설치하고 파일을 삭제할까요??", true);
}
