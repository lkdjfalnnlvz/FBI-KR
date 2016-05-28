#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "section.h"
#include "action/action.h"
#include "action/clipboard.h"
#include "task/task.h"
#include "../error.h"
#include "../list.h"
#include "../prompt.h"
#include "../ui.h"
#include "../../core/linkedlist.h"
#include "../../core/screen.h"
#include "../../core/util.h"

static list_item copy = {"Copy", COLOR_TEXT, NULL};
static list_item paste = {"Paste", COLOR_TEXT, action_paste_contents};

static list_item delete_file = {"Delete", COLOR_TEXT, action_delete_file};

static list_item install_cia = {"Install CIA", COLOR_TEXT, action_install_cia};
static list_item install_and_delete_cia = {"Install and delete CIA", COLOR_TEXT, action_install_cia_delete};

static list_item install_ticket = {"Install ticket", COLOR_TEXT, action_install_ticket};
static list_item install_and_delete_ticket = {"Install and delete ticket", COLOR_TEXT, action_install_ticket_delete};

static list_item delete_dir = {"Delete", COLOR_TEXT, action_delete_dir};
static list_item delete_all_contents = {"Delete all contents", COLOR_TEXT, action_delete_dir_contents};
static list_item copy_all_contents = {"Copy all contents", COLOR_TEXT, NULL};

static list_item install_all_cias = {"Install all CIAs", COLOR_TEXT, action_install_cias};
static list_item install_and_delete_all_cias = {"Install and delete all CIAs", COLOR_TEXT, action_install_cias_delete};
static list_item delete_all_cias = {"Delete all CIAs", COLOR_TEXT, action_delete_dir_cias};

static list_item install_all_tickets = {"Install all tickets", COLOR_TEXT, action_install_tickets};
static list_item install_and_delete_all_tickets = {"Install and delete all tickets", COLOR_TEXT, action_install_tickets_delete};
static list_item delete_all_tickets = {"Delete all tickets", COLOR_TEXT, action_delete_dir_tickets};

typedef struct {
    populate_files_data populateData;

    bool populated;

    FS_ArchiveID archiveId;
    FS_Path archivePath;
    FS_Archive archive;

    bool showDirectories;
    bool showCias;
    bool showTickets;
    bool showMisc;

    char currDir[FILE_PATH_MAX];
    list_item* dirItem;
} files_data;

typedef struct {
    linked_list* items;
    list_item* selected;
    files_data* parent;
} files_action_data;

static void files_action_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    ui_draw_file_info(view, ((files_action_data*) data)->selected->data, x1, y1, x2, y2);
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
        void(*action)(linked_list*, list_item*) = (void(*)(linked_list*, list_item*)) selected->data;

        ui_pop();
        list_destroy(view);

        if(selected == &copy || selected == &copy_all_contents) {
            file_info* info = (file_info*) actionData->selected->data;

            Result res = 0;
            if(R_SUCCEEDED(res = clipboard_set_contents(actionData->parent->archiveId, &actionData->parent->archivePath, info->path, selected == &copy_all_contents))) {
                prompt_display("Success", selected == &copy_all_contents ? "Current directory contents copied to clipboard." : info->isDirectory ? "Current directory copied to clipboard." : "File copied to clipboard.", COLOR_TEXT, false, info, NULL, ui_draw_file_info, NULL);
            } else {
                error_display_res(NULL, info, ui_draw_file_info, res, "Failed to copy to clipboard.");
            }
        } else {
            action(actionData->items, actionData->selected);
        }

        free(data);

        return;
    }

    if(linked_list_size(items) == 0) {
        file_info* info = (file_info*) actionData->selected->data;

        if(info->isDirectory) {
            if(info->containsCias) {
                linked_list_add(items, &install_all_cias);
                linked_list_add(items, &install_and_delete_all_cias);
                linked_list_add(items, &delete_all_cias);
            }

            if(info->containsTickets) {
                linked_list_add(items, &install_all_tickets);
                linked_list_add(items, &install_and_delete_all_tickets);
                linked_list_add(items, &delete_all_tickets);
            }

            linked_list_add(items, &delete_all_contents);
            linked_list_add(items, &copy_all_contents);

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

        linked_list_add(items, &copy);
        linked_list_add(items, &paste);
    }
}

static void files_action_open(linked_list* items, list_item* selected, files_data* parent) {
    files_action_data* data = (files_action_data*) calloc(1, sizeof(files_action_data));
    if(data == NULL) {
        error_display(NULL, NULL, NULL, "Failed to allocate files action data.");

        return;
    }

    data->items = items;
    data->selected = selected;
    data->parent = parent;

    list_display(((file_info*) selected->data)->isDirectory ? "Directory Action" : "File Action", "A: Select, B: Return", data, files_action_update, files_action_draw_top);
}

static void files_filters_add_entry(linked_list* items, const char* name, bool* val) {
    list_item* item = (list_item*) calloc(1, sizeof(list_item));
    if(item != NULL) {
        snprintf(item->name, LIST_ITEM_NAME_MAX, "%s", name);
        item->color = *val ? COLOR_ENABLED : COLOR_DISABLED;
        item->data = val;

        linked_list_add(items, item);
    }
}

static void files_filters_update(ui_view* view, void* data, linked_list* items, list_item* selected, bool selectedTouched) {
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
        files_filters_add_entry(items, "Show directories", &listData->showDirectories);
        files_filters_add_entry(items, "Show CIAs", &listData->showCias);
        files_filters_add_entry(items, "Show tickets", &listData->showTickets);
        files_filters_add_entry(items, "Show miscellaneous", &listData->showMisc);
    }
}

static void files_filters_open(files_data* data) {
    list_display("Filters", "A: Toggle, B: Return", data, files_filters_update, NULL);
}

static void files_draw_top(ui_view* view, void* data, float x1, float y1, float x2, float y2, list_item* selected) {
    if(selected != NULL && selected->data != NULL) {
        ui_draw_file_info(view, selected->data, x1, y1, x2, y2);
    }
}

static void files_repopulate(files_data* listData, linked_list* items) {
    if(!listData->populateData.finished) {
        svcSignalEvent(listData->populateData.cancelEvent);
        while(!listData->populateData.finished) {
            svcSleepThread(1000000);
        }
    }

    if(listData->dirItem != NULL) {
        task_free_file(listData->dirItem);
        listData->dirItem = NULL;
    }

    Result res = 0;
    if(R_SUCCEEDED(res = task_create_file_item(&listData->dirItem, listData->archive, listData->currDir))) {
        listData->populateData.items = items;
        listData->populateData.base = (file_info*) listData->dirItem->data;

        res = task_populate_files(&listData->populateData);
    }

    if(R_FAILED(res)) {
        error_display_res(NULL, NULL, NULL, res, "Failed to initiate file list population.");
    }

    listData->populated = true;
}

static void files_navigate(files_data* listData, linked_list* items, const char* path) {
    strncpy(listData->currDir, path, FILE_PATH_MAX);

    listData->populated = false;
}

static void files_free_data(files_data* data) {
    if(!data->populateData.finished) {
        svcSignalEvent(data->populateData.cancelEvent);
        while(!data->populateData.finished) {
            svcSleepThread(1000000);
        }
    }

    if(data->dirItem != NULL) {
        task_free_file(data->dirItem);
        data->dirItem = NULL;
    }

    if(data->archive != 0) {
        FSUSER_CloseArchive(data->archive);
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

    while(!util_is_dir(listData->archive, listData->currDir)) {
        char parentDir[FILE_PATH_MAX] = {'\0'};
        util_get_parent_path(parentDir, listData->currDir, FILE_PATH_MAX);

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
            util_get_parent_path(parentDir, listData->currDir, FILE_PATH_MAX);

            files_navigate(listData, items, parentDir);
        }
    }

    if(hidKeysDown() & KEY_SELECT) {
        files_filters_open(listData);
        return;
    }

    if((hidKeysDown() & KEY_Y) && listData->dirItem != NULL) {
        files_action_open(items, listData->dirItem, listData);
        return;
    }

    if(selected != NULL && selected->data != NULL && (selectedTouched || (hidKeysDown() & KEY_A))) {
        file_info* fileInfo = (file_info*) selected->data;

        if(fileInfo->isDirectory) {
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
        error_display_res(NULL, NULL, NULL, listData->populateData.result, "Failed to populate file list.");

        listData->populateData.result = 0;
    }
}

static bool files_filter(void* data, const char* name, u32 attributes) {
    files_data* listData = (files_data*) data;

    if((attributes & FS_ATTRIBUTE_DIRECTORY) != 0) {
        return listData->showDirectories;
    } else {
        size_t len = strlen(name);
        if(len >= 4) {
            const char* extension = name + len - 4;
            if(strncasecmp(extension, ".cia", 4) == 0) {
                return listData->showCias;
            } else if(strncasecmp(extension, ".tik", 4) == 0) {
                return listData->showTickets;
            }
        }
    }

    return listData->showMisc;
}

void files_open(FS_ArchiveID archiveId, FS_Path archivePath) {
    files_data* data = (files_data*) calloc(1, sizeof(files_data));
    if(data == NULL) {
        error_display(NULL, NULL, NULL, "Failed to allocate files data.");

        return;
    }

    data->populateData.recursive = false;
    data->populateData.includeBase = false;
    data->populateData.dirsFirst = true;

    data->populateData.filter = files_filter;
    data->populateData.filterData = data;

    data->populateData.finished = true;

    data->populated = false;

    data->showDirectories = true;
    data->showCias = true;
    data->showTickets = true;
    data->showMisc = true;

    data->archiveId = archiveId;
    data->archivePath.type = archivePath.type;
    data->archivePath.size = archivePath.size;
    if(archivePath.data != NULL) {
        data->archivePath.data = calloc(1, data->archivePath.size);
        if(data->archivePath.data == NULL) {
            error_display(NULL, NULL, NULL, "Failed to allocate files data.");

            files_free_data(data);
            return;
        }

        memcpy((void*) data->archivePath.data, archivePath.data, data->archivePath.size);
    } else {
        data->archivePath.data = NULL;
    }

    snprintf(data->currDir, FILE_PATH_MAX, "/");
    data->dirItem = NULL;

    Result res = 0;
    if(R_FAILED(res = FSUSER_OpenArchive(&data->archive, archiveId, archivePath))) {
        error_display_res(NULL, NULL, NULL, res, "Failed to open file listing archive.");

        files_free_data(data);
        return;
    }

    list_display("Files", "A: Select, B: Back, X: Refresh, Y: Dir, Select: Filter", data, files_update, files_draw_top);
}

void files_open_sd() {
    files_open(ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
}

void files_open_ctr_nand() {
    files_open(ARCHIVE_NAND_CTR_FS, fsMakePath(PATH_EMPTY, ""));
}

void files_open_twl_nand() {
    files_open(ARCHIVE_NAND_TWL_FS, fsMakePath(PATH_EMPTY, ""));
}

void files_open_twl_photo() {
    files_open(ARCHIVE_TWL_PHOTO, fsMakePath(PATH_EMPTY, ""));
}

void files_open_twl_sound() {
    files_open(ARCHIVE_TWL_SOUND, fsMakePath(PATH_EMPTY, ""));
}