#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "resources.h"
#include "section.h"
#include "action/action.h"
#include "task/uitask.h"
#include "../core/core.h"

static list_item rename_opt = {"이름 변경", COLOR_TEXT, action_rename};
static list_item copy = {"복사", COLOR_TEXT, NULL};
static list_item paste = {"붙여넣기", COLOR_TEXT, action_paste_contents};

static list_item delete_file = {"삭제", COLOR_TEXT, action_delete_file};

static list_item install_cia = {"CIA 설치", COLOR_TEXT, action_install_cia};
static list_item install_and_delete_cia = {"CIA 설치 후 파일 삭제", COLOR_TEXT, action_install_cia_delete};

static list_item install_ticket = {"티켓 설치", COLOR_TEXT, action_install_ticket};
static list_item install_and_delete_ticket = {"티켓 설치 후 파일 삭제", COLOR_TEXT, action_install_ticket_delete};

static list_item delete_dir = {"삭제", COLOR_TEXT, action_delete_dir};
static list_item copy_all_contents = {"모든 내용물 복사", COLOR_TEXT, NULL};
static list_item delete_all_contents = {"모든 내용물 삭제", COLOR_TEXT, action_delete_dir_contents};
static list_item new_folder = {"새 폴더", COLOR_TEXT, action_new_folder};

static list_item install_all_cias = {"모든 CIA 파일 설치", COLOR_TEXT, action_install_cias};
static list_item install_and_delete_all_cias = {"CIA 파일 전부 설치 후 파일 삭제", COLOR_TEXT, action_install_cias_delete};
static list_item delete_all_cias = {"폴더 내 CIA 파일 모두 삭제", COLOR_TEXT, action_delete_dir_cias};

static list_item install_all_tickets = {"티켓 전부 설치", COLOR_TEXT, action_install_tickets};
static list_item install_and_delete_all_tickets = {"티켓 전부 설치 후 파일 삭제", COLOR_TEXT, action_install_tickets_delete};
static list_item delete_all_tickets = {"폴더 내 티켓 전부 삭제", COLOR_TEXT, action_delete_dir_tickets};

typedef struct {
    populate_files_data populateData;

    bool populated;

    FS_ArchiveID archiveId;
    FS_Path archivePath;
    FS_Archive archive;

    bool showHidden;
    bool showDirectories;
    bool showFiles;
    bool showCias;
    bool showTickets;

    char currDir[FILE_PATH_MAX];
} files_data;

typedef struct {
    linked_list* items;
    list_item* selected;
    files_data* parent;

    bool containsCias;
    bool containsTickets;
} files_action_data;

static bool files_filter(void* data, const char* name, u32 attributes) {
    files_data* listData = (files_data*) data;

    if((attributes & FS_ATTRIBUTE_HIDDEN) != 0 && !listData->showHidden) {
        return false;
    }

    if((attributes & FS_ATTRIBUTE_DIRECTORY) != 0) {
        return listData->showDirectories;
    } else {
        if((fs_filter_cias(NULL, name, attributes) && !listData->showCias) || (fs_filter_tickets(NULL, name, attributes) && !listData->showTickets)) {
            return false;
        }

        return listData->showFiles;
    }
}

static void files_action_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    task_draw_file_info(view, ((files_action_data*) data)->selected->data, x1, y1, x2, y2);
}

static void files_action_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    files_action_data* actionData = (files_action_data*) data;

    if(hidKeysDown() & KEY_B) {
        ui_pop();
        list_destroy(view);

        free(data);

        return;
    }

    if(selected != NULL && (selected->data != NULL || selected == &copy || selected == &copy_all_contents) && (selectedTouched || (hidKeysDown() & KEY_A))) {
        void* action = selected->data;

        ui_pop();
        list_destroy(view);

        if(selected == &copy || selected == &copy_all_contents) {
            file_info* info = (file_info*) actionData->selected->data;

            Result res = 0;
            if(R_SUCCEEDED(res = clipboard_set_contents(actionData->parent->archive, info->path, selected == &copy_all_contents))) {
                prompt_display_notify("Success", selected == &copy_all_contents ? "현재 폴더가 클립보드로 복사되었습니다." : (info->attributes & FS_ATTRIBUTE_DIRECTORY) ? "현재 폴더가 클립보드에 복사되었습니다." : "파일이 클립보드에 복사되었습니다.", COLOR_TEXT, info, task_draw_file_info, NULL);
            } else {
                error_display_res(info, task_draw_file_info, res, "클립보드 복사에 실패했습니다.");
            }
        } else if(selected == &install_all_cias || selected == &install_and_delete_all_cias || selected == &install_all_tickets || selected == &install_and_delete_all_tickets) {
            void (*filteredAction)(linked_list*, list_item*, bool (*)(void*, const char*, u32), void*) = action;

            filteredAction(actionData->items, actionData->selected, files_filter, actionData->parent);
        } else {
            void (*normalAction)(linked_list*, list_item*) = action;

            normalAction(actionData->items, actionData->selected);
        }

        free(data);

        return;
    }

    if(linked_list_size(items) == 0) {
        file_info* info = (file_info*) actionData->selected->data;

        if(info->attributes & FS_ATTRIBUTE_DIRECTORY) {
            if(actionData->containsCias) {
                linked_list_add(items, &install_all_cias);
                linked_list_add(items, &install_and_delete_all_cias);
                linked_list_add(items, &delete_all_cias);
            }

            if(actionData->containsTickets) {
                linked_list_add(items, &install_all_tickets);
                linked_list_add(items, &install_and_delete_all_tickets);
                linked_list_add(items, &delete_all_tickets);
            }

            linked_list_add(items, &copy_all_contents);
            linked_list_add(items, &delete_all_contents);

            linked_list_add(items, &new_folder);

            linked_list_add(items, &delete_dir);
        } else {
            if(info->isCia) {
                linked_list_add(items, &install_cia);
                linked_list_add(items, &install_and_delete_cia);
            }

            if(info->isTicket) {
                linked_list_add(items, &install_ticket);
                linked_list_add(items, &install_and_delete_ticket);
            }

            linked_list_add(items, &delete_file);
        }

        linked_list_add(items, &rename_opt);
        linked_list_add(items, &copy);
        linked_list_add(items, &paste);
    }
}

static void files_action_open(linked_list* items, list_item* selected, files_data* parent) {
    files_action_data* data = (files_action_data*) calloc(1, sizeof(files_action_data));
    if(data == NULL) {
        error_display(NULL, NULL, "파일 작업 데이터를 할당하는 데 실패했습니다.");

        return;
    }

    data->items = items;
    data->selected = selected;
    data->parent = parent;

    data->containsCias = false;
    data->containsTickets = false;

    linked_list_iter iter;
    linked_list_iterate(data->items, &iter);

    while(linked_list_iter_has_next(&iter)) {
        file_info* info = (file_info*) ((list_item*) linked_list_iter_next(&iter))->data;

        if(info->isCia) {
            data->containsCias = true;
        } else if(info->isTicket) {
            data->containsTickets = true;
        }
    }

    list_display((((file_info*) selected->data)->attributes & FS_ATTRIBUTE_DIRECTORY) ? "폴더 작업" : "파일 작업", "A: 선택, B: 뒤로 가기", data, files_action_update, files_action_draw_top);
}

static void files_options_add_entry(linked_list* items, const char* name, bool* val) {
    list_item* item = (list_item*) calloc(1, sizeof(list_item));
    if(item != NULL) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s", name);
        item->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
        item->data = val;

        linked_list_add(items, item);
    }
}

static void files_options_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    files_data* listData = (files_data*) data;

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

        selected->color = *val ? COLOR_ENABLED : COLOR_DISABLED;

        listData->populated = false;
    }

    if(linked_list_size(items) == 0) {
        files_options_add_entry(items, "숨김 파일 보기", &listData->showHidden);
        files_options_add_entry(items, "폴더 보기", &listData->showDirectories);
        files_options_add_entry(items, "파일 보기", &listData->showFiles);
        files_options_add_entry(items, "CIA 보기", &listData->showCias);
        files_options_add_entry(items, "티켓 보기", &listData->showTickets);
    }
}

static void files_options_open(files_data* data) {
    list_display("설정", "A: 토글, B: 뒤로 가기", data, files_options_update, NULL);
}

static void files_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    if(selected != NULL && selected->data != NULL) {
        task_draw_file_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void files_repopulate(files_data* listData, linked_list* items) {
    if(!listData->populateData.finished) {
        svcSignalEvent(listData->populateData.cancelEvent);
        while(!listData->populateData.finished) {
            svcSleepThread(1000000);
        }
    }

    listData->populateData.items = items;
    listData->populateData.archive = listData->archive;
    string_copy(listData->populateData.path, listData->currDir, FILE_PATH_MAX);

    Result res = task_populate_files(&listData->populateData);
    if(R_FAILED(res)) {
        error_display_res(NULL, NULL, res, "파일 목록 채우기를 시작하지 못했습니다.");
    }

    listData->populated = true;
}

static void files_navigate(files_data* listData, linked_list* items, const char* path) {
    string_copy(listData->currDir, path, FILE_PATH_MAX);

    listData->populated = false;
}

static void files_free_data(files_data* data) {
    if(!data->populateData.finished) {
        svcSignalEvent(data->populateData.cancelEvent);
        while(!data->populateData.finished) {
            svcSleepThread(1000000);
        }
    }

    if(data->archive != 0) {
        fs_close_archive(data->archive);
        data->archive = 0;
    }

    if(data->archivePath.data != NULL) {
        free((void*) data->archivePath.data);
        data->archivePath.data = NULL;
    }

    free(data);
}

static void files_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
    files_data* listData = (files_data*) data;

    if(listData->populated) {
        // Detect whether the current directory was renamed by an action.
        list_item* currDirItem = linked_list_get(items, 0);
        if(currDirItem != NULL && strncmp(listData->currDir, ((file_info*) currDirItem->data)->path, FILE_PATH_MAX) != 0) {
            string_copy(listData->currDir, ((file_info*) currDirItem->data)->path, FILE_PATH_MAX);
        }
    }

    while(!fs_is_dir(listData->archive, listData->currDir)) {
        char parentDir[FILE_PATH_MAX] = {'\0'};
        string_get_parent_path(parentDir, listData->currDir, FILE_PATH_MAX);

        files_navigate(listData, items, parentDir);
    }

    if(hidKeysDown() & KEY_B) {
        if(strncmp(listData->currDir, "/", FILE_PATH_MAX) == 0) {
            ui_pop();

            files_free_data(listData);

            task_clear_files(items);
            list_destroy(view);

            return;
        } else {
            char parentDir[FILE_PATH_MAX] = {'\0'};
            string_get_parent_path(parentDir, listData->currDir, FILE_PATH_MAX);

            files_navigate(listData, items, parentDir);
        }
    }

    if(hidKeysDown() & KEY_SELECT) {
        files_options_open(listData);
        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        file_info* fileInfo = (file_info*) selected->data;

        if((fileInfo->attributes & FS_ATTRIBUTE_DIRECTORY) && strncmp(selected->name, "<현재 폴더>", LIST_ITEM_NAME_MAX) != 0) {
            files_navigate(listData, items, fileInfo->path);
        } else {
            files_action_open(items, selected, listData);
            return;
        }
    }

    if(!listData->populated || (hidKeysDown() & KEY_X)) {
        files_repopulate(listData, items);
    }

    if(listData->populateData.finished && R_FAILED(listData->populateData.result)) {
        error_display_res(NULL, NULL, listData->populateData.result, "파일 목록을 채오는 데 실패했습니다.");

        listData->populateData.result = 0;
    }
}

void files_open(FS_ArchiveID archiveId, FS_Path archivePath) {
    files_data* data = (files_data*) calloc(1, sizeof(files_data));
    if(data == NULL) {
        error_display(NULL, NULL, "파일 데이터를 할당하는 데 실패했습니다.");

        return;
    }

    data->populateData.recursive = false;
    data->populateData.includeBase = true;
    data->populateData.meta = true;

    data->populateData.filter = files_filter;
    data->populateData.filterData = data;

    data->populateData.finished = true;

    data->populated = false;

    data->showHidden = false;
    data->showDirectories = true;
    data->showFiles = true;
    data->showCias = true;
    data->showTickets = true;

    data->archiveId = archiveId;
    data->archivePath.type = archivePath.type;
    data->archivePath.size = archivePath.size;
    if(archivePath.data != NULL) {
        data->archivePath.data = calloc(1, data->archivePath.size);
        if(data->archivePath.data == NULL) {
            error_display(NULL, NULL, "파일 데이터를 할당하는 데 실패했습니다.");

            files_free_data(data);
            return;
        }

        memcpy((void*) data->archivePath.data, archivePath.data, data->archivePath.size);
    } else {
        data->archivePath.data = NULL;
    }

    snprintf(data->currDir, FILE_PATH_MAX, "/");

    Result res = 0;
    if(R_FAILED(res = fs_open_archive(&data->archive, archiveId, archivePath))) {
        error_display_res(NULL, NULL, res, "파일 목록 아카이브를 열 수 없습니다.");

        files_free_data(data);
        return;
    }

    list_display("파일", "A: 선택, B: 뒤로, X: 새로고침, Select: 설정", data, files_update, files_draw_top);
}

static void files_open_nand_warning_onresponse(ui_view* view, void* data, u32 response) {
    FS_ArchiveID archive = (FS_ArchiveID) data;

    if(response == PROMPT_YES) {
        files_open(archive, fsMakePath(PATH_EMPTY, ""));
    }
}

void files_open_nand_warning(FS_ArchiveID archive) {
    prompt_display_yes_no("확인", "낸드를 수정하는 것은 매우 위험하며\n 시스템을 벽돌로 만들 수 있습니다.\n자신이 무엇을 하는지 잘 알고 있어야 합니다.\n\n진행할까요?", COLOR_TEXT, (void*) archive, NULL, files_open_nand_warning_onresponse);
}

void files_open_sd() {
    files_open(ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
}

void files_open_ctr_nand() {
    files_open_nand_warning(ARCHIVE_NAND_CTR_FS);
}

void files_open_twl_nand() {
    files_open_nand_warning(ARCHIVE_NAND_TWL_FS);
}

void files_open_twl_photo() {
    files_open(ARCHIVE_TWL_PHOTO, fsMakePath(PATH_EMPTY, ""));
}

void files_open_twl_sound() {
    files_open(ARCHIVE_TWL_SOUND, fsMakePath(PATH_EMPTY, ""));
}
